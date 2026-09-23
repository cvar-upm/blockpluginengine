#include "detail/bpe_simple_scheduler.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <pthread.h>
#include <sched.h>
#include <time.h>

#include "block_plugin_engine/itimer_block.hpp"

namespace bpe {
namespace {

constexpr std::int64_t NANOSECONDS_PER_SECOND = 1'000'000'000LL;

std::int64_t to_nanoseconds(const std::chrono::nanoseconds& value) {
  return value.count();
}

} // namespace

BPE_SimpleScheduler::BPE_SimpleScheduler(SimpleSchedulerConfig config) : config_(config) {}

BPE_SimpleScheduler::~BPE_SimpleScheduler() {
  stop();
}

SchedulerResult BPE_SimpleScheduler::add(const std::string& instance_name,
                                         ITimerBlock& block,
                                         const ExecutionConfig& config) {
  SchedulerResult result = SchedulerResult::SUCCESS;

  if (running_.load(std::memory_order_acquire)) {
    result = SchedulerResult::INVALID_STATE;
  } else if (config.policy != ExecutionPolicy::TIMER || config.period.count() <= 0) {
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
          ScheduledBlock{instance_name, &block, to_nanoseconds(config.period), timespec{}});
    }
  }

  return result;
}

SchedulerResult BPE_SimpleScheduler::remove(const std::string& instance_name) {
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

SchedulerResult BPE_SimpleScheduler::start() {
  SchedulerResult result = SchedulerResult::SUCCESS;

  if (running_.load(std::memory_order_acquire)) {
    result = SchedulerResult::INVALID_STATE;
  } else {
    result = validate_start_configuration();

    if (result == SchedulerResult::SUCCESS) {
      initialize_deadlines();

      worker_ready_.store(false, std::memory_order_release);
      start_gate_.store(false, std::memory_order_release);
      realtime_setup_ok_.store(!config_.realtime_enabled, std::memory_order_release);
      running_.store(true, std::memory_order_release);

      if (blocks_.empty()) {
        worker_ready_.store(true, std::memory_order_release);
        start_gate_.store(true, std::memory_order_release);
      } else {
        worker_ = std::thread(&BPE_SimpleScheduler::worker_loop, this);

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

SchedulerResult BPE_SimpleScheduler::stop() {
  if (running_.exchange(false, std::memory_order_acq_rel)) {
    start_gate_.store(true, std::memory_order_release);

    if (worker_.joinable()) {
      worker_.join();
    }
  }

  return SchedulerResult::SUCCESS;
}

SchedulerResult BPE_SimpleScheduler::clear() {
  SchedulerResult result = SchedulerResult::SUCCESS;

  if (running_.load(std::memory_order_acquire)) {
    result = SchedulerResult::INVALID_STATE;
  } else {
    blocks_.clear();
  }

  return result;
}

bool BPE_SimpleScheduler::time_before(const timespec& lhs, const timespec& rhs) {
  return lhs.tv_sec < rhs.tv_sec || (lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec < rhs.tv_nsec);
}

bool BPE_SimpleScheduler::time_reached(const timespec& now, const timespec& deadline) {
  return !time_before(now, deadline);
}

timespec BPE_SimpleScheduler::subtract_ns(const timespec& time, std::int64_t nanoseconds) {
  timespec result = time;

  result.tv_sec -= nanoseconds / NANOSECONDS_PER_SECOND;
  result.tv_nsec -= nanoseconds % NANOSECONDS_PER_SECOND;

  if (result.tv_nsec < 0) {
    --result.tv_sec;
    result.tv_nsec += NANOSECONDS_PER_SECOND;
  }

  return result;
}

void BPE_SimpleScheduler::add_ns(timespec& time, std::int64_t nanoseconds) {
  time.tv_sec += nanoseconds / NANOSECONDS_PER_SECOND;
  time.tv_nsec += nanoseconds % NANOSECONDS_PER_SECOND;

  if (time.tv_nsec >= NANOSECONDS_PER_SECOND) {
    ++time.tv_sec;
    time.tv_nsec -= NANOSECONDS_PER_SECOND;
  }
}

SchedulerResult BPE_SimpleScheduler::validate_start_configuration() const {
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

bool BPE_SimpleScheduler::configure_realtime_thread() {
  bool success = true;

  if (config_.realtime_enabled) {
    sched_param parameters{};
    parameters.sched_priority = config_.realtime_priority;

    success = pthread_setschedparam(pthread_self(), SCHED_FIFO, &parameters) == 0;
  }

  return success;
}

void BPE_SimpleScheduler::initialize_deadlines() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);

  for (auto& block : blocks_) {
    block.next_execution = now;
    add_ns(block.next_execution, block.period_ns);
  }
}

void BPE_SimpleScheduler::worker_loop() {
  const bool realtime_ok = configure_realtime_thread();
  realtime_setup_ok_.store(realtime_ok, std::memory_order_release);
  worker_ready_.store(true, std::memory_order_release);

  while (!start_gate_.load(std::memory_order_acquire) && running_.load(std::memory_order_acquire)) {
  }

  while (running_.load(std::memory_order_acquire)) {
    const ScheduledBlock* earliest = &blocks_.front();

    for (std::size_t i = 1; i < blocks_.size(); ++i) {
      if (time_before(blocks_[i].next_execution, earliest->next_execution)) {
        earliest = &blocks_[i];
      }
    }

    const timespec wake_time =
        subtract_ns(earliest->next_execution, config_.spin_before_deadline.count());

    wait_until(wake_time);

    if (running_.load(std::memory_order_acquire)) {
      busy_wait_until(earliest->next_execution);
    }

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);

    for (auto& block : blocks_) {
      if (running_.load(std::memory_order_acquire) && time_reached(now, block.next_execution)) {
        block.block->execute();
        add_ns(block.next_execution, block.period_ns);
        clock_gettime(CLOCK_MONOTONIC, &now);
      }
    }
  }
}

void BPE_SimpleScheduler::wait_until(const timespec& deadline) {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);

  if (time_before(now, deadline)) {
    int result = 0;

    do {
      result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
    } while (result == EINTR && running_.load(std::memory_order_acquire));
  }
}

void BPE_SimpleScheduler::busy_wait_until(const timespec& deadline) {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);

  while (running_.load(std::memory_order_acquire) && time_before(now, deadline)) {
    clock_gettime(CLOCK_MONOTONIC, &now);
  }
}

} // namespace bpe
