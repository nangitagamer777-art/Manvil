# CSF Event System

How the kernel notifies userspace of CSF events and errors.

## Event object

struct kbase_csf_event {
    struct list_head callback_list;
    struct list_head error_list;
    spinlock_t lock;
};

Each context has one event object. It holds two lists: callbacks
registered by internal kernel code, and pending errors queued for the
userspace.

## Callbacks

Callbacks are registered by the kernel during initialization or by
other subsystems that need to be notified of CSF events.

typedef enum kbase_csf_event_callback_action
    kbase_csf_event_callback(void *param);

A callback returns either KEEP (stay registered) or REMOVE (remove
itself after the call).

API:
  int  kbase_csf_event_wait_add(kctx, callback, param);
  void kbase_csf_event_wait_remove(kctx, callback, param);
  void kbase_csf_event_term(kctx);

## Signal

void kbase_csf_event_signal(struct kbase_context *kctx, bool notify_gpu);

Dispatches all callbacks. If notify_gpu is true, also notifies the
firmware so it knows the event happened.

Two inline wrappers exist:
  kbase_csf_event_signal_notify_gpu(kctx)    // notify_gpu = true
  kbase_csf_event_signal_cpu_only(kctx)      // notify_gpu = false

## Error queue

Errors are accumulated in error_list as base_csf_notification structs.
They are added by the kernel when a fatal event occurs.

API:
  bool kbase_csf_event_read_error(kctx, base_csf_notification *out);
  void kbase_csf_event_add_error(kctx, kbase_csf_notification *item,
                                  base_csf_notification const *data);
  void kbase_csf_event_remove_error(kctx, kbase_csf_notification *item);
  bool kbase_csf_event_error_pending(kctx);

read_error pops one error from the list and copies it to the output
buffer. It returns true if an error was read, false if the list is
empty.

## Accumulation

Adding an error does not wake the userspace immediately. The kernel
defers the wakeup to make adding multiple errors efficient.

The userspace is expected to poll the device fd. When the kernel
decides to wake, poll returns and the userspace reads errors one by
one until the list is empty.

## Error sources

Errors are added to the list when:
  - a queue reports a fatal fault (queue->error)
  - a group reports a fatal group error (group->error_fatal)
  - a group reports a progress timeout (group->error_timeout)
  - a group reports a tiler heap out of memory (group->error_tiler_oom)

Each notification includes a type byte and a payload. The type
determines how the payload is interpreted.

## Notification payloads

For fatal errors, the payload is base_gpu_queue_group_error, which
contains:
  error_type:      one of the base_gpu_queue_group_error_type enum
  payload:         union of fatal_group or fatal_queue

For queue faults, the payload is base_gpu_queue_error_fatal_payload:
  sideband:        additional info
  status:          fatal status
  csi_index:       index of the CSI that faulted

For group faults, the payload is base_gpu_queue_group_error_fatal_payload:
  sideband:        additional info
  status:          fatal status

## Userspace side

Manvil should:
  1. Have a dedicated thread polling the device fd for readability.
  2. When wakeup occurs, call the ioctl that reads one error.
  3. Copy the error into a userspace queue.
  4. Notify application code.

The read operation is the ioctl CS_EVENT_SIGNAL (ioctl 44). Each call
returns one error or reports that the list is empty.

## Timing

Because errors are accumulated, the userspace may see multiple errors
at once. The order is preserved (the kernel uses an ordered list).

The kernel does not guarantee a minimum delay. In practice, the
userspace sees errors within a few microseconds of their generation.

## Interaction with sync objects

The event system is also used to wake up threads waiting on CQS
objects. When the firmware signals a CQS, the kernel wakes the
threads that registered a callback for the relevant event.

This is transparent to the userspace, which sees it as a poll
returning readability.
