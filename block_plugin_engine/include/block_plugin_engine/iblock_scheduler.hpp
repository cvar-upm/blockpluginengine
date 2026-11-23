// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>
#include <chrono>
#include <time.h>

#include "block_plugin_engine/event_signal.hpp"
#include "block_plugin_engine/scheduler_config.hpp"

namespace bpe {

class IScheduledBlock;

enum class SchedulerResult { SUCCESS, INVALID_STATE, INVALID_CONFIG, NOT_FOUND, SYSTEM_ERROR };

class IBlockScheduler {
public:
  virtual ~IBlockScheduler() = default;

  virtual SchedulerResult configure(const SchedulerConfig& config) = 0;

  // Register a block using scheduler-specific execution semantics.
  // Missing YAML configuration is valid: the scheduler decides whether to schedule it.
  virtual SchedulerResult register_block(
      const std::string& instance_name, IScheduledBlock& block,
      const YAML::Node& execution) = 0;

  virtual SchedulerResult remove(const std::string& instance_name) = 0;
  // Monotonic absolute origin shared by composable schedulers.
  virtual SchedulerResult start(std::chrono::nanoseconds origin) = 0;
  virtual SchedulerResult start() {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      return SchedulerResult::SYSTEM_ERROR;
    }
    return start(std::chrono::seconds{now.tv_sec} + std::chrono::nanoseconds{now.tv_nsec});
  }
  virtual SchedulerResult stop() = 0;
  virtual SchedulerResult clear() = 0;
};

} // namespace bpe
