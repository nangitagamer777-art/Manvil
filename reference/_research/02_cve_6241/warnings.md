# Warnings and Non-Reusable Content

The CVE-2023-6241 disclosure package contains both interface definitions
and exploit logic. Only the interface definitions are usable for Manvil.
This document records what is deliberately excluded and why.

## Excluded files

### firmware_offsets.h

Contains hardcoded kernel symbol offsets specific to the Pixel 8 running
build UD1A.231105.004:

  AVC_DENY_2311            0x806b50
  SEL_READ_ENFORCE_2311    0x818714
  INIT_CRED_2311           0x271bfa8
  COMMIT_CREDS_2311        0x167b40
  ADD_COMMIT_2311          0x912d0108
  ADD_INIT_2311            0x913ea000

These offsets change with every kernel build and are only useful for the
exploit. They have no relation to the CSF protocol.

### mem_read_write.c / mem_read_write.h (partial)

The file contains two categories of code:

Reusable reference:
  The ARM64 instruction encoding primitives (write_adrp and the ADRP
  instruction layout) are generic and could be useful if Manvil ever
  needs to generate ARM64 code at runtime, for example for a firmware
  side JIT.

Not reusable:
  fixup_root_shell builds a privilege escalation payload.
  write_func, set_addr_lv3, compute_pt_index manipulate page tables.
  The OpenCL kernel rw_mem performs unchecked GPU memory access using
  user-controlled virtual addresses.

### mali_jit_csf.c (partial)

Reusable reference:
  The setup, queue register, queue bind, KCPU enqueue, and JIT allocate
  patterns are correct uses of the UAPI and are reproduced in the
  patterns.md document.

Not reusable:
  find_pgd walks page tables.
  write_shellcode rewrites kernel page table entries.
  The main function orchestrates the exploit and depends on
  firmware_offsets.h.

### log_utils.h

A one-line logging macro. Not relevant.

### CL

A symlink to /usr/include/CL. Belongs to the exploit's OpenCL build.

## Design principles for Manvil

The following principles are derived from studying the exploit and are
adopted as Manvil policy:

1. Virtual addresses submitted by the application to the kernel or
   firmware are validated before use. No unchecked dereference.

2. KCPU enqueue busy waits carry a timeout. An unbounded wait can hang
   the process if the firmware or kernel does not respond.

3. Resource lifecycles are explicit. Every allocation has a matching
   free, and every group and queue has a matching terminate.

4. Kernel internal structures are never the target of writes. Manvil
   operates only on memory that it owns or has legally mapped.

5. Public UAPI headers are the sole source of interface definitions.
   Kernel symbol offsets and other build-specific values are not used.

## What the exploit demonstrates as a positive

Beyond the vulnerability itself, the exploit is a working example of the
full CSF initialization and submission sequence. This is valuable
because the CSF documentation published by ARM does not include a
complete example.

The extractable lessons are:

  The exact ioctl order for handshake.
  The exact struct initialization for memory allocation.
  The exact bind and mmap flow for USER pages.
  The exact KCPU enqueue flow with shared memory result.
  The exact JIT initialization and allocation flow.

These lessons are captured in patterns.md.
