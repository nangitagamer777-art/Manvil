# CSF Scheduler

The CSF scheduler distributes GPU execution time across queue groups from
all contexts that are using the GPU. It operates on a periodic tick and
can be reconfigured at runtime.

## Scheduling model

The scheduler maintains a list of runnable groups from all contexts. On
each tick, it selects a subset of groups to place on the available CSG
slots. Groups that do not fit are deferred to the next tick.

Selection order is by priority level:

  realtime    runs before any other level
  high
  medium
  low

Within the same priority level, fairness is achieved with a scan
sequence number. Each group has a scan_seq_num. The scheduler scans in
ascending order and assigns slots to the lowest values. After a group
runs, its scan_seq_num moves to the end of the range.

## Tick and tock

Two scheduling phases operate in parallel:

tick    triggered by a high resolution timer at a configurable period
        (csg_scheduling_period_ms). Runs the synchronous scheduling
        operation. All state transitions that require the GPU to be
        briefly quiesced happen here.

tock    triggered asynchronously when a group becomes runnable between
        ticks (for example, after a kick). Runs an incremental update
        without waiting for the next tick.

Both phases hold the scheduler lock, so they do not run concurrently.

## Idle groups

A group whose queues are all idle or blocked on a sync object is moved
to the idle_wait_groups list. These groups are appended to the end of
the schedule list so that non-idle groups get preference.

When a sync object becomes signaled, the group is moved back to the
runnable list. The sync_update_wq work queue processes these events.

## CSG slots

The scheduler maintains an array of CSG slot descriptors, one per
hardware slot. Each slot has a state (READY, RUNNING, SUSPENDED, etc.)
and a pointer to the resident group.

A slot transitions through:

READY -> READY2RUN -> RUNNING -> DOWN2STOP -> STOPPED -> READY

The kernel writes CSG_REQ_STATE_START or CSG_REQ_STATE_SUSPEND to the
slot and waits for the corresponding CSG_ACK_STATE from the firmware.
If the acknowledgement does not arrive within the timeout, the slot
enters a TIMEDOUT state and the kernel takes corrective action.

## Time slicing

When there are more runnable groups than CSG slots, the scheduler
suspends some groups and resumes others. Each group has a suspend
buffer (normal_suspend_buf) that holds its context across the switch.

The period of the rotation is the tick period. A group may wait several
ticks before being scheduled again if many other groups are runnable.

Implication for Manvil: user visible operations can be delayed by the
scheduling period multiplied by the number of competing groups. A
five second timeout is safe. A ten millisecond timeout is not.

## Priority encoding

Two encodings exist and must not be confused:

UAPI value (base_queue_group_priority):
  HIGH      0
  MEDIUM    1
  LOW       2
  REALTIME  3

Kernel internal value (kbase_queue_group_priority):
  REALTIME  0
  HIGH      1
  MEDIUM    2
  LOW       3

The kernel maps between them at the ioctl boundary. Manvil uses the
UAPI values.

## Address spaces

The scheduler tracks how many address spaces are in use. Each context
with runnable groups occupies one address space slot. The hardware
supports a fixed number, typically four to eight. When more contexts
are active than address spaces, the scheduler must rotate contexts on
and off the hardware, with the same time slicing effect.

## Protection mode

Groups can request protected mode execution for DRM content. Only one
group at a time can be in protected mode. The scheduler tracks the
active protected mode group and prioritizes its scheduling.

Manvil does not use protected mode.

## Firmware ping

When the GPU is idle and only one slot is active, the kernel sends a
periodic ping to the firmware to verify that it is still responsive. If
the ping is not acknowledged within the firmware timeout, the kernel
initiates a GPU reset.

Implication for Manvil: long running shaders that do not respond to
ping are treated as firmware crashes. This is unlikely to affect
compute work, but it is relevant for very long shader executions.
