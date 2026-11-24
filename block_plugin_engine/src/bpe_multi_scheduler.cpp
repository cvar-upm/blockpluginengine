// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include "detail/bpe_multi_scheduler.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <utility>
#include <string_view>
#include <time.h>

#include <pluginlib/class_list_macros.hpp>
#include <pluginlib/exceptions.hpp>

namespace bpe {
namespace {
constexpr const char* DEFAULT_SCHEDULER = "block_plugin_engine/FairScheduler";
constexpr std::int64_t NS_PER_SEC = 1'000'000'000LL;

bool parse_delay(const YAML::Node& node, std::chrono::nanoseconds& delay) {
  if (!node || !node.IsScalar())
    return false;
  const std::string value = node.as<std::string>();
  std::string_view v{value};
  std::int64_t factor = 0;
  std::size_t suffix = 0;
  for (const auto& unit : {std::pair<std::string_view, std::int64_t>{"ns", 1},
                           {"us", 1'000},
                           {"ms", 1'000'000},
                           {"s", NS_PER_SEC}}) {
    if (v.size() > unit.first.size() && v.substr(v.size() - unit.first.size()) == unit.first) {
      factor = unit.second;
      suffix = unit.first.size();
      break;
    }
  }
  if (!suffix)
    return false;
  v.remove_suffix(suffix);
  std::int64_t n = 0;
  const auto converted = std::from_chars(v.data(), v.data() + v.size(), n);
  if (converted.ec != std::errc{} || converted.ptr != v.data() + v.size() || n < 0 ||
      n > std::numeric_limits<std::int64_t>::max() / factor)
    return false;
  delay = std::chrono::nanoseconds{n * factor};
  return true;
}

std::chrono::nanoseconds monotonic_now() {
  timespec time{};
  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    return std::chrono::nanoseconds{-1};
  return std::chrono::seconds{time.tv_sec} + std::chrono::nanoseconds{time.tv_nsec};
}
} // namespace

BPE_MultiScheduler::BPE_MultiScheduler() : loader_("block_plugin_engine", "bpe::IBlockScheduler") {}
BPE_MultiScheduler::~BPE_MultiScheduler() {
  stop();
  clear();
  schedulers_.clear();
}

SchedulerResult BPE_MultiScheduler::configure(const SchedulerConfig& config) {
  if (running_ || !entries_.empty())
    return SchedulerResult::INVALID_STATE;
  if (!config || !config.IsMap() || !config["threads"] || !config["threads"].IsSequence() ||
      config["threads"].size() == 0)
    return SchedulerResult::INVALID_CONFIG;

  std::vector<std::shared_ptr<IBlockScheduler>> selected;
  std::chrono::nanoseconds delay{std::chrono::milliseconds{100}};
  try {
    if (config["start_delay"] && !parse_delay(config["start_delay"], delay))
      return SchedulerResult::INVALID_CONFIG;
    for (const auto& node : config["threads"]) {
      if (!node.IsMap())
        return SchedulerResult::INVALID_CONFIG;
      const std::string type = node["type"] ? node["type"].as<std::string>() : DEFAULT_SCHEDULER;
      if (type.empty() || type == "block_plugin_engine/MultiScheduler")
        return SchedulerResult::INVALID_CONFIG;
      auto scheduler = loader_.createSharedInstance(type);
      const auto result = scheduler->configure(node);
      if (result != SchedulerResult::SUCCESS)
        return result;
      selected.push_back(std::move(scheduler));
    }
  } catch (const YAML::Exception&) {
    return SchedulerResult::INVALID_CONFIG;
  } catch (const pluginlib::PluginlibException&) {
    return SchedulerResult::INVALID_CONFIG;
  }
  schedulers_ = std::move(selected);
  start_delay_ = delay;
  return SchedulerResult::SUCCESS;
}

SchedulerResult BPE_MultiScheduler::register_block(const std::string& name,
                                                   IScheduledBlock& block,
                                                   const YAML::Node& execution) {
  if (running_ || schedulers_.empty())
    return SchedulerResult::INVALID_STATE;
  if (std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.name == name; }))
    return SchedulerResult::INVALID_CONFIG;

  // Only the target thread is interpreted here. All other fields belong to
  // the selected scheduler (and may be ignored by a pipeline scheduler).
  std::size_t thread = 0;
  try {
    if (execution && !execution.IsNull() && execution.IsMap() && execution["thread"]) {
      const auto index = execution["thread"].as<std::int64_t>();
      if (index < 0)
        return SchedulerResult::INVALID_CONFIG;
      thread = static_cast<std::size_t>(index);
    }
  } catch (const YAML::Exception&) {
    return SchedulerResult::INVALID_CONFIG;
  }
  if (thread >= schedulers_.size())
    return SchedulerResult::INVALID_CONFIG;

  // A child scheduler receives its local configuration, not MultiScheduler's
  // routing selector. YAML::Clone prevents mutating the caller's configuration.
  YAML::Node local = execution && !execution.IsNull() ? YAML::Clone(execution) : YAML::Node{};
  if (local.IsMap())
    local.remove("thread");
  const auto result = schedulers_[thread]->register_block(name, block, local);
  if (result == SchedulerResult::SUCCESS)
    entries_.push_back({name, thread});
  return result;
}

std::shared_ptr<IEventSignal> BPE_MultiScheduler::get_signal(std::string_view name) {
  std::shared_ptr<IEventSignal> result;
  for (const auto& scheduler : schedulers_) {
    auto* provider = dynamic_cast<IEventSignalProvider*>(scheduler.get());
    if (!provider)
      continue;
    auto candidate = provider->get_signal(name);
    // Ambiguity is a configuration error: use globally distinct signal names.
    if (candidate && result)
      return {};
    if (candidate)
      result = std::move(candidate);
  }
  return result;
}

SchedulerResult BPE_MultiScheduler::remove(const std::string& name) {
  if (running_)
    return SchedulerResult::INVALID_STATE;
  const auto it = std::find_if(
      entries_.begin(), entries_.end(), [&](const Entry& e) { return e.name == name; });
  if (it == entries_.end())
    return SchedulerResult::NOT_FOUND;
  const auto result = schedulers_[it->index]->remove(name);
  if (result == SchedulerResult::SUCCESS)
    entries_.erase(it);
  return result;
}

SchedulerResult BPE_MultiScheduler::start(std::chrono::nanoseconds origin) {
  if (running_ || schedulers_.empty())
    return SchedulerResult::INVALID_STATE;
  const auto now = monotonic_now();
  if (origin.count() < 0 || now.count() < 0)
    return SchedulerResult::SYSTEM_ERROR;
  if (start_delay_.count() > std::numeric_limits<std::int64_t>::max() - origin.count())
    return SchedulerResult::INVALID_CONFIG;
  const auto shared_origin = origin + start_delay_;
  if (shared_origin <= now)
    return SchedulerResult::INVALID_CONFIG;
  std::size_t started = 0;
  SchedulerResult result = SchedulerResult::SUCCESS;
  for (auto& scheduler : schedulers_) {
    result = scheduler->start(shared_origin);
    if (result != SchedulerResult::SUCCESS)
      break;
    ++started;
  }
  if (result == SchedulerResult::SUCCESS && monotonic_now() >= shared_origin)
    result = SchedulerResult::SYSTEM_ERROR;
  if (result != SchedulerResult::SUCCESS) {
    while (started > 0)
      schedulers_[--started]->stop();
  } else {
    running_ = true;
  }
  return result;
}

SchedulerResult BPE_MultiScheduler::stop() {
  SchedulerResult result = SchedulerResult::SUCCESS;
  for (auto& scheduler : schedulers_) {
    if (scheduler->stop() != SchedulerResult::SUCCESS)
      result = SchedulerResult::SYSTEM_ERROR;
  }
  running_ = false;
  return result;
}

SchedulerResult BPE_MultiScheduler::clear() {
  if (running_)
    return SchedulerResult::INVALID_STATE;
  SchedulerResult result = SchedulerResult::SUCCESS;
  for (auto& scheduler : schedulers_) {
    if (scheduler->clear() != SchedulerResult::SUCCESS)
      result = SchedulerResult::SYSTEM_ERROR;
  }
  if (result == SchedulerResult::SUCCESS)
    entries_.clear();
  return result;
}
} // namespace bpe

PLUGINLIB_EXPORT_CLASS(bpe::BPE_MultiScheduler, bpe::IBlockScheduler)
