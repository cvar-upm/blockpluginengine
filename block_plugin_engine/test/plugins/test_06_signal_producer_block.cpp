// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include <memory>
#include <string>
#include <pluginlib/class_list_macros.hpp>

#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/ischeduled_block.hpp"
#include "block_plugin_engine/event_signal.hpp"
#include "tools/event_log.hpp"

namespace bpe::test {

// A periodically scheduled producer that signals a pipeline on another worker.
class TestSignalProducerBlock final : public IBlockPlugin,
                                      public IScheduledBlock,
                                      public IEventSignalUser {
public:
  void set_event_signal_provider(IEventSignalProvider& provider) override {
    provider_ = &provider;
    record_event("producer.inject");
  }
  BlockResult configure(const BlockConfig& config) override {
    if (!provider_ || !config["signal"])
      return BlockResult::FAILURE;
    signal_ = provider_->get_signal(config["signal"].as<std::string>());
    record_event(signal_ ? "producer.configure" : "producer.missing_signal");
    return signal_ ? BlockResult::SUCCESS : BlockResult::FAILURE;
  }
  BlockResult start() override {
    record_event("producer.start");
    return BlockResult::SUCCESS;
  }
  BlockResult stop() override {
    return BlockResult::SUCCESS;
  }
  BlockResult cleanup() override {
    signal_.reset();
    return BlockResult::SUCCESS;
  }
  ExecutionResult execute() override {
    record_event("producer.execute");
    return signal_ && signal_->signal() ? ExecutionResult::SUCCESS : ExecutionResult::FAILURE;
  }

private:
  IEventSignalProvider* provider_{nullptr};
  std::shared_ptr<IEventSignal> signal_;
};

} // namespace bpe::test
PLUGINLIB_EXPORT_CLASS(bpe::test::TestSignalProducerBlock, bpe::IBlockPlugin)
