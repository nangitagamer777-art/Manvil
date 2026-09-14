# Additional Patterns

Patterns extracted from the additional CVE disclosures that are not
already covered in the CVE-2023-6241 research. These are additions to
the kernel API and sync module designs.

## API version encoding

Manvil should represent the API version as a single u32 with this
layout:

bits 31-20  major
bits 19-8   minor
bits  7-0   patch

Helper macros:

#define MANVIL_API_VERSION(major, minor) \
    ((((major) & 0xFFF) << 20) | (((minor) & 0xFFF) << 8))

#define MANVIL_API_MAJ(v) ((v >> 20) & 0xFFF)
#define MANVIL_API_MIN(v) ((v >>  8) & 0xFFF)

Usage:

  At device open, read the kernel's reported version.
  Store it as a u32 in the device structure.
  Compare against known version constants when selecting ABI variants.

## KCPU synchronization timeouts

The KCPU timer operates on 256 ms buckets. A command that depends on
the timer, such as a FENCE_SIGNAL scheduled after a blocked CQS_WAIT,
will not fire until the next bucket boundary.

Manvil should use these defaults:

  Fence and wait timeouts         5 seconds
  CQS wait poll interval          100 milliseconds
  CQS wait total timeout          10 seconds
  KCPU enqueue result timeout     5 seconds

The enqueue result timeout applies to commands that write a result back
to shared memory. The exploit used an unbounded busy wait, which risks
hanging the process forever if the firmware or kernel stalls.

## CS_CPU_QUEUE_DUMP usage

The ioctl takes a user buffer and size. The kernel writes the current
state of the KCPU queue into the buffer. The buffer should be at least
0xF000 bytes.

Manvil exposes this operation in the diagnostic tools only. It is not
part of the normal submission path.

Suggested API:

int manvil_kcpu_dump(manvil_device *dev, uint8_t queue_id,
                     void *buffer, size_t size);

The dump is a snapshot of the queue state and is safe to call while the
queue is idle. It should not be called concurrently with operations
that modify the queue.

## Timeline stream packet parsing

If Manvil ever exposes the timeline stream for diagnostics, the packet
format is:

  offset 0  packet header  8 bytes
  offset 8  message id     u32
  offset 12 timestamp      u64
  offset 20 payload        depends on message id

Packet header fields:

  bits  0-3   stream id
  bits  4-7   packet class
  bits  8-11  packet type
  bits 12-15  reserved
  bits 16-31  payload length (varies)

The type values observed:

  2  SUMMARY  contains concatenated messages

The class values observed:

  0  OBJ      object lifecycle messages

A parser should:

  1. Read the header and validate the length.
  2. Extract the message id.
  3. Look up the payload format for that id.
  4. Extract each field according to the format string.

Manvil does not need to expose this in the first version. A minimal
parser can be added later if diagnostics are required.

## Context create flags summary

Consolidated list of context create flags observed across all
disclosures:

  bit 0  BASE_CONTEXT_CCTX_EMBEDDED
  bit 1  BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED
  bit 2  BASE_CONTEXT_CSF_EVENT_THREAD
  bit 3  BASEP_CONTEXT_MMU_GROUP_ID_SHIFT (bit 0)
  bit 4  BASEP_CONTEXT_MMU_GROUP_ID_SHIFT (bit 1) AND
         BASE_CONTEXT_CREATE_FLAG_MONITOR (special case)
  bit 5  BASEP_CONTEXT_MMU_GROUP_ID_SHIFT (bit 2)
  bit 6  BASEP_CONTEXT_MMU_GROUP_ID_SHIFT (bit 3)

Bit 4 is shared between the MMU group ID field and the monitor flag.
The monitor flag is only set by the kernel when the process is granted
monitor privileges, and only in specific kernel versions. Manvil does
not use the monitor flag.

For a normal CSF context, Manvil should set:

  BASE_CONTEXT_CSF_EVENT_THREAD (bit 2)

and leave the MMU group ID at zero unless the application requests a
specific group.

## VERSION_CHECK ioctl selection

Probe order:

  1. Try ioctl 52 with the CSF version struct.
     If it succeeds, the kernel uses the CSF ABI.
     Read the returned major and minor.
  2. If ioctl 52 fails with EINVAL or ENOTTY,
     try ioctl 0 with the same struct.
     If it succeeds, the kernel uses the JM ABI.
  3. If both fail, the device does not support Kbase
     or the process lacks permission.

Manvil only proceeds with the CSF path. If the kernel is JM only,
device initialization fails with a clear error.

## User page ownership

On kernels that support CSG_CS_USER_PAGE_ALLOCATION (r53 and later),
the user IO pages belong to the queue group. Two consequences:

  Unbinding a queue and rebinding it to the same group is cheap.
  The user IO pages remain valid until the group is terminated.

Manvil should query this capability during device initialization and
set a flag in the device structure. Code paths that free or reuse user
IO pages should consult the flag.

On kernels without this feature, each bind allocates fresh pages and
each unbind frees them. Manvil must free the pages explicitly in that
case.
