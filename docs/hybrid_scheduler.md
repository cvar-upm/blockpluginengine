# HybridFairScheduler — scheduler-owned pipelines

Plugin: `block_plugin_engine/HybridFairScheduler` (`bpe::BPE_HybridFairScheduler`).

The scheduling unit is a **pipeline**, not a block. Each pipeline has a
fixed ordered sequence of `ITimerBlock` instances, a nominal period, an
optional offset, and a stable registration-order tie-breaker. A one-block
pipeline is the ordinary single-block case. The scheduler executes the
complete pipeline without interleaving another pipeline in the same worker.

## Configuration

Pipelines are configured **inside the scheduler's YAML**, independently of
`blocks:` and without declaring a producer:

```yaml
scheduler:
  type: block_plugin_engine/HybridFairScheduler
  realtime: {enabled: false} # use an appropriate SCHED_FIFO configuration in deployment
  pipelines:
    - name: estimation
      policy: timer
      period: 20ms
      blocks: [sensor_read, filter, estimate]
    - name: perception
      policy: event
      period: 40ms
      signal: camera_ready
      blocks: [preprocess, detect, publish]

blocks:
  - {name: sensor_read, type: my_pkg/SensorRead}
  - {name: filter, type: my_pkg/Filter}
  - {name: estimate, type: my_pkg/Estimate}
  - {name: preprocess, type: my_pkg/Preprocess}
  - {name: detect, type: my_pkg/Detect}
  - {name: publish, type: my_pkg/Publish}
```

Blocks referenced by a pipeline must implement `ITimerBlock`. The manager
loads them normally and resolves each reference via `IBlockScheduler::bind_block`.
A missing or incompatible reference causes `start()` to fail before the
scheduler worker is launched. Pipeline names and signal names must each be
unique inside one scheduler. Multiple producers may obtain the same signal.

The former per-block `execution: {policy: event, event_tag: ..., order: ...}`
format is **removed**. Per-block `execution: {policy: timer, period: ...}`
remains available for compatibility as a single-block timer pipeline.

## Pull-based producer interface

A block that emits notifications optionally implements `IEventSignalUser`:

```cpp
#include "block_plugin_engine/event_signal.hpp"

class CameraInput : public bpe::IBlockPlugin, public bpe::IEventSignalUser {
public:
  void set_event_signal_provider(bpe::IEventSignalProvider& provider) override {
    provider_ = &provider;
  }

  bpe::BlockResult configure(const bpe::BlockConfig&) override {
    signal_ = provider_ ? provider_->get_signal("camera_ready") : nullptr;
    return signal_ ? bpe::BlockResult::SUCCESS : bpe::BlockResult::FAILURE;
  }

  void on_image() {
    // Commit the newest image to the blackboard first.
    (void)signal_->signal();
  }

private:
  bpe::IEventSignalProvider* provider_{nullptr};
  std::shared_ptr<bpe::IEventSignal> signal_;
};
```

Look up signals in `configure()`, not in the real-time execution path. A
producer can obtain several signals and any number of producers can share
one signal. `signal()` carries **no data**; consumers read the latest
blackboard value. A handle becomes inactive when its scheduler is cleared.

The `SchedulerPluginHost` forwards the provider and `MultiScheduler` looks
up signals across its child schedulers. Names must be unambiguous across
children of `MultiScheduler`; duplicate matches return no handle.

## Execution and nominal fairness

All pipelines use nominal priority `origin + offset + (k+1) * period`,
with smaller values scheduled first. Equal nominal priorities are ordered
by stable configuration order. An EVENT is eligible only after a signal
and not before its minimum permissible start-to-start interval.

After an EVENT execution, its `k` advances past nominal opportunities
already missed and to the smallest value behind the competing priority.
Notifications accumulated before or during an execution are coalesced and
discarded; they are not a backlog. An EVENT never runs more frequently than
its nominal design period. A TIMER advances one nominal period per execution,
retaining the existing catch-up policy for late periodic work.

A single worker uses `epoll` over its timerfd, its stop eventfd and each
pipeline's eventfd. No global EventService or timestamped notification
windows are involved. Only whole pipelines are non-preemptive; a long
pipeline may delay other pipelines and must be accounted for in temporal
analysis. Reusing a block across schedulers requires the block to provide
its own reentrancy/concurrency guarantees; the framework does not enforce
exclusive membership.
