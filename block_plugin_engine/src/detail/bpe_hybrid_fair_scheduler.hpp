#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "block_plugin_engine/event_service.hpp"
#include "block_plugin_engine/iblock_scheduler.hpp"
#include "block_plugin_engine/itimer_block_with_info.hpp"

namespace bpe {

// Separate plugin, deliberately not derived from BPE_FairScheduler.
class BPE_HybridFairScheduler final : public IBlockScheduler {
public:
  ~BPE_HybridFairScheduler() override;
  SchedulerResult configure(const SchedulerConfig&) override;
  SchedulerResult add(const std::string&, ITimerBlock&, const ExecutionConfig&) override;
  SchedulerResult remove(const std::string&) override;
  using IBlockScheduler::start;
  SchedulerResult start(std::chrono::nanoseconds) override;
  SchedulerResult stop() override;
  SchedulerResult clear() override;

private:
  struct Timer {
    std::string name;
    ITimerBlock* block{};
    ITimerBlockWithInfo* with_info{};
    std::int64_t period{};
    std::int64_t offset{};
    std::int64_t deadline{};
    std::uint64_t k{};
    std::size_t registration{};
  };
  struct Consumer {
    std::string name;
    ITimerBlock* block{};
    std::size_t order{};
  };
  struct Event {
    std::string tag;
    std::shared_ptr<EventService::Channel> channel;
    std::vector<Consumer> consumers;
    std::int64_t period{};
    std::int64_t offset{};
    std::int64_t nominal{};
    std::int64_t begin{};
    std::int64_t end{};
    std::uint64_t k{};
    std::size_t registration{};
    bool pending{};
    bool phase_initialized{};
    std::uint64_t coalesced{};
    std::uint64_t missed{};
    std::int64_t deferred_stamp{};
    std::uint64_t deferred_count{};
  };

  bool setup_fds();
  void close_fds();
  void worker_loop();
  void observe_events(int timeout_ms);
  void handle_signal(Event& event);
  void advance_event(Event& event, std::int64_t now, bool executed);
  void program_next_deadline(std::int64_t now);
  bool setup_realtime();
  static std::int64_t now_ns();

  std::vector<Timer> timers_;
  std::vector<Event> events_;
  std::size_t registration_count_{};
  bool realtime_enabled_{true};
  bool realtime_required_{false};
  int realtime_priority_{50};
  std::int64_t phase_step_limit_ns_{0}; // 0 means T/8
  std::int64_t origin_{};
  int epoll_fd_{-1};
  int timer_fd_{-1};
  int stop_fd_{-1};
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> ready_{false};
  std::atomic<bool> realtime_ok_{false};
  std::atomic<bool> start_gate_{false};
};

} // namespace bpe
