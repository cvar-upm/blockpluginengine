// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <numeric>
#include <string>
#include <vector>
#include <iostream>
#include <pluginlib/class_list_macros.hpp>
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/ischeduled_block.hpp"
#include "block_plugin_engine/typed_blackboard.hpp"
#include "types.hpp"
namespace bpe_examples {
class Generator final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& cfg) override {
    try {
      producer_.bind(cfg["output"].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) {
      return bpe::BlockResult::FAILURE;
    }
  }
  bpe::ExecutionResult execute() override {
    auto out = producer_.write_access();
    const double n = static_cast<double>(++counter_);
    out.value() = Sample{counter_, {n, n + 2.0, n + 4.0}};
    return bpe::ExecutionResult::SUCCESS;
  }

private:
  bpe::TypedProducer<Sample> producer_;
  std::uint64_t counter_{0};
};
class Average final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& cfg) override {
    try {
      consumer_.bind(cfg["input"].as<std::string>());
      producer_.bind(cfg["output"].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) {
      return bpe::BlockResult::FAILURE;
    }
  }
  bpe::ExecutionResult execute() override {
    auto in = consumer_.read_access();
    if (!in.has_new_value() || in.value().values.empty())
      return bpe::ExecutionResult::SUCCESS;
    auto out = producer_.write_access();
    const auto& values = in.value().values;
    out.value() = Report{in.value().sequence,
                         std::accumulate(values.begin(), values.end(), 0.0) / values.size()};
    return bpe::ExecutionResult::SUCCESS;
  }

private:
  bpe::TypedConsumer<Sample> consumer_;
  bpe::TypedProducer<Report> producer_;
};
class ReportPrinter final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& cfg) override {
    try {
      consumer_.bind(cfg["input"].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) {
      return bpe::BlockResult::FAILURE;
    }
  }
  bpe::ExecutionResult execute() override {
    auto in = consumer_.read_access();
    if (in.has_new_value())
      std::cout << "[Report] "
                << "seq=" << in.value().sequence << ": " << in.value().average << std::endl;
    return bpe::ExecutionResult::SUCCESS;
  }

private:
  bpe::TypedConsumer<Report> consumer_;
};
} // namespace bpe_examples
PLUGINLIB_EXPORT_CLASS(bpe_examples::Generator, bpe::IBlockPlugin)
PLUGINLIB_EXPORT_CLASS(bpe_examples::Average, bpe::IBlockPlugin)
PLUGINLIB_EXPORT_CLASS(bpe_examples::ReportPrinter, bpe::IBlockPlugin)
