// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once
// Included from bpe_hybrid_fair_scheduler.hpp; template implementation.

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <limits>
#include <pthread.h>
#include <sched.h>
#include <string_view>
#include <utility>
#include <ctime>
#include <exception>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <unistd.h>

namespace bpe {
namespace {
constexpr std::int64_t kSecond = 1'000'000'000LL;
constexpr std::uint64_t kTimerToken = 1;
constexpr std::uint64_t kStopToken = 2;
constexpr std::uint64_t kEventTokenBase = 3;

bool parse_duration(const YAML::Node& node, bool positive, std::int64_t& out) {
  if (!node || !node.IsScalar())
    return false;
  const std::string value = node.as<std::string>();
  const std::string_view v{value};
  for (const auto& [unit, factor] : {std::pair<std::string_view, std::int64_t>{"ns", 1},
                                     {"us", 1'000},
                                     {"ms", 1'000'000},
                                     {"s", kSecond}}) {
    if (v.size() <= unit.size() || v.substr(v.size() - unit.size()) != unit)
      continue;
    const auto number = v.substr(0, v.size() - unit.size());
    std::int64_t amount{};
    const auto result = std::from_chars(number.data(), number.data() + number.size(), amount);
    if (result.ec != std::errc{} || result.ptr != number.data() + number.size() || amount < 0 ||
        (positive && amount == 0) || amount > std::numeric_limits<std::int64_t>::max() / factor)
      return false;
    out = amount * factor;
    return true;
  }
  return false;
}
} // namespace

template <typename Clock> BaseHybridFairScheduler<Clock>::Signal::~Signal() {
  if (fd >= 0)
    ::close(fd);
}

template <typename Clock> bool BaseHybridFairScheduler<Clock>::Signal::signal() noexcept {
  return active.load(std::memory_order_acquire) && ::eventfd_write(fd, 1) == 0;
}

template <typename Clock> BaseHybridFairScheduler<Clock>::~BaseHybridFairScheduler() {
  stop();
  clear();
}

template <typename Clock>
SchedulerResult BaseHybridFairScheduler<Clock>::configure(const SchedulerConfig& config) {
  if (running_ || !pipelines_.empty())
    return SchedulerResult::INVALID_STATE;
  try {
    if (config && !config.IsMap())
      return SchedulerResult::INVALID_CONFIG;
    bool rt_enabled = true;
    bool rt_required = false;
    int rt_priority = 50;
    if (config["realtime"]) {
      const auto rt = config["realtime"];
      if (!rt.IsMap())
        return SchedulerResult::INVALID_CONFIG;
      if (rt["enabled"])
        rt_enabled = rt["enabled"].as<bool>();
      if (rt["required"])
        rt_required = rt["required"].as<bool>();
      if (rt["priority"])
        rt_priority = rt["priority"].as<int>();
    }
    std::vector<Pipeline> parsed;
    const auto list = config["pipelines"];
    if (list && !list.IsSequence())
      return SchedulerResult::INVALID_CONFIG;
    for (const auto& node : list) {
      if (!node.IsMap() || !node["name"] || !node["policy"] || !node["period"] || !node["blocks"] ||
          !node["blocks"].IsSequence() || node["blocks"].size() == 0)
        return SchedulerResult::INVALID_CONFIG;
      Pipeline pipeline;
      pipeline.name = node["name"].as<std::string>();
      const auto policy = node["policy"].as<std::string>();
      if (pipeline.name.empty() || (policy != "timer" && policy != "event") ||
          std::any_of(parsed.begin(),
                      parsed.end(),
                      [&](const Pipeline& p) { return p.name == pipeline.name; }) ||
          !parse_duration(node["period"], true, pipeline.period))
        return SchedulerResult::INVALID_CONFIG;
      pipeline.event = policy == "event";
      if (node["offset"] && !parse_duration(node["offset"], false, pipeline.offset))
        return SchedulerResult::INVALID_CONFIG;
      if (pipeline.offset > std::numeric_limits<std::int64_t>::max() - pipeline.period)
        return SchedulerResult::INVALID_CONFIG;
      if (pipeline.event) {
        if (!node["signal"])
          return SchedulerResult::INVALID_CONFIG;
        pipeline.signal_name = node["signal"].as<std::string>();
        if (pipeline.signal_name.empty() ||
            std::any_of(parsed.begin(), parsed.end(), [&](const Pipeline& p) {
              return p.signal_name == pipeline.signal_name;
            }))
          return SchedulerResult::INVALID_CONFIG;
        const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (fd < 0)
          return SchedulerResult::SYSTEM_ERROR;
        pipeline.channel = std::make_shared<Signal>(fd);
      } else if (node["signal"])
        return SchedulerResult::INVALID_CONFIG;
      for (const auto& name_node : node["blocks"]) {
        const auto name = name_node.as<std::string>();
        if (name.empty())
          return SchedulerResult::INVALID_CONFIG;
        pipeline.blocks.push_back({name, nullptr, nullptr});
      }
      pipeline.registration = parsed.size();
      parsed.push_back(std::move(pipeline));
    }
    realtime_enabled_ = rt_enabled;
    realtime_required_ = rt_required;
    realtime_priority_ = rt_priority;
    pipelines_ = std::move(parsed);
  } catch (const YAML::Exception&) {
    return SchedulerResult::INVALID_CONFIG;
  } catch (const std::exception&) {
    return SchedulerResult::INVALID_CONFIG;
  }
  return SchedulerResult::SUCCESS;
}

template <typename Clock>
SchedulerResult BaseHybridFairScheduler<Clock>::register_block(const std::string& name,
                                                               IScheduledBlock& block,
                                                               const YAML::Node&) {
  // Pipelines are scheduler-owned. Individual execution settings are intentionally ignored.
  if (running_)
    return SchedulerResult::INVALID_STATE;
  for (auto& pipeline : pipelines_)
    for (auto& entry : pipeline.blocks) {
      if (entry.name == name) {
        entry.block = &block;
        entry.with_info = dynamic_cast<ITimerBlockWithInfo*>(&block);
      }
    }
  return SchedulerResult::SUCCESS;
}

template <typename Clock>
std::shared_ptr<IEventSignal> BaseHybridFairScheduler<Clock>::get_signal(std::string_view name) {
  for (const auto& pipeline : pipelines_)
    if (pipeline.event && pipeline.signal_name == name && pipeline.channel)
      return pipeline.channel;
  return {};
}

template <typename Clock>
SchedulerResult BaseHybridFairScheduler<Clock>::remove(const std::string& name) {
  if (running_)
    return SchedulerResult::INVALID_STATE;
  bool found = false;
  for (auto& pipeline : pipelines_)
    for (auto& block : pipeline.blocks) {
      if (block.name == name && block.block) {
        block.block = nullptr;
        block.with_info = nullptr;
        found = true;
      }
    }
  return found ? SchedulerResult::SUCCESS : SchedulerResult::NOT_FOUND;
}

template <typename Clock> SchedulerResult BaseHybridFairScheduler<Clock>::clear() {
  if (running_)
    return SchedulerResult::INVALID_STATE;
  for (auto& pipeline : pipelines_)
    if (pipeline.channel)
      pipeline.channel->active.store(false, std::memory_order_release);
  pipelines_.clear();
  return SchedulerResult::SUCCESS;
}

template <typename Clock> void BaseHybridFairScheduler<Clock>::drain(int fd) {
  eventfd_t count{};
  while (::eventfd_read(fd, &count) == 0) {
  }
}

template <typename Clock> bool BaseHybridFairScheduler<Clock>::setup_fds() {
  epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
  timer_fd_ = clock_->create_deadline_fd();
  stop_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (epoll_fd_ < 0 || timer_fd_ < 0 || stop_fd_ < 0)
    return false;
  auto add_fd = [&](int fd, std::uint64_t token) {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = token;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
  };
  if (!add_fd(timer_fd_, kTimerToken) || !add_fd(stop_fd_, kStopToken))
    return false;
  for (std::size_t i = 0; i < pipelines_.size(); ++i)
    if (pipelines_[i].event && !add_fd(pipelines_[i].channel->fd, kEventTokenBase + i))
      return false;
  return true;
}

template <typename Clock> void BaseHybridFairScheduler<Clock>::close_fds() {
  if (timer_fd_ >= 0) {
    clock_->release_deadline_fd(timer_fd_);
    ::close(timer_fd_);
  }
  if (stop_fd_ >= 0)
    ::close(stop_fd_);
  if (epoll_fd_ >= 0)
    ::close(epoll_fd_);
  timer_fd_ = stop_fd_ = epoll_fd_ = -1;
}

template <typename Clock>
SchedulerResult BaseHybridFairScheduler<Clock>::start(std::chrono::nanoseconds origin) {
  if (running_)
    return SchedulerResult::INVALID_STATE;
  if (origin.count() < 0)
    return SchedulerResult::INVALID_CONFIG;
  if (realtime_enabled_ && (realtime_priority_ < sched_get_priority_min(SCHED_FIFO) ||
                            realtime_priority_ > sched_get_priority_max(SCHED_FIFO)))
    return SchedulerResult::INVALID_CONFIG;
  for (const auto& pipeline : pipelines_) {
    if (std::any_of(pipeline.blocks.begin(), pipeline.blocks.end(), [](const Block& block) {
          return block.block == nullptr;
        }))
      return SchedulerResult::INVALID_CONFIG;
    if (origin.count() >
        std::numeric_limits<std::int64_t>::max() - pipeline.offset - pipeline.period)
      return SchedulerResult::INVALID_CONFIG;
  }
  origin_ = origin.count();
  for (auto& pipeline : pipelines_) {
    pipeline.k = 0;
    pipeline.nominal = origin_ + pipeline.offset + pipeline.period;
    pipeline.next_allowed = origin_ + pipeline.offset;
    pipeline.pending = false;
    if (pipeline.event)
      drain(pipeline.channel->fd);
  }
  if (!setup_fds()) {
    close_fds();
    return SchedulerResult::SYSTEM_ERROR;
  }
  ready_ = false;
  start_gate_ = false;
  realtime_ok_ = !realtime_enabled_;
  running_ = true;
  worker_ = std::thread(&BaseHybridFairScheduler<Clock>::worker_loop, this);
  while (!ready_.load(std::memory_order_acquire))
    std::this_thread::yield();
  if (realtime_required_ && !realtime_ok_.load(std::memory_order_acquire)) {
    stop();
    return SchedulerResult::SYSTEM_ERROR;
  }
  start_gate_.store(true, std::memory_order_release);
  return SchedulerResult::SUCCESS;
}

template <typename Clock> SchedulerResult BaseHybridFairScheduler<Clock>::stop() {
  running_.store(false, std::memory_order_release);
  clock_->on_stop();
  start_gate_.store(true, std::memory_order_release);
  if (stop_fd_ >= 0)
    (void)::eventfd_write(stop_fd_, 1);
  if (worker_.joinable())
    worker_.join();
  close_fds();
  return SchedulerResult::SUCCESS;
}

template <typename Clock> bool BaseHybridFairScheduler<Clock>::setup_realtime() {
  if (!realtime_enabled_)
    return true;
  sched_param param{};
  param.sched_priority = realtime_priority_;
  return ::pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
}

template <typename Clock> void BaseHybridFairScheduler<Clock>::observe_events(int timeout_ms) {
  epoll_event ready[32]{};
  const int count = ::epoll_wait(epoll_fd_, ready, 32, timeout_ms);
  if (count < 0 && errno == EINTR)
    return;
  if (count <= 0)
    return;
  for (int i = 0; i < count; ++i) {
    const auto token = ready[i].data.u64;
    if (token == kTimerToken) {
      clock_->consume_deadline(timer_fd_);
    } else if (token == kStopToken) {
      drain(stop_fd_);
    } else if (token >= kEventTokenBase && token - kEventTokenBase < pipelines_.size()) {
      auto& pipeline = pipelines_[token - kEventTokenBase];
      drain(pipeline.channel->fd);
      // Events inside the minimum interval are deliberately discarded, not queued.
      if (clock_->now_ns() >= pipeline.next_allowed)
        pipeline.pending = true;
    }
  }
}

template <typename Clock>
void BaseHybridFairScheduler<Clock>::execute_pipeline(Pipeline& pipeline, std::int64_t now) {
  // Once selected, a pipeline completes even if stop() is requested.
  for (const auto& block : pipeline.blocks) {
    if (block.with_info) {
      block.with_info->execute(
          {pipeline.k,
           std::chrono::nanoseconds(pipeline.nominal),
           std::chrono::nanoseconds(std::max<std::int64_t>(0, now - pipeline.nominal))});
    } else
      block.block->execute();
  }
}

template <typename Clock>
void BaseHybridFairScheduler<Clock>::advance_event(Pipeline& pipeline, std::int64_t now) {
  // Only one pipeline execution per minimum real-time period. Skipped
  // nominal opportunities never produce catch-up executions.
  const auto period = pipeline.period;
  pipeline.next_allowed = now > std::numeric_limits<std::int64_t>::max() - period
                              ? std::numeric_limits<std::int64_t>::max()
                              : now + period;
  std::int64_t reference = std::numeric_limits<std::int64_t>::max();
  for (const auto& other : pipelines_) {
    if (&other != &pipeline)
      reference = std::min(reference, other.nominal);
  }
  // Advance k to the first nominal priority strictly behind the closest
  // competing pipeline; ties are resolved in favor of the competitor.
  // Even if there is no other eligible pipeline, missed nominal periods
  // are discarded. The actual execution time is the lower-bound reference.
  std::int64_t target = std::max(pipeline.nominal, now);
  if (reference != std::numeric_limits<std::int64_t>::max())
    target = std::max(target, reference);
  const auto steps = (target - pipeline.nominal) / period + 1;
  pipeline.k += static_cast<std::uint64_t>(steps);
  pipeline.nominal += steps * period;
}

template <typename Clock>
void BaseHybridFairScheduler<Clock>::program_next_deadline(std::int64_t now) {
  std::int64_t earliest = std::numeric_limits<std::int64_t>::max();
  for (const auto& pipeline : pipelines_) {
    if (pipeline.event) {
      if (pipeline.pending && pipeline.next_allowed > now)
        earliest = std::min(earliest, pipeline.next_allowed);
    } else
      earliest = std::min(earliest, pipeline.nominal);
  }
  (void)clock_->arm_deadline(timer_fd_,
                             earliest == std::numeric_limits<std::int64_t>::max() ? -1 : earliest);
}

template <typename Clock> void BaseHybridFairScheduler<Clock>::worker_loop() {
  const bool rt = setup_realtime();
  realtime_ok_.store(rt, std::memory_order_release);
  ready_.store(true, std::memory_order_release);
  while (!start_gate_.load(std::memory_order_acquire) && running_)
    std::this_thread::yield();

  while (running_) {
    clock_->before_observe();
    if (!running_)
      break;
    observe_events(0);
    const auto now = clock_->now_ns();
    Pipeline* choice = nullptr;
    for (auto& pipeline : pipelines_) {
      const bool eligible = pipeline.event ? pipeline.pending && now >= pipeline.next_allowed
                                           : now >= pipeline.nominal;
      if (eligible &&
          (!choice || pipeline.nominal < choice->nominal ||
           (pipeline.nominal == choice->nominal && pipeline.registration < choice->registration)))
        choice = &pipeline;
    }
    if (choice) {
      if (choice->event) {
        choice->pending = false;
        execute_pipeline(*choice, now);
        // Coalesce notifications received during the pipeline's execution.
        drain(choice->channel->fd);
        advance_event(*choice, now);
      } else {
        execute_pipeline(*choice, now);
        ++choice->k;
        choice->nominal += choice->period;
      }
      continue;
    }
    program_next_deadline(now);
    observe_events(-1);
  }
}

} // namespace bpe
