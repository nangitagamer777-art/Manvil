# JIT Memory Reference

Notes on the JIT (just in time) memory allocator exposed through the KCPU
JIT_ALLOC and JIT_FREE commands. This is separate from shader
compilation.

## Purpose

JIT is a mechanism for GPU memory regions whose physical pages are
populated on demand. The virtual address range is reserved once, and
individual allocations within that range grow as the GPU faults on
missing pages.

This is useful for large sparse buffers and for allocations whose final
size is not known in advance.

## Initialization

struct kbase_ioctl_mem_jit_init init = {0};
init.va_pages = va_pages;
init.max_allocations = 255;
init.trim_level = trim_level;
init.group_id = group_id;
init.phys_pages = va_pages;
ioctl(fd, KBASE_IOCTL_MEM_JIT_INIT, &init);

va_pages:      total virtual address space reserved for JIT
phys_pages:    maximum physical pages the JIT region can occupy
max_allocations: maximum concurrent JIT allocations, up to 255
trim_level:    percentage of physical pages to release when freeing,
               0 means release nothing, 100 means release everything
group_id:      physical memory group (0-15)

## Allocation

Each JIT allocation has:

id              unique identifier (u8), cannot be zero
gpu_alloc_addr  user memory where the kernel writes the resulting GPU VA
va_pages        minimum virtual pages for the allocation
commit_pages    minimum physical pages backing the allocation
extension       granularity of physical growth on each fault
bin_id          bin identifier for allocation grouping
usage_id        hint for reuse of previous allocations

The allocation is submitted through the KCPU JIT_ALLOC command. The
kernel writes the resulting GPU VA to gpu_alloc_addr.

## Free

Freeing requires only the allocation id. Physical pages are released
according to trim_level. The virtual address range is preserved for
reuse.

## Observed behavior

From the exploit test run:

The allocator returns virtual addresses within the initialized range.
When a JIT allocation grows, adjacent regions are not disturbed unless
the allocator decides to reuse freed space. The trim level affects how
much memory is returned to the system on free.

The exploit targets a specific interaction between JIT and page tables
during growth. Manvil does not perform this interaction and uses JIT
only for legitimate memory management.

## Manvil integration

The memory module should offer:

manvil_jit_init(device, va_pages, phys_pages, trim_level, group_id)
manvil_jit_alloc(device, va_pages, commit_pages, extension, bin_id,
                 usage_id, gpu_result_addr)
manvil_jit_free(device, jit_id)

The kernel API exposes the underlying commands:

manvil_kcpu_jit_alloc(...)
manvil_kcpu_jit_free(...)

The memory module wraps them with allocation id management and result
synchronization.
