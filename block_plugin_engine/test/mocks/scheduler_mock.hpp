// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "block_plugin_engine/iblock_scheduler.hpp"
#include "block_plugin_engine/ischeduled_block.hpp"

namespace bpe::test {

// Reusable direct-injection mock with optional event signal capability.
class SchedulerMock : public IBlockScheduler, public IEventSignalProvider {
public:
  struct Entry {
    std::string name;
    IScheduledBlock* block;
    YAML::Node config;
  };

  SchedulerResult configure(const SchedulerConfig&) override;
  std::shared_ptr<IEventSignal> get_signal(std::string_view name) override;
  void allow_signal(std::string name);
  SchedulerResult register_block(const std::string&, IScheduledBlock&, const YAML::Node&) override;
  SchedulerResult remove(const std::string&) override;
  SchedulerResult start() override;
  SchedulerResult start(std::chrono::nanoseconds) override;
  SchedulerResult stop() override;
  SchedulerResult clear() override;

  const std::vector<Entry>& entries() const { return entries_; }
  const std::vector<std::string>& calls() const { return calls_; }
  void set_add_result(SchedulerResult result) { add_result_ = result; }
  void set_clear_result(SchedulerResult result) { clear_result_ = result; }
  void reset();
  ExecutionResult execute(const std::string& name);

private:
  std::vector<Entry> entries_;
  std::vector<std::string> calls_;
  SchedulerResult add_result_{SchedulerResult::SUCCESS};
  SchedulerResult clear_result_{SchedulerResult::SUCCESS};
  bool running_{false};
  std::string signal_name_;
  std::shared_ptr<IEventSignal> test_signal_;
};

} // namespace bpe::test
