# First Submission

## Summary

On the tested MediaTek Mali-G615 the Manvil driver has completed a
full submit cycle end to end. It opens the Kbase device, reads the
GPU and CSF capabilities, allocates GPU memory, creates a compute
queue group, binds a queue to that group, creates a synchronization
object, submits a command batch through the queue, waits for the
firmware to process it, and reads back the signaled value. The full
sequence runs in a few tens of milliseconds on cold cache and
completes in well under a millisecond per submission once the queue
is warm.

This document records what was achieved, the three specific
discoveries that made it work, and the public sources that were
consulted. It is intended as a milestone marker and as a reference
for anyone porting Manvil to a different device.

## The pipeline

The submission path exercised by the mprobe tool is:
application code
    |
    | builds a batch of CSF command entries
    v
manvil_sched_submit
    |
    | writes the entries to the ring buffer
    | updates CS_INSERT in the USER_IO input page
    | rings the doorbell
    v
firmware
    |
    | reads entries between CS_EXTRACT and CS_INSERT
    | executes them in order
    | writes the sync value, advances CS_EXTRACT
    v
manvil_sync_wait_cpu
    |
    | polls the sync value until it reaches the target
    v
application code
The batch used by the probe is small and self contained:

  1. ENOP              mark a point in the stream, no side effect
  2. MOVE48            load the sync object address into a register
  3. MOVE48            load the target value into a second register
  4. SYNC_SET64        write the value through the two registers
  5. ERROR_BARRIER     terminate the batch cleanly

Manvil reserves the top four registers of the CS register file for
this kind of use. The exact number of registers and the index of the
first user register are not hardcoded. They are read from the
firmware at open time as part of the CSF interface query.

## Observed behavior on the target hardware

Processor: MediaTek Mali-G615 MC6.
CSF interface version: 2.5.0.
CS register file: 96 registers, 17 scoreboards.
User register base: index 92.

The first submission of a fresh queue takes between four and seven
milliseconds. This is dominated by the kernel side scheduling work:
moving the queue from UNBOUND to RUNNABLE, assigning it a CSG slot,
and starting the command stream on the firmware. The second
submission reuses the already started queue and completes in a few
hundred microseconds in the best case, up to a few milliseconds if
the kernel scheduler happens to be rotating other groups.

Both submissions complete with no errors reported by the firmware.
The final CS_INSERT and CS_EXTRACT are equal, which means the
firmware consumed every entry that was written.

## What is not yet implemented

The submission path supports only the four commands listed above,
plus the others exposed by cmd.c that are not yet exercised. There
is no shader execution yet: the batch does not CALL any code. There
is no tiler heap in use. There is no synchronization between queues
beyond a single intra group sync object. Those features are on the
roadmap and build on the same infrastructure.

## The three discoveries that made it work

Reaching the first successful submission required fixing three
specific interface mismatches. Each was found by comparing the
Manvil implementation against the public Mesa genxml specification,
against the kernel driver source in mali-kbase-src, and against the
PanVK kbase fork. None of them was documented in the Kbase UAPI
headers directly.

### Discovery 1: the USER_IO page order

The three pages returned by CS_QUEUE_BIND through the mmap_handle
are, in order:

    offset 0x0000   doorbell hardware page
    offset 0x1000   CS_USER_INPUT   holds CS_INSERT
    offset 0x2000   CS_USER_OUTPUT  holds CS_EXTRACT

An earlier version of Manvil assumed a different order and wrote
CS_INSERT into what was actually the doorbell page. The firmware
never observed any new work, and the submissions timed out.

The correct order is documented in Mesa as comments next to the
CS_USER_IO_INPUT_CS_INSERT and CS_USER_IO_OUTPUT_CS_EXTRACT macros
in the drm-uapi header:

    #define CS_USER_IO_INPUT_CS_INSERT      0x0000 /* u64, page 1 */
    #define CS_USER_IO_OUTPUT_CS_EXTRACT    0x0000 /* u64, page 2 */

### Discovery 2: CS_INSERT and CS_EXTRACT are 64 bit

Both registers are 64 bit unsigned integers. An earlier version of
Manvil treated them as 32 bit, which worked by accident because the
values stayed small, but was not portable and did not match the
register definition in the CSF register map.

The Manvil queue module now stores the local cache of CS_INSERT as
a 64 bit value and reads CS_EXTRACT through a 64 bit pointer into
the mapped output page.

### Discovery 3: SYNC_ADD64 and SYNC_SET64 take register indexes

This was the most subtle of the three. The Data and Address fields
of SYNC_ADD64 and SYNC_SET64 are only eight bits wide each, and
they carry the index of a CS register, not a value or an address.

An earlier version embedded the sync object's GPU virtual address in
the Address field and the value in the Data field, truncating both
to 8 bits. The firmware rejected the resulting instruction with
CS_FATAL_EXCEPTION_TYPE_CS_INVALID_INSTRUCTION (0x49), and the
kernel killed the queue.

The correct pattern is:

    MOVE48 reg_addr, sync_gpu_va
    MOVE48 reg_data, target_value
    SYNC_SET64 address_reg=reg_addr, data_reg=reg_data

The same pattern applies to SYNC_ADD64. The Mesa genxml
specification for v14 describes the field widths:

    <struct name="CS SYNC_ADD64" size="2">
        <field name="Data" size="8" start="32" type="uint"/>
        <field name="Address" size="8" start="40" type="uint"/>
        ...

The index used for reg_addr and reg_data comes from the CS
register file. The firmware reports its size through
CS_GET_GLB_IFACE in the features field of the first stream
capability structure. Manvil reads that value at open time and
stores it in the public csf interface struct. On the tested device
the register file has 96 entries and the top four (indexes 92 to
95) belong to the user.

## Acknowledgments

The CSF command set is not documented in the Kbase UAPI headers.
The public reference that was used to determine the field layout of
each command is the Mesa 3D genxml specification for the Valhall
architecture, distributed with Mesa under the MIT license. The
relevant files are src/panfrost/genxml/v10.xml through v14.xml and
the builder header generated from them.

Manvil does not include any Mesa source code. The bit level layout
of each command was extracted from the genxml description, which is
public hardware documentation, and reimplemented in the Manvil
command builder. Where a layout matched what the PanVK kbase fork
also does, the fork was consulted to confirm that the interpretation
was correct.

The kernel side behavior was confirmed against the driver source in
mali-kbase-src, also under the GPL and published by Arm. The source
was read for behavior, not copied.

## What comes next

The immediate next steps in order of priority:

1. Tiler heap in actual use. The heap module is implemented and
   tested in isolation, but no submission has yet emitted HEAP_SET
   and used the heap. This is the first step towards a graphics
   pipeline.

2. A compute shader. The command set already supports CALL and the
   command builder exposes it. With a precompiled ISA Valhall shader
   and a small descriptor buffer, Manvil can issue a compute
   dispatch and read the result back through a sync.

3. KRAID integration. The Mesa KRAID compiler produces ISA Valhall
   from SPIR-V. Manvil can use it as a library to compile arbitrary
   shaders at pipeline creation time. This removes the need to ship
   precompiled shaders for every workload.

4. Vulkan ICD. Once the submission path can carry a real pipeline,
   the Vulkan ICD layer can be built on top.

## Reproducing the result

The test tool is tools/mprobe.c. To build and run it:

    gcc -Wall -Wextra -std=c11 -I src -DMANVIL_TARGET_UAPI=20 \
        -o mprobe \
        tools/mprobe.c \
        src/kernel_api/manvil_kbase.c \
        src/mem/mem.c \
        src/device/device.c \
        src/csf/group.c \
        src/csf/queue.c \
        src/csf/event.c \
        src/kcpu/kcpu.c \
        src/cmd/cmd.c \
        src/sync/sync.c \
        src/sched/sched.c \
        src/heap/heap.c

    ./mprobe

The tool prints a summary of the device capabilities, walks through
the steps of the pipeline, and reports SUCCESS if every step
completed without errors from the kernel or the firmware.
