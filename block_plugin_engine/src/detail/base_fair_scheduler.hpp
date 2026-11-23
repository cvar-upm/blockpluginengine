#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <thread>
#include <time.h>
#include <vector>

#include "block_plugin_engine/iblock_scheduler.hpp"
#include "detail/itimer_block_with_info.hpp"
#include "detail/monotonic_clock.hpp"

namespace bpe {

struct FairSchedulerConfig {
  bool realtime_enabled{true};
  bool realtime_required{false};
  int realtime_priority{50};
  std::chrono::nanoseconds spin_before_deadline{std::chrono::microseconds{100}};
};

template<typename Clock, typename RealtimePolicy = SystemRealtimePolicy>
class BaseFairScheduler final : public IBlockScheduler {
public:
  BaseFairScheduler() : clock_(std::make_shared<Clock>()) {}
  explicit BaseFairScheduler(std::shared_ptr<Clock> clock, RealtimePolicy realtime = {})
      : clock_(std::move(clock)), realtime_(std::move(realtime)) {
    if (!clock_) { throw std::invalid_argument("scheduler clock must not be null"); }
  }
  ~BaseFairScheduler() override;

  std::shared_ptr<Clock> clock() const noexcept { return clock_; }
  SchedulerResult start() override {
    const timespec t = clock_->now();
    return start(std::chrono::seconds{t.tv_sec} + std::chrono::nanoseconds{t.tv_nsec});
  }

  SchedulerResult configure(const SchedulerConfig& config) override;

  SchedulerResult register_block(const std::string& instance_name,
                                 IScheduledBlock& block,
                                 const YAML::Node& execution) override;

  SchedulerResult remove(const std::string& instance_name) override;
  using IBlockScheduler::start;
  SchedulerResult start(std::chrono::nanoseconds origin) override;
  SchedulerResult stop() override;
  SchedulerResult clear() override;

private:
  struct ScheduledBlock {
    std::string name;
    IScheduledBlock* block{nullptr};
    ITimerBlockWithInfo* block_with_info{nullptr};
    std::uint64_t activation_index{0};
    std::int64_t period_ns{0};
    std::int64_t offset_ns{0};
    timespec next_execution{};
  };

  static bool time_before(const timespec& lhs, const timespec& rhs);
  static bool time_reached(const timespec& now, const timespec& deadline);
  static timespec subtract_ns(const timespec& time, std::int64_t nanoseconds);
  static void add_ns(timespec& time, std::int64_t nanoseconds);
  static bool parse_duration(const YAML::Node& node, std::chrono::nanoseconds& value);

  SchedulerResult validate_start_configuration() const;
  bool configure_realtime_thread();
  void initialize_deadlines(std::chrono::nanoseconds origin);
  void worker_loop();

  std::shared_ptr<Clock> clock_;
  RealtimePolicy realtime_{};
  FairSchedulerConfig config_;
  std::vector<ScheduledBlock> blocks_;
  std::thread worker_;

  std::atomic<bool> running_{false};
  std::atomic<bool> worker_ready_{false};
  std::atomic<bool> start_gate_{false};
  std::atomic<bool> realtime_setup_ok_{false};
};

using BPE_FairScheduler = BaseFairScheduler<MonotonicClock>;

} // namespace bpe

#include "detail/base_fair_scheduler_impl.hpp"
