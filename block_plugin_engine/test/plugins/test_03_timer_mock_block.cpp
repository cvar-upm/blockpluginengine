// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include <string>

#include <pluginlib/class_list_macros.hpp>

#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/itimer_block.hpp"
#include "tools/event_log.hpp"

namespace bpe::test {

// Compatibility fixture: the plugin intentionally uses the pre-1.0 alias.
class TestTimerBlock : public IBlockPlugin, public ITimerBlock {
public:
  BlockResult configure(const BlockConfig& config) override {
    if (!config["id"] || !config["label"]) {
      return BlockResult::FAILURE;
    }

    id_ = config["id"].as<std::string>();
    label_ = config["label"].as<std::string>();

    record_event(id_ + ".configure:" + label_);
    return BlockResult::SUCCESS;
  }

  BlockResult start() override {
    record_event(id_ + ".start");
    return BlockResult::SUCCESS;
  }

  BlockResult stop() override {
    record_event(id_ + ".stop");
    return BlockResult::SUCCESS;
  }

  BlockResult cleanup() override {
    record_event(id_ + ".cleanup");
    return BlockResult::SUCCESS;
  }

  ExecutionResult execute() override {
    record_event(id_ + ".execute");
    return ExecutionResult::SUCCESS;
  }

private:
  std::string id_;
  std::string label_;
};

} // namespace bpe::test

PLUGINLIB_EXPORT_CLASS(bpe::test::TestTimerBlock, bpe::IBlockPlugin)