# Device Handshake

The sequence to establish a working connection to the Kbase kernel driver.

## Step 1: open

Open /dev/mali0. Nodes from /dev/mali0 through /dev/mali7 may exist. Only
one needs to be opened for a single context.

## Step 2: version check

KBASE_IOCTL_VERSION_CHECK is ioctl 52 on CSF builds. The struct is:

__u16 major
__u16 minor

The client fills in the version it supports, the kernel fills in the
version it supports. Mismatches are reported through the return value.

Older kernels used ioctl 0 for VERSION_CHECK. A driver that wants to
support both should probe 52 first and fall back to 0.

## Step 3: set flags

KBASE_IOCTL_SET_FLAGS is ioctl 1. The struct carries a single __u32
create_flags field. Standard flags come from base_context_create_flags.

## Step 4: mmap tracking page

The kernel exposes a tracking page at handle 3 << PAGE_SHIFT. It is
obtained by calling mmap on the device fd with this handle as the offset.
Any subsequent ioctls that return GPU VAs use cookies relative to this
tracking page.

## Step 5: read GPU properties

KBASE_IOCTL_GET_GPUPROPS is ioctl 3. Call it first with size 0 to learn
the required buffer size, then with a real buffer.

The properties include the GPU model, capabilities, memory sizes, and the
CSF global interface parameters when applicable.

## Step 6: initialize EXEC_VA zone

KBASE_IOCTL_MEM_EXEC_INIT is ioctl 38. It reserves a range of GPU virtual
address space for executable allocations.

## Step 7: CSF global interface

KBASE_IOCTL_CS_GET_GLB_IFACE is ioctl 51. It reports how many CSG slots
and CSIs the firmware supports, along with CSF feature bits.

## Step 8: verify

At this point the context is fully initialized and can start creating
memory allocations, groups, and queues.
