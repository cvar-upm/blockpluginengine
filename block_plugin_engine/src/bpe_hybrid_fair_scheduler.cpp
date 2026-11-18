#include "detail/bpe_hybrid_fair_scheduler.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>
#include <iterator>
#include <pthread.h>
#include <sched.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace bpe {
namespace {
constexpr std::int64_t kSecond = 1'000'000'000LL;
constexpr std::uint64_t kTimerToken = 1;
constexpr std::uint64_t kStopToken = 2;
constexpr std::uint64_t kEventTokenBase = 3;

timespec as_timespec(std::int64_t time) {
  return {static_cast<time_t>(time / kSecond), static_cast<long>(time % kSecond)};
}
}

std::int64_t BPE_HybridFairScheduler::now_ns() {
  timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::int64_t>(ts.tv_sec) * kSecond + ts.tv_nsec;
}

BPE_HybridFairScheduler::~BPE_HybridFairScheduler() { stop(); }

SchedulerResult BPE_HybridFairScheduler::configure(const SchedulerConfig& config) {
  if (running_) return SchedulerResult::INVALID_STATE;
  try {
    if (config && !config.IsMap()) return SchedulerResult::INVALID_CONFIG;
    if (config["realtime"]) {
      const auto rt = config["realtime"];
      if (!rt.IsMap()) return SchedulerResult::INVALID_CONFIG;
      if (rt["enabled"]) realtime_enabled_ = rt["enabled"].as<bool>();
      if (rt["required"]) realtime_required_ = rt["required"].as<bool>();
      if (rt["priority"]) realtime_priority_ = rt["priority"].as<int>();
    }
    if (config["phase_step_limit_ns"]) {
      phase_step_limit_ns_ = config["phase_step_limit_ns"].as<std::int64_t>();
      if (phase_step_limit_ns_ < 0) return SchedulerResult::INVALID_CONFIG;
    }
  } catch (const YAML::Exception&) { return SchedulerResult::INVALID_CONFIG; }
  return SchedulerResult::SUCCESS;
}

SchedulerResult BPE_HybridFairScheduler::add(
    const std::string& name, ITimerBlock& block, const ExecutionConfig& config) {
  if (running_) return SchedulerResult::INVALID_STATE;
  if (name.empty() || config.period.count() <= 0 || config.offset.count() < 0 ||
      config.thread != 0 || config.period.count() > std::numeric_limits<std::int64_t>::max() - config.offset.count())
    return SchedulerResult::INVALID_CONFIG;
  for (const auto& timer : timers_) if (timer.name == name) return SchedulerResult::INVALID_CONFIG;
  for (const auto& event : events_)
    for (const auto& consumer : event.consumers)
      if (consumer.name == name) return SchedulerResult::INVALID_CONFIG;

  if (config.policy == ExecutionPolicy::TIMER) {
    if (!config.event_tag.empty()) return SchedulerResult::INVALID_CONFIG;
    timers_.push_back({name, &block, dynamic_cast<ITimerBlockWithInfo*>(&block),
                       config.period.count(), config.offset.count(), 0, 0, registration_count_++});
    return SchedulerResult::SUCCESS;
  }
  if (config.policy != ExecutionPolicy::EVENT || config.event_tag.empty())
    return SchedulerResult::INVALID_CONFIG;
  auto it = std::find_if(events_.begin(), events_.end(), [&](const Event& e) {
    return e.tag == config.event_tag;
  });
  if (it == events_.end()) {
    auto channel = EventService::instance().channel(config.event_tag);
    if (!channel) return SchedulerResult::SYSTEM_ERROR;
    Event event{};
    event.tag = config.event_tag;
    event.channel = std::move(channel);
    event.period = config.period.count();
    event.offset = config.offset.count();
    event.registration = registration_count_++;
    events_.push_back(std::move(event));
    it = std::prev(events_.end());
  } else if (it->period != config.period.count() || it->offset != config.offset.count()) {
    return SchedulerResult::INVALID_CONFIG;
  }
  for (const auto& consumer : it->consumers)
    if (consumer.order == config.pipeline_order) return SchedulerResult::INVALID_CONFIG;
  it->consumers.push_back({name, &block, config.pipeline_order});
  std::sort(it->consumers.begin(), it->consumers.end(),
            [](const Consumer& a, const Consumer& b) { return a.order < b.order; });
  return SchedulerResult::SUCCESS;
}

SchedulerResult BPE_HybridFairScheduler::remove(const std::string& name) {
  if (running_) return SchedulerResult::INVALID_STATE;
  const auto timer = std::find_if(timers_.begin(), timers_.end(), [&](const Timer& t) {
    return t.name == name;
  });
  if (timer != timers_.end()) { timers_.erase(timer); return SchedulerResult::SUCCESS; }
  for (auto it = events_.begin(); it != events_.end(); ++it) {
    auto& consumers = it->consumers;
    auto consumer = std::find_if(consumers.begin(), consumers.end(), [&](const Consumer& c) {
      return c.name == name;
    });
    if (consumer != consumers.end()) {
      consumers.erase(consumer);
      if (consumers.empty()) events_.erase(it);
      return SchedulerResult::SUCCESS;
    }
  }
  return SchedulerResult::NOT_FOUND;
}

SchedulerResult BPE_HybridFairScheduler::clear() {
  if (running_) return SchedulerResult::INVALID_STATE;
  timers_.clear();
  events_.clear();
  registration_count_ = 0;
  return SchedulerResult::SUCCESS;
}

bool BPE_HybridFairScheduler::setup_fds() {
  epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
  timer_fd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  stop_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (epoll_fd_ < 0 || timer_fd_ < 0 || stop_fd_ < 0) return false;
  auto add_fd = [&](int fd, std::uint64_t token) {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = token;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
  };
  if (!add_fd(timer_fd_, kTimerToken) || !add_fd(stop_fd_, kStopToken)) return false;
  for (std::size_t i = 0; i < events_.size(); ++i)
    if (!add_fd(events_[i].channel->fd, kEventTokenBase + i)) return false;
  return true;
}

void BPE_HybridFairScheduler::close_fds() {
  if (timer_fd_ >= 0) ::close(timer_fd_);
  if (stop_fd_ >= 0) ::close(stop_fd_);
  if (epoll_fd_ >= 0) ::close(epoll_fd_);
  timer_fd_ = stop_fd_ = epoll_fd_ = -1;
}

SchedulerResult BPE_HybridFairScheduler::start(std::chrono::nanoseconds origin) {
  if (running_) return SchedulerResult::INVALID_STATE;
  if (origin.count() < 0) return SchedulerResult::INVALID_CONFIG;
  if (realtime_enabled_ &&
      (realtime_priority_ < sched_get_priority_min(SCHED_FIFO) ||
       realtime_priority_ > sched_get_priority_max(SCHED_FIFO)))
    return SchedulerResult::INVALID_CONFIG;
  origin_ = origin.count();
  for (auto& timer : timers_) {
    timer.k = 0;
    timer.deadline = origin_ + timer.offset + timer.period;
  }
  for (auto& event : events_) {
    event.k = 0;
    event.nominal = origin_ + event.offset + event.period;
    event.begin = origin_ + event.offset;
    event.end = event.nominal;
    event.pending = false;
    event.phase_initialized = false;
    event.coalesced = 0;
    event.missed = 0;
    event.deferred_stamp = 0;
    event.deferred_count = 0;
  }
  if (!setup_fds()) { close_fds(); return SchedulerResult::SYSTEM_ERROR; }
  ready_ = false;
  start_gate_ = false;
  realtime_ok_ = !realtime_enabled_;
  running_ = true;
  worker_ = std::thread(&BPE_HybridFairScheduler::worker_loop, this);
  while (!ready_.load(std::memory_order_acquire)) std::this_thread::yield();
  if (realtime_required_ && !realtime_ok_.load(std::memory_order_acquire)) {
    stop();
    return SchedulerResult::SYSTEM_ERROR;
  }
  start_gate_.store(true, std::memory_order_release);
  return SchedulerResult::SUCCESS;
}

SchedulerResult BPE_HybridFairScheduler::stop() {
  running_.store(false, std::memory_order_release);
  start_gate_.store(true, std::memory_order_release);
  if (stop_fd_ >= 0) ::eventfd_write(stop_fd_, 1);
  if (worker_.joinable()) worker_.join();
  close_fds();
  return SchedulerResult::SUCCESS;
}

bool BPE_HybridFairScheduler::setup_realtime() {
  if (!realtime_enabled_) return true;
  sched_param param{};
  param.sched_priority = realtime_priority_;
  return ::pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
}

void BPE_HybridFairScheduler::handle_signal(Event& event) {
  eventfd_t count{};
  if (::eventfd_read(event.channel->fd, &count) != 0) return;
  const auto stamp = event.channel->last_signal_ns.load(std::memory_order_acquire);
  // Consume all notifications, including duplicates. Never reassign an old
  // notification to a subsequent window.
  if (stamp >= event.begin && stamp < event.end && !event.pending) {
    event.pending = true;
    if (count > 1) event.coalesced += count - 1;
  } else if (stamp >= event.end) {
    // Preserve the latest signal received for a later nominal window.
    event.deferred_stamp = stamp;
    event.deferred_count += count;
  } else {
    event.coalesced += count;
  }
}

void BPE_HybridFairScheduler::observe_events(int timeout_ms) {
  epoll_event ready[32]{};
  int count = ::epoll_wait(epoll_fd_, ready, 32, timeout_ms);
  if (count < 0 && errno == EINTR) return;
  for (int j = 0; j < count; ++j) {
    const auto id = ready[j].data.u64;
    if (id == kTimerToken) {
      std::uint64_t expirations{};
      (void)::read(timer_fd_, &expirations, sizeof(expirations));
    } else if (id >= kEventTokenBase && id - kEventTokenBase < events_.size()) {
      handle_signal(events_[id - kEventTokenBase]);
    }
  }
}

void BPE_HybridFairScheduler::advance_event(Event& event, std::int64_t now, bool executed) {
  // Nominal priority always advances by the configured period. Only the
  // prediction window is phase-corrected; its bounds remain contiguous.
  std::int64_t correction = 0;
  if (executed) {
    const auto stamp = event.channel->last_signal_ns.load(std::memory_order_acquire);
    if (stamp >= event.begin && stamp < event.end) {
      const auto error = stamp - (event.begin + event.period / 2);
      const auto limit = phase_step_limit_ns_ > 0 ?
          std::min(phase_step_limit_ns_, event.period / 8) : event.period / 8;
      correction = std::clamp(error / 8, -limit, limit);
      event.phase_initialized = true;
    }
  } else {
    ++event.missed;
  }
  ++event.k;
  event.nominal += event.period;
  event.begin = event.end;
  event.end += event.period + correction;
  // After a late callback, retain monotonic windows without changing nominal priority.
  (void)now;
  event.pending = false;
  if (event.deferred_count && event.deferred_stamp >= event.begin &&
      event.deferred_stamp < event.end) {
    event.pending = true;
    event.coalesced += event.deferred_count - 1;
    event.deferred_count = 0;
    event.deferred_stamp = 0;
  } else if (event.deferred_count && event.deferred_stamp < event.begin) {
    event.coalesced += event.deferred_count;
    event.deferred_count = 0;
    event.deferred_stamp = 0;
  }
}

void BPE_HybridFairScheduler::program_next_deadline(std::int64_t now) {
  std::int64_t earliest = std::numeric_limits<std::int64_t>::max();
  for (const auto& timer : timers_) earliest = std::min(earliest, timer.deadline);
  for (const auto& event : events_) {
    earliest = std::min(earliest, event.end);
    if (event.begin > now) earliest = std::min(earliest, event.begin);
  }
  itimerspec spec{};
  if (earliest != std::numeric_limits<std::int64_t>::max()) {
    spec.it_value = as_timespec(std::max(earliest, std::int64_t{1}));
  }
  (void)::timerfd_settime(timer_fd_, TFD_TIMER_ABSTIME, &spec, nullptr);
}

void BPE_HybridFairScheduler::worker_loop() {
  const bool rt = setup_realtime();
  realtime_ok_.store(rt, std::memory_order_release);
  ready_.store(true, std::memory_order_release);
  while (!start_gate_.load(std::memory_order_acquire) && running_) std::this_thread::yield();

  while (running_) {
    // Drain the wait set first: epoll order never determines block order.
    observe_events(0);
    const auto now = now_ns();
    Timer* timer_choice = nullptr;
    Event* event_choice = nullptr;
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    std::size_t best_registration = std::numeric_limits<std::size_t>::max();
    for (auto& timer : timers_) {
      if (timer.deadline <= now && (timer.deadline < best ||
          (timer.deadline == best && timer.registration < best_registration))) {
        best = timer.deadline;
        best_registration = timer.registration;
        timer_choice = &timer;
        event_choice = nullptr;
      }
    }
    for (auto& event : events_) {
      // Pending events may execute before their nominal deadline; expiration
      // of an empty window simply consumes its nominal opportunity.
      if ((event.pending || event.end <= now) && event.begin <= now &&
          (event.nominal < best ||
           (event.nominal == best && event.registration < best_registration))) {
        best = event.nominal;
        best_registration = event.registration;
        event_choice = &event;
        timer_choice = nullptr;
      }
    }
    if (timer_choice) {
      Timer& timer = *timer_choice;
      if (timer.with_info) {
        timer.with_info->execute({timer.k, std::chrono::nanoseconds(timer.deadline),
             std::chrono::nanoseconds(std::max<std::int64_t>(0, now - timer.deadline))});
      } else timer.block->execute();
      ++timer.k;
      timer.deadline += timer.period;
      continue;
    }
    if (event_choice) {
      Event& event = *event_choice;
      const bool executed = event.pending;
      if (executed) {
        for (const auto& consumer : event.consumers) {
          if (!running_) break;
          consumer.block->execute();
        }
      }
      advance_event(event, now_ns(), executed);
      continue;
    }
    program_next_deadline(now);
    observe_events(-1);
  }
}

} // namespace bpe

#include <pluginlib/class_list_macros.hpp>
PLUGINLIB_EXPORT_CLASS(bpe::BPE_HybridFairScheduler, bpe::IBlockScheduler)
