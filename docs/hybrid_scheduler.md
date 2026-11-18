# HybridFairScheduler (initial implementation)

Plugin: `block_plugin_engine/HybridFairScheduler` (`bpe::BPE_HybridFairScheduler`).
Independent of `BPE_FairScheduler`, implements `bpe::IBlockScheduler`.

- TIMER blocks continue to use the nominal period/offset and `execute()` (or
  `ITimerBlockWithInfo::execute(info)`).
- EVENT blocks are grouped by `event_tag`. Each group has one expected period,
  one offset, and a pipeline of consumers sorted by unique explicit `order`.
  Each event window permits at most one invocation of the complete pipeline.
- Event producers obtain a move-only `EventService::Publisher` from the
  injected `EventService`, and call `signal()`. Only one live publisher per
  tag is permitted. Do not write directly to the eventfd.
- Events do not transport data. Plugins read the latest Blackboard contents.
- The worker has one `timerfd`, one `epoll` instance and one stop `eventfd`.
  Event fd readiness marks an opportunity available; nominal times break ties.
  An empty window is skipped (its planning index advances). All event
  consumers execute consecutively in configured order.
- The nominal period is never changed by phase correction. A bounded correction
  (1/8 of the phase error, capped at T/8 or a smaller configured bound) shifts
  only subsequent window boundaries.

Example block instances (inside the existing `blocks:` configuration):

```yaml
- name: input_processor
  type: my_package/InputProcessor
  execution:
    policy: event
    event_tag: sensor_ready
    period: 20ms
    order: 0
- name: state_estimator
  type: my_package/StateEstimator
  execution:
    policy: event
    event_tag: sensor_ready
    period: 20ms
    order: 1
- name: periodic_monitor
  type: my_package/Monitor
  execution:
    policy: timer
    period: 10ms
```

To inject the event service into a producer, its plugin implements
`bpe::IEventServiceUser::set_event_service(EventService&)` and owns a publisher
returned by `service.publisher("sensor_ready")` (the handle releases the unique
publisher claim through RAII). Configuration must precede use.

## Current limitations (important before integration)

- A Linux `eventfd` accumulates counts, not per-signal timestamps. Only the
  timestamp of the latest call to `signal()` is kept. If the scheduler is busy
  across several windows, the exact distribution of multiple signals across
  those windows **cannot** be reconstructed. Notifications are coalesced and
  may result in a missed earlier window. Correct retrospective attribution
  would require an additional bounded per-window bookkeeping mechanism.
- As in the current FairScheduler code, timer opportunities are advanced
  one-by-one. A dedicated global *whole-cycle skip* algorithm is not present
  in the supplied implementation and has **not** been invented here.
- `stop()` waits for the currently running callback or pipeline to finish.
- The optional RT priority follows the same basic `SCHED_FIFO` policy as
  FairScheduler; the event/timer wake path is `epoll`/`timerfd`, not its
  spin-before-deadline loop. Temporal precision has not been evaluated.
- No tests were created, modified or run in this iteration.
