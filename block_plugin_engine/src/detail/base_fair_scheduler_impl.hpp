#pragma once
// Included by base_fair_scheduler.hpp; no separate compilation required.

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <string_view>
#include <time.h>

#include "block_plugin_engine/itimer_block.hpp"
#include "block_plugin_engine/itimer_block_with_info.hpp"

namespace bpe {
namespace {

constexpr std::int64_t NANOSECONDS_PER_SECOND = 1'000'000'000LL;

inline std::int64_t to_nanoseconds(const std::chrono::nanoseconds& value) {
  return value.count();
}

inline bool ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

} // namespace

template<typename Clock, typename RealtimePolicy>
BaseFairScheduler<Clock, RealtimePolicy>::~BaseFairScheduler() {
  stop();
}

template<typename Clock, typename RealtimePolicy>
SchedulerResult BaseFairScheduler<Clock, RealtimePolicy>::configure(const SchedulerConfig& config) {
  SchedulerResult result = SchedulerResult::SUCCESS;

  if (running_.load(std::memory_order_acquire)) {
    result = SchedulerResult::INVALID_STATE;
  } else {
    FairSchedulerConfig parsed{};

    try {
      if (config && !config.IsMap()) {
        result = SchedulerResult::INVALID_CONFIG;
      }

      if (result == SchedulerResult::SUCCESS && config["realtime"]) {
        const YAML::Node realtime = config["realtime"];

        if (!realtime.IsMap()) {
          result = SchedulerResult::INVALID_CONFIG;
        } else {
          if (realtime["enabled"]) {
            parsed.realtime_enabled = realtime["enabled"].as<bool>();
          }

          if (realtime["required"]) {
            parsed.realtime_required = realtime["required"].as<bool>();
          }

          if (realtime["priority"]) {
            parsed.realtime_priority = realtime["priority"].as<int>();
          }
        }
      }

      if (result == SchedulerResult::SUCCESS && config["timing"]) {
        const YAML::Node timing = config["timing"];

        if (!timing.IsMap()) {
          result = SchedulerResult::INVALID_CONFIG;
        } else if (timing["spin_before_deadline"]) {
          std::chrono::nanoseconds spin_time{};

          if (!parse_duration(timing["spin_before_deadline"], spin_time)) {
            result = SchedulerResult::INVALID_CONFIG;
          } else {
            parsed.spin_before_deadline = spin_time;
          }
        }
      }
    } catch (const YAML::Exception&) {
      result = SchedulerResult::INVALID_CONFIG;
    }

    if (result == SchedulerResult::SUCCESS) {
      config_ = parsed;
    }
  }

  return result;
}

template<typename Clock, typename RealtimePolicy>
SchedulerResult BaseFairScheduler<Clock, RealtimePolicy>::add(const std::string& instance_name,
                                         ITimerBlock& block,
                                         const ExecutionConfig& config) {
  SchedulerResult result = SchedulerResult::SUCCESS;

  if (running_.load(std::memory_order_acquire)) {
    result = SchedulerResult::INVALID_STATE;
  } else if (config.policy != ExecutionPolicy::TIMER || config.period.count() <= 0 ||
             config.offset.count() < 0 || config.thread != 0) {
    result = SchedulerResult::INVALID_CONFIG;
  } else if (config_.spin_before_deadline.count() < 0 ||
             config_.spin_before_deadline >= config.period) {
    result = SchedulerResult::INVALID_CONFIG;
  } else {
    const auto duplicated = std::find_if(
        blocks_.begin(), blocks_.end(), [&instance_name](const ScheduledBlock& scheduled) {
          return scheduled.name == instance_name;
        });

    if (duplicated != blocks_.end()) {
      result = SchedulerResult::INVALID_CONFIG;
    } else {
      blocks_.push_back(
          ScheduledBlock{instance_name, &block, dynamic_cast<ITimerBlockWithInfo*>(&block),
                         0, to_nanoseconds(config.period), to_nanoseconds(config.offset), timespec{}});
    }
  }

  return result;
}

template<typename Clock, typename RealtimePolicy>
SchedulerResult BaseFairScheduler<Clock, RealtimePolicy>::remove(const std::string& instance_name) {
  SchedulerResult result = SchedulerResult::SUCCESS;

  if (running_.load(std::memory_order_acquire)) {
    result = SchedulerResult::INVALID_STATE;
  } else {
    const auto entry = std::find_if(
        blocks_.begin(), blocks_.end(), [&instance_name](const ScheduledBlock& scheduled) {
          return scheduled.name == instance_name;
        });

    if (entry == blocks_.end()) {
      result = SchedulerResult::NOT_FOUND;
    } else {
      blocks_.erase(entry);
    }
  }

  return result;
}

template<typename Clock, typename RealtimePolicy>
SchedulerResult BaseFairScheduler<Clock, RealtimePolicy>::start(std::chrono::nanoseconds origin) {
  SchedulerResult result = SchedulerResult::SUCCESS;

  if (running_.load(std::memory_order_acquire)) {
    result = SchedulerResult::INVALID_STATE;
  } else {
    if (origin.count() < 0) {
      result = SchedulerResult::INVALID_CONFIG;
    } else {
      result = validate_start_configuration();
    }

    if (result == SchedulerResult::SUCCESS) {
      initialize_deadlines(origin);

      worker_ready_.store(false, std::memory_order_release);
      start_gate_.store(false, std::memory_order_release);
      realtime_setup_ok_.store(!config_.realtime_enabled, std::memory_order_release);
      running_.store(true, std::memory_order_release);

      if (blocks_.empty()) {
        worker_ready_.store(true, std::memory_order_release);
        start_gate_.store(true, std::memory_order_release);
      } else {
        worker_ = std::thread(&BaseFairScheduler<Clock, RealtimePolicy>::worker_loop, this);

        while (!worker_ready_.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }

        if (config_.realtime_enabled && !realtime_setup_ok_.load(std::memory_order_acquire) &&
            config_.realtime_required) {
          running_.store(false, std::memory_order_release);
          start_gate_.store(true, std::memory_order_release);
          worker_.join();
          result = SchedulerResult::SYSTEM_ERROR;
        } else {
          start_gate_.store(true, std::memory_order_release);
        }
      }
    }
  }

  return result;
}

template<typename Clock, typename RealtimePolicy>
SchedulerResult BaseFairScheduler<Clock, RealtimePolicy>::stop() {
  if (running_.exchange(false, std::memory_order_acq_rel)) {
    start_gate_.store(true, std::memory_order_release);

    clock_->notify_stop();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  return SchedulerResult::SUCCESS;
}

template<typename Clock, typename RealtimePolicy>
SchedulerResult BaseFairScheduler<Clock, RealtimePolicy>::clear() {
  SchedulerResult result = SchedulerResult::SUCCESS;

  if (running_.load(std::memory_order_acquire)) {
    result = SchedulerResult::INVALID_STATE;
  } else {
    blocks_.clear();
  }

  return result;
}

template<typename Clock, typename RealtimePolicy>
bool BaseFairScheduler<Clock, RealtimePolicy>::time_before(const timespec& lhs, const timespec& rhs) {
  return lhs.tv_sec < rhs.tv_sec || (lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec < rhs.tv_nsec);
}

template<typename Clock, typename RealtimePolicy>
bool BaseFairScheduler<Clock, RealtimePolicy>::time_reached(const timespec& now, const timespec& deadline) {
  return !time_before(now, deadline);
}

template<typename Clock, typename RealtimePolicy>
timespec BaseFairScheduler<Clock, RealtimePolicy>::subtract_ns(const timespec& time, std::int64_t nanoseconds) {
  timespec result = time;

  result.tv_sec -= nanoseconds / NANOSECONDS_PER_SECOND;
  result.tv_nsec -= nanoseconds % NANOSECONDS_PER_SECOND;

  if (result.tv_nsec < 0) {
    --result.tv_sec;
    result.tv_nsec += NANOSECONDS_PER_SECOND;
  }

  return result;
}

template<typename Clock, typename RealtimePolicy>
void BaseFairScheduler<Clock, RealtimePolicy>::add_ns(timespec& time, std::int64_t nanoseconds) {
  time.tv_sec += nanoseconds / NANOSECONDS_PER_SECOND;
  time.tv_nsec += nanoseconds % NANOSECONDS_PER_SECOND;

  if (time.tv_nsec >= NANOSECONDS_PER_SECOND) {
    ++time.tv_sec;
    time.tv_nsec -= NANOSECONDS_PER_SECOND;
  }
}

template<typename Clock, typename RealtimePolicy>
bool BaseFairScheduler<Clock, RealtimePolicy>::parse_duration(const YAML::Node& node, std::chrono::nanoseconds& value) {
  bool success = false;

  if (node.IsScalar()) {
    const std::string text = node.as<std::string>();
    std::string_view view{text};

    std::int64_t multiplier = 0;
    std::size_t suffix_length = 0;

    if (ends_with(view, "ns")) {
      multiplier = 1;
      suffix_length = 2;
    } else if (ends_with(view, "us")) {
      multiplier = 1'000;
      suffix_length = 2;
    } else if (ends_with(view, "ms")) {
      multiplier = 1'000'000;
      suffix_length = 2;
    } else if (ends_with(view, "s")) {
      multiplier = NANOSECONDS_PER_SECOND;
      suffix_length = 1;
    }

    if (suffix_length > 0) {
      view.remove_suffix(suffix_length);

      std::int64_t amount = 0;
      const auto conversion = std::from_chars(view.data(), view.data() + view.size(), amount);

      if (conversion.ec == std::errc{} && conversion.ptr == view.data() + view.size() &&
          amount >= 0) {
        value = std::chrono::nanoseconds{amount * multiplier};
        success = true;
      }
    }
  }

  return success;
}

template<typename Clock, typename RealtimePolicy>
SchedulerResult BaseFairScheduler<Clock, RealtimePolicy>::validate_start_configuration() const {
  SchedulerResult result = SchedulerResult::SUCCESS;

  if (config_.spin_before_deadline.count() < 0) {
    result = SchedulerResult::INVALID_CONFIG;
  } else if (config_.realtime_enabled) {
    const int minimum = sched_get_priority_min(SCHED_FIFO);
    const int maximum = sched_get_priority_max(SCHED_FIFO);

    if (minimum == -1 || maximum == -1 || config_.realtime_priority < minimum ||
        config_.realtime_priority > maximum) {
      result = SchedulerResult::INVALID_CONFIG;
    }
  }

  return result;
}

template<typename Clock, typename RealtimePolicy>
bool BaseFairScheduler<Clock, RealtimePolicy>::configure_realtime_thread() {
  bool success = true;

  if (config_.realtime_enabled) {
    success = realtime_.request_fifo(config_.realtime_priority);
  }

  return success;
}

template<typename Clock, typename RealtimePolicy>
void BaseFairScheduler<Clock, RealtimePolicy>::initialize_deadlines(std::chrono::nanoseconds origin) {
  timespec now{};
  now.tv_sec = static_cast<time_t>(origin.count() / NANOSECONDS_PER_SECOND);
  now.tv_nsec = static_cast<long>(origin.count() % NANOSECONDS_PER_SECOND);

  for (auto& block : blocks_) {
    block.activation_index = 0;
    block.next_execution = now;
    add_ns(block.next_execution, block.period_ns);
    add_ns(block.next_execution, block.offset_ns);
  }
}

template<typename Clock, typename RealtimePolicy>
void BaseFairScheduler<Clock, RealtimePolicy>::worker_loop() {
  const bool realtime_ok = configure_realtime_thread();
  realtime_setup_ok_.store(realtime_ok, std::memory_order_release);
  worker_ready_.store(true, std::memory_order_release);

  while (!start_gate_.load(std::memory_order_acquire) && running_.load(std::memory_order_acquire)) {
  }

  while (running_.load(std::memory_order_acquire)) {
    // Select one activation per pass: minimum nominal deadline, with insertion
    // order as the stable tie breaker. No allocations occur in this loop.
    ScheduledBlock* earliest = &blocks_.front();
    for (std::size_t i = 1; i < blocks_.size(); ++i) {
      if (time_before(blocks_[i].next_execution, earliest->next_execution)) {
        earliest = &blocks_[i];
      }
    }

    const timespec wake_time =
        subtract_ns(earliest->next_execution, config_.spin_before_deadline.count());
    clock_->wait_until(wake_time, running_);
    if (running_.load(std::memory_order_acquire)) {
      clock_->busy_wait_until(earliest->next_execution, running_);
    }

    if (running_.load(std::memory_order_acquire)) {
      const timespec now = clock_->now();
      if (time_reached(now, earliest->next_execution)) {
        if (earliest->block_with_info != nullptr) {
          const std::int64_t scheduled_ns =
              static_cast<std::int64_t>(earliest->next_execution.tv_sec) *
                  NANOSECONDS_PER_SECOND + earliest->next_execution.tv_nsec;
          const std::int64_t now_ns =
              static_cast<std::int64_t>(now.tv_sec) * NANOSECONDS_PER_SECOND + now.tv_nsec;
          const TimerExecutionInfo info{
              earliest->activation_index,
              std::chrono::nanoseconds{scheduled_ns},
              std::chrono::nanoseconds{std::max<std::int64_t>(0, now_ns - scheduled_ns)}};
          earliest->block_with_info->execute(info);
        } else {
          earliest->block->execute();
        }
        ++earliest->activation_index;
        add_ns(earliest->next_execution, earliest->period_ns);
      }
    }
  }
}


} // namespace bpe
