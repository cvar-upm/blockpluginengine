// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include <memory>
#include <pluginlib/class_list_macros.hpp>

#include "block_plugin_engine/event_signal.hpp"
#include "block_plugin_engine/iblock_plugin.hpp"
#include "tools/event_log.hpp"

namespace bpe::test {
class TestSignalUserMockBlock final : public IBlockPlugin, public IEventSignalUser {
public:
  void set_event_signal_provider(IEventSignalProvider& provider) override {
    provider_ = &provider;
    record_event("signal.inject");
  }
  BlockResult configure(const BlockConfig& config) override {
    signal_ = provider_ ? provider_->get_signal("ready") : nullptr;
    record_event(signal_ ? "signal.configure:resolved" : "signal.configure:missing");
    return (signal_ || (config["optional"] && config["optional"].as<bool>()))
        ? BlockResult::SUCCESS : BlockResult::FAILURE;
  }
  BlockResult start() override {
    if (!signal_ || !signal_->signal()) return BlockResult::FAILURE;
    record_event("signal.start:sent");
    return BlockResult::SUCCESS;
  }
private:
  IEventSignalProvider* provider_{nullptr};
  std::shared_ptr<IEventSignal> signal_;
};
} // namespace bpe::test
PLUGINLIB_EXPORT_CLASS(bpe::test::TestSignalUserMockBlock, bpe::IBlockPlugin)
