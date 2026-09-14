# Miscellaneous Notes

## Detection and compatibility

The exploit targets a Pixel 8 with UAPI 1.14. The target device for
Manvil is a MediaTek Dimensity with UAPI 1.20. The delta between 1.14
and 1.20 is not covered by this package.

Additional CVE disclosures or kernel source tree dumps will be needed
to obtain headers newer than 1.14. The general shape of the protocol is
stable across minor versions. New fields are added at the end of input
structs to preserve backward compatibility.

## What was not extracted

The shellcode payload, page table manipulation, and kernel symbol
offsets are excluded. They serve only the exploit.

## Files to consult next

From the mali-kbase-src directory (the kernel driver source tree):

  mali_kbase/csf/mali_kbase_csf_registers.h
    Register definitions for the CSF hardware and firmware interface.
    These are not UAPI but they define the layout of the USER page and
    the ring buffer entries.

  mali_kbase/csf/mali_kbase_csf_defs.h
    Internal structures used by the kernel when handling CSF ioctls.
    Useful to understand exactly what the kernel does with each field.

  mali_kbase/csf/mali_kbase_csf_firmware.h
    Firmware loading, boot, and control interface.

  mali_kbase/csf/mali_kbase_csf_kcpu.h
    KCPU queue implementation details.

  mali_kbase/csf/mali_kbase_csf_tiler_heap.h
    Tiler heap kernel side implementation. Confirms field semantics.

  mali_kbase/csf/mali_kbase_csf_tiler_heap_def.h
    Additional tiler heap definitions.

These files live in the mali_kbase source tree, not the UAPI tree, and
are part of the GPL kernel driver. They are documentation of the kernel
side behavior and are the natural next step for Manvil research.

## Version tracking

Manvil aims to support at least UAPI 1.14 and 1.20. The version
negotiation happens at VERSION_CHECK. Struct layouts that changed between
these versions must be handled by the ABI layer with explicit per
version definitions.

Known version-dependent items:

  CS_QUEUE_GROUP_CREATE has a csi_handlers field from 1.12 onward.
  CS_TILER_HEAP_INIT has a buf_desc_va field from 1.14 onward.
  READ_USER_PAGE exists from 1.13 onward.
  Sync32 and Sync64 layouts are explicit from an early version but
  documented in full only in recent headers.

When both ends support a feature, use the newest. Otherwise fall back to
the version that the kernel supports.
