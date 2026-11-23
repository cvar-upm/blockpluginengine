#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "block_plugin_engine/iblock_scheduler.hpp"
#include "detail/itimer_block_with_info.hpp"
#include "detail/hybrid_deadline_clock.hpp"
#include <stdexcept>
#include <utility>

namespace bpe {

// Scheduler-owned, non-preemptive pipelines; no EventService/global registry.
template<typename Clock>
class BaseHybridFairScheduler final : public IBlockScheduler, public IEventSignalProvider {
public:
  BaseHybridFairScheduler() : clock_(std::make_shared<Clock>()) {}
  explicit BaseHybridFairScheduler(std::shared_ptr<Clock> clock)
      : clock_(std::move(clock)) {
    if (!clock_) throw std::invalid_argument("hybrid scheduler clock must not be null");
  }
  ~BaseHybridFairScheduler() override;
  SchedulerResult configure(const SchedulerConfig&) override;
  SchedulerResult register_block(const std::string&, IScheduledBlock&,
                                 const YAML::Node&) override;
  std::shared_ptr<IEventSignal> get_signal(std::string_view name) override;
  SchedulerResult remove(const std::string&) override;
  using IBlockScheduler::start;
  SchedulerResult start(std::chrono::nanoseconds) override;
  SchedulerResult stop() override;
  SchedulerResult clear() override;

private:
  struct Signal final : IEventSignal {
    explicit Signal(int fd) : fd(fd) {}
    ~Signal() override;
    bool signal() noexcept override;
    const int fd;
    std::atomic<bool> active{true};
  };
  struct Block {
    std::string name;
    IScheduledBlock* block{nullptr};
    ITimerBlockWithInfo* with_info{nullptr};
  };
  struct Pipeline {
    std::string name;
    bool event{false};
    std::string signal_name;
    std::shared_ptr<Signal> channel;
    std::vector<Block> blocks;
    std::int64_t period{0};
    std::int64_t offset{0};
    std::int64_t nominal{0};
    std::int64_t next_allowed{0};
    std::uint64_t k{0};
    std::size_t registration{0};
    bool pending{false};
  };

  bool setup_fds();
  void close_fds();
  void worker_loop();
  void observe_events(int timeout_ms);
  void execute_pipeline(Pipeline&, std::int64_t now);
  void advance_event(Pipeline&, std::int64_t now);
  void program_next_deadline(std::int64_t now);
  bool setup_realtime();
  static void drain(int fd);

  std::shared_ptr<Clock> clock_;
  std::vector<Pipeline> pipelines_;
  bool realtime_enabled_{true};
  bool realtime_required_{false};
  int realtime_priority_{50};
  std::int64_t origin_{0};
  int epoll_fd_{-1};
  int timer_fd_{-1};
  int stop_fd_{-1};
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> ready_{false};
  std::atomic<bool> realtime_ok_{false};
  std::atomic<bool> start_gate_{false};
};

using BPE_HybridFairScheduler = BaseHybridFairScheduler<MonotonicHybridClock>;

} // namespace bpe

#include "detail/bpe_hybrid_fair_scheduler_impl.hpp"
