# Firmware Interface

Notes on how the kernel loads and manages the CSF firmware.

## Firmware image

The firmware is a binary image made of entries. Each entry describes a
section of the firmware with a name, permissions, and content.

CSF_FIRMWARE_ENTRY flags:

  CSF_FIRMWARE_ENTRY_READ           bit 0
  CSF_FIRMWARE_ENTRY_WRITE          bit 1
  CSF_FIRMWARE_ENTRY_EXECUTE        bit 2
  CSF_FIRMWARE_ENTRY_CACHE_MODE     bits 3-4
  CSF_FIRMWARE_ENTRY_PROTECTED      bit 5
  CSF_FIRMWARE_ENTRY_SHARED         bit 30
  CSF_FIRMWARE_ENTRY_ZERO           bit 31

CSF_FIRMWARE_ENTRY_ZERO means the section is zero initialized before
loading. CSF_FIRMWARE_ENTRY_SHARED means the section is shared between
host and firmware.

## Firmware interfaces

Each entry in the firmware image becomes a kbase_csf_firmware_interface
in the kernel. Interfaces may share physical pages if their contents
match (reuse_pages).

Interfaces use either 4 KiB or 2 MiB pages. The 2 MiB pages reduce TLB
pressure on the MCU.

The shared interface is the memory area that both host and firmware
read and write. It contains the global control block, group control
blocks, and stream control blocks.

## Global interface

At boot, the firmware writes the global interface structure into the
shared area. The kernel parses it and stores the result in
kbase_csf_device.global_iface.

The structure describes the CSGs, CSs, and their features. It is
exposed to userspace through KBASE_IOCTL_CS_GET_GLB_IFACE.

Fields of the userspace visible structure:

  glb_version         version of the global interface
  features            feature bitmask
  group_num           number of CSG slots supported
  total_stream_num    total number of CSIs across all groups
  prfcnt_size         size of the performance counter data
  instr_features      instrumentation features

## Firmware state

The kernel tracks several state flags:

  firmware_inited               cold boot completed
  firmware_reloaded             reload after reset completed
  firmware_reload_needed        partial reload pending
  firmware_full_reload_needed   full reload required
  firmware_hctl_core_pwr        host controls core power

The firmware_reload_work work item performs the reload. The
fw_error_work work item handles firmware internal errors.

## Firmware ping

The kernel sends a periodic ping to the firmware (GLB_REQ_PING) when
the GPU is idle and only one CSG slot is active. The firmware must
respond within the firmware timeout. Failure to respond triggers a GPU
reset.

This mechanism detects silent firmware crashes that would otherwise go
unnoticed during idle periods.

## Reset and recovery

A GPU reset is triggered by:
  - fatal faults reported by the firmware
  - ping timeout
  - explicit request from other parts of the kernel

The reset sequence:

  1. Prepare the reset (PREPARED state)
  2. Commit the reset (COMMITTED state)
  3. Perform the reset (HAPPENING state)
  4. Reload the firmware
  5. Signal completion

During the reset, all CSF activity is quiesced and the semaphore
kbase_csf_reset_gpu.sem blocks external access to the hardware.

Implication for Manvil: a reset destroys all client state. The
userspace must detect the reset through ioctl failures and recreate
everything. There is no way to recover individual queues or groups
from a reset.

## Userspace interaction

The userspace does not interact with the firmware directly. All
firmware operations go through kernel ioctls. The kernel translates
userspace requests into firmware commands and translates firmware
responses into notifications.

The userspace learns about firmware state through:

  - ioctl return codes
  - notifications consumed through the CSF event mechanism
  - the timeline stream (for diagnostics only)

There is no direct memory access to the firmware address space from
the userspace.
