# Kernel Object Model

The kernel structures that represent CSF state. These are not visible to
userspace, but they explain the behavior that userspace observes.

## kbase_csf_device

The global CSF device, shared by all contexts.

Fields of interest:

  mcu_mmu                MMU for the MCU firmware address space
  shared_interface       Host and firmware shared memory area
  global_iface           Parsed global interface from firmware
  scheduler              Global scheduler instance
  reset                  GPU reset handler
  progress_timeout       Global progress timeout value
  ipa_control            Power management
  firmware_inited        Cold boot completed
  firmware_reloaded      Reload after reset completed
  firmware_reload_needed Reload is pending
  fw_error_work          Work item for firmware internal error
  fw_timeout_ms          Timeout for any request to the firmware

The firmware is booted once and can be reloaded after a reset. A full
reload is more expensive than a partial one.

## kbase_csf_context

Per-process CSF state.

Fields of interest:

  event_pages_head       Pages for synchronization objects
  cookies[16]            USER IO page handles in use
  user_pages_info[16]    Queue pointer for each cookie
  queue_groups[256]      Array of queue groups
  queue_list             List of registered queues
  kcpu_queues            KCPU queue state
  event                  Callback and error lists
  tiler_heaps            Tiler heap state
  user_reg_vma           Mapping of the USER register page
  sched                  Scheduler state for this context
  pending_submission_work Pending kicks awaiting processing

The 16 cookies limit the number of queues that can be bound
simultaneously within one context. Each bind consumes one cookie.

## kbase_queue

One GPU command queue.

Fields of interest:

  kctx                   Owning context
  reg                    VA region for the user IO pages
  phys[2]                Physical pages for input and output
  user_io_addr           Kernel mapping of the pages
  handle                 Value returned to userspace for mmap
  doorbell_nr            Index of the doorbell for this queue
  db_file_offset         File offset for userspace mmap
  group                  Group this queue is bound to
  queue_reg              VA region for the ring buffer
  base_addr              Base address of the ring buffer
  size                   Size of the ring buffer
  priority               Priority within the group
  csi_index              Assigned CSI interface
  bind_state             UNBOUND, BIND_IN_PROGRESS, BOUND
  enabled                Whether the CS is running
  status_wait            Cached CS_STATUS_WAIT
  sync_ptr               Cached sync object pointer
  sync_value             Cached sync test value
  blocked_reason         Reason the queue is blocked
  cs_fatal               Cached CS_FATAL value
  cs_fatal_info          Additional fatal information
  error                  Notification for fatal events
  pending                New work submitted

Only two physical pages are allocated. The doorbell is a hardware
register page, not a memory page. The bind ioctl returns an mmap handle
for three pages: input, output, and a virtual mapping of the doorbell.

## kbase_queue_group

One GPU command queue group.

Fields of interest:

  normal_suspend_buf     Context save area for normal mode
  protected_suspend_buf  Context save area for protected mode
  handle                 Handle returned to userspace
  csg_nr                 Currently assigned CSG slot, or -1
  priority               Priority level
  tiler_max              Max tiler endpoints
  fragment_max           Max fragment endpoints
  compute_max            Max compute endpoints
  tiler_mask             Allowed tiler endpoints
  fragment_mask          Allowed fragment endpoints
  compute_mask           Allowed compute endpoints
  group_uid              Unique identifier
  run_state              Current state
  prepared_seq_num       Position for scheduling
  scan_seq_num           Position for fairness
  bound_queues           Array of bound queues
  error_fatal            Notification for fatal group errors
  error_timeout          Notification for timeout errors
  error_tiler_oom        Notification for tiler OOM errors

Each error type has its own notification slot. A group can accumulate
multiple errors before the userspace reads them.

## kbase_csf_scheduler (global)

Fields of interest:

  state                  Operational state
  doorbell_inuse_bitmap  Doorbells in use
  csg_inuse_bitmap       CSG slots in use
  csg_slots              Array of CSG slot descriptors
  runnable_kctxs         Contexts with runnable groups
  groups_to_schedule     Groups prepared for the current tick
  idle_groups_to_schedule Idle groups deferred to the end
  csg_scheduling_period_ms Period of the tick timer
  tick_timer             High resolution timer
  tick_work              Synchronous scheduling work
  tock_work              Asynchronous scheduling work
  ping_work              Firmware health check work
  top_ctx, top_grp       Highest priority group of the last tick

The scheduler operates on a periodic tick, configurable in milliseconds.
Between ticks, asynchronous additions are handled by tock_work.

## kbase_csf_scheduler_context (per-context)

Fields of interest:

  runnable_groups[4]     Lists by priority level
  num_runnable_grps      Total runnable groups
  idle_wait_groups       Groups blocked on sync
  sync_update_wq         Work queue for sync updates
  sync_update_work       Work item for sync updates

Priority levels are ordered realtime, high, medium, low. A group in a
higher priority level runs before any group in a lower level.

## kbase_csf_csg_slot

Descriptor of a hardware CSG slot.

Fields of interest:

  resident_group         Group currently on this slot, or NULL
  state                  Slot state per enum kbase_csf_csg_slot_state
  trigger_jiffies        Time of last state change
  priority               Dynamic priority assigned by scheduler

## State machines

### Queue bind state

UNBOUND -> BIND_IN_PROGRESS -> BOUND
BOUND -> UNBOUND on unbind

### Group state

INACTIVE -> RUNNABLE -> IDLE -> SUSPENDED -> ...
RUNNABLE -> FAULT_EVICTED -> TERMINATED

### CSG slot state

READY -> READY2RUN -> RUNNING -> DOWN2STOP -> STOPPED -> READY

### GPU reset state

NOT_PENDING -> PREPARED -> COMMITTED -> HAPPENING
                        -> COMMITTED_SILENT -> HAPPENING
HAPPENING -> FAILED (only on error)

### Scheduler state

SCHED_BUSY, SCHED_INACTIVE, SCHED_SUSPENDED, SCHED_SLEEPING

## Limits

KBASEP_MAX_KCPU_QUEUES      256
MAX_QUEUE_GROUP_NUM         256
MAX_TILER_HEAPS             128
User IO page cookies        16

The practical limits are lower because of hardware constraints. The
hardware supports a fixed number of CSG slots, typically 8 or 16.
