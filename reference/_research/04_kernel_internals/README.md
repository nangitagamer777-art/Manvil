# Kernel Internals Research

Analysis of the CSF kernel driver headers from mali-kbase-src. These are
not UAPI headers. They describe how the kernel implements the CSF
interface and how it interacts with the firmware.

## Files analyzed

mali_kbase_csf_registers.h        1524 lines   CSF register definitions
mali_kbase_csf_defs.h             1356 lines   Kernel side object model
mali_kbase_csf.h                   468 lines   Internal CSF API
mali_kbase_csf_firmware.h          808 lines   Firmware interface
mali_kbase_csf_kcpu.h              354 lines   KCPU model
mali_kbase_csf_tiler_heap.h        115 lines   Tiler heap API
mali_kbase_csf_tiler_heap_def.h    114 lines   Tiler heap structures
mali_kbase_csf_heap_context_alloc.h 75 lines   Heap context allocator
mali_kbase_csf_event.h             171 lines   Event system
mali_kbase_csf_timeout.h            66 lines   Progress timeout
mali_kbase_csf_protected_memory.h   75 lines   Protected memory
mali_kbase_csf_trace_buffer.h      179 lines   Firmware trace buffers
mali_kbase_csf_scheduler.h         637 lines   Scheduler API

Total: about 5942 lines of kernel headers analyzed.

## Documents

registers_map.md        Complete CSF register map
objects_model.md        Kernel object model
scheduling.md           Scheduler behavior and timing
power_and_timers.md     IPA, progress timer, power-off timer
firmware_interface.md   Firmware loading and control
kcpu_model.md           KCPU command queue
tiler_heap_model.md     Tiler heap and chunks
event_system.md         Error and event notification

## Value

High. Together these headers explain the behavior that userspace
observes: why operations block, why timeouts occur, what the kernel
does with each ioctl, and how scheduling affects latency.

## What is not covered

The Mali shader ISA is not public and is not described in these
headers. Shader compilation to Valhall ISA requires an external
component (Mesa KRAID or similar).

The exact format of shader blobs accepted by the firmware is not
documented. Reverse engineering or reuse of existing tooling is
required.

The internal layout of the firmware heap context is opaque. Only the
chunk header format (documented here) is available.
