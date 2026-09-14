# Power Management and Timers

Notes on the timers and power management mechanisms used by CSF.

## Progress timer

GLB_PROGRESS_TIMER in the global input block sets the maximum number of
GPU cycles that a group may run without forward progress before the
kernel terminates it.

Unit: 1024 GPU cycles (GLB_PROGRESS_TIMER_TIMEOUT_SCALE).
Maximum: GLB_PROGRESS_TIMER_TIMEOUT_MAX.

The value is chosen by the kernel as a balance between detecting real
hangs quickly and allowing long shader executions to complete.

When the timer expires for a group, the firmware raises
CSG_REQ_PROGRESS_TIMER_EVENT and the kernel delivers a
BASE_GPU_QUEUE_GROUP_ERROR_TIMEOUT notification to the userspace.

Implication for Manvil: very long shader executions can trigger this
path. Solutions include splitting work into smaller submissions, or
ensuring progress is visible to the firmware at regular intervals.

## Power off timer

GLB_PWROFF_TIMER in the global input block controls how long the CSF
waits before powering down shader cores that are idle.

Default: 800 microseconds (DEFAULT_GLB_PWROFF_TIMEOUT_US).

This value is aggressive. A short pause between submissions is often
enough to trigger a power down. The MCU takes time to bring cores back
up when work resumes.

Implication for Manvil: the first submission after a pause is slower
than subsequent ones. Benchmarks should include a warm up run.

If the host driver takes direct control of core power, this timer can
be disabled (DISABLE_GLB_PWROFF_TIMER). Manvil relies on the kernel
default.

## Idle timer

The scheduler has a configurable idle hysteresis (gpu_idle_hysteresis_ms
via sysfs). If the GPU is idle for longer than this period, the
scheduler transitions to a low power state.

The GPU can also transition to sleep mode if the hardware supports it.
This is deeper than the regular low power state.

Implication for Manvil: after a long idle, the first submission may
take longer due to the transition back to active state.

## IPA control

IPA (Intelligent Power Allocation) reads performance counters from four
blocks (CSHW, MEMSYS, TILER, SHADER) and adjusts GPU frequency.

There are up to 32 counters split evenly across the four blocks. A
session can subscribe to a subset of counters.

The GPU_ACTIVE counter is at index 4 of the CSHW block.

Manvil does not interact with IPA control directly. The kernel manages
it.

## Timeouts

Three kernel timeouts exist for CSF operation:

  CSF_FIRMWARE_TIMEOUT     response from the firmware
  CSF_PM_TIMEOUT           power management transitions
  CSF_GPU_RESET_TIMEOUT    GPU reset completion

The values are scaled by the lowest expected GPU frequency. They are
not configurable from userspace.

The firmware timeout is the most important for Manvil. It bounds how
long any single ioctl that talks to the firmware can take before the
kernel assumes a crash.

## Firmware reload

When a reset is triggered, the kernel reloads the firmware. Two types
exist:

  partial reload    The MCU is rebooted, GPU is preserved. Faster.
  full reload       The entire firmware image is reloaded. Slower.

A full reload is required if the partial reload fails.

Implication for Manvil: a full reload destroys all client state. The
userspace must recreate every queue and group after such an event. The
kernel signals this through subsequent ioctl failures (typically -EIO
or similar).
