// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <pluginlib/class_list_macros.hpp>
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/ischeduled_block.hpp"
#include "block_plugin_engine/typed_blackboard.hpp"
#include "types.hpp"
namespace bpe_examples {
// MultiConsumer<Sample,Sample> + TypedProducer<Report>: two sources fused into one result.
class FuseSamples final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& c) override {
    try {
      inputs_.bind(c["input_a"].as<std::string>(), c["input_b"].as<std::string>());
      output_.bind(c["output"].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) {
      return bpe::BlockResult::FAILURE;
    }
  }
  bpe::ExecutionResult execute() override {
    auto in = inputs_.read_access();
    if (in.get<0>().has_new_value()) {
      a_ = in.get<0>().value();
      fresh_a_ = true;
    }
    if (in.get<1>().has_new_value()) {
      b_ = in.get<1>().value();
      fresh_b_ = true;
    }
    if (!fresh_a_ || !fresh_b_ || a_.values.empty() || b_.values.empty())
      return bpe::ExecutionResult::SUCCESS;
    double sum = 0.0;
    for (double x : a_.values)
      sum += x;
    for (double x : b_.values)
      sum += x;
    auto out = output_.write_access();
    out.value() =
        Report{std::max(a_.sequence, b_.sequence), sum / (a_.values.size() + b_.values.size())};
    fresh_a_ = fresh_b_ = false;
    return bpe::ExecutionResult::SUCCESS;
  }

private:
  bpe::MultiConsumer<Sample, Sample> inputs_;
  bpe::TypedProducer<Report> output_;
  Sample a_, b_;
  bool fresh_a_{false}, fresh_b_{false};
};
// TypedConsumer<Report> + MultiProducer<Report,Summary>: split into two types.
class SplitReport final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& c) override {
    try {
      input_.bind(c["input"].as<std::string>());
      outputs_.bind(c["output_report"].as<std::string>(), c["output_summary"].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) {
      return bpe::BlockResult::FAILURE;
    }
  }
  bpe::ExecutionResult execute() override {
    auto in = input_.read_access();
    if (!in.has_new_value())
      return bpe::ExecutionResult::SUCCESS;
    auto out = outputs_.write_access();
    out.get<0>().value() = in.value();
    out.get<1>().value() =
        Summary{in.value().sequence, {in.value().average, std::abs(in.value().average)}};
    return bpe::ExecutionResult::SUCCESS;
  }

private:
  bpe::TypedConsumer<Report> input_;
  bpe::MultiProducer<Report, Summary> outputs_;
};
// MultiConsumer<Report,Summary> + MultiProducer<Report,Summary>.
class PairAnnotator final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& c) override {
    try {
      inputs_.bind(c["input_report"].as<std::string>(), c["input_summary"].as<std::string>());
      outputs_.bind(c["output_report"].as<std::string>(), c["output_summary"].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) {
      return bpe::BlockResult::FAILURE;
    }
  }
  bpe::ExecutionResult execute() override {
    auto in = inputs_.read_access();
    if (in.get<0>().has_new_value()) {
      report_ = in.get<0>().value();
      report_fresh_ = true;
    }
    if (in.get<1>().has_new_value()) {
      summary_ = in.get<1>().value();
      summary_fresh_ = true;
    }
    if (!report_fresh_ || !summary_fresh_)
      return bpe::ExecutionResult::SUCCESS;
    auto out = outputs_.write_access();
    out.get<0>().value() = Report{report_.sequence, report_.average};
    out.get<1>().value() = Summary{summary_.sequence, summary_.statistics};
    report_fresh_ = summary_fresh_ = false;
    return bpe::ExecutionResult::SUCCESS;
  }

private:
  bpe::MultiConsumer<Report, Summary> inputs_;
  bpe::MultiProducer<Report, Summary> outputs_;
  Report report_;
  Summary summary_;
  bool report_fresh_{false}, summary_fresh_{false};
};
class SummaryPrinter final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& c) override {
    try {
      consumer_.bind(c["input"].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) {
      return bpe::BlockResult::FAILURE;
    }
  }
  bpe::ExecutionResult execute() override {
    auto in = consumer_.read_access();
    if (in.has_new_value()) {
      std::cout << "[Summary] seq=" << in.value().sequence;
      for (double x : in.value().statistics)
        std::cout << " " << x;
      std::cout << std::endl;
    }
    return bpe::ExecutionResult::SUCCESS;
  }

private:
  bpe::TypedConsumer<Summary> consumer_;
};
} // namespace bpe_examples
PLUGINLIB_EXPORT_CLASS(bpe_examples::FuseSamples, bpe::IBlockPlugin)
PLUGINLIB_EXPORT_CLASS(bpe_examples::SplitReport, bpe::IBlockPlugin)
PLUGINLIB_EXPORT_CLASS(bpe_examples::PairAnnotator, bpe::IBlockPlugin)
PLUGINLIB_EXPORT_CLASS(bpe_examples::SummaryPrinter, bpe::IBlockPlugin)
