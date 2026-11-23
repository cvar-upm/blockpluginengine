// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <memory>
#include <string>

#include <pluginlib/class_loader.hpp>
#include <rclcpp/node_options.hpp>

#include "block_plugin_engine/iblock_scheduler.hpp"

namespace bpe {

class BPE_LifecycleNode;
class BlockPluginManager;

// Owns the runtime, loads its configuration and wires the selected scheduler
// into the block manager. The scheduler loader outlives every loaded instance.
class BPECompositionRoot {
public:
  explicit BPECompositionRoot(
      const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~BPECompositionRoot();

  BPECompositionRoot(const BPECompositionRoot&) = delete;
  BPECompositionRoot& operator=(const BPECompositionRoot&) = delete;
  BPECompositionRoot(BPECompositionRoot&&) = delete;
  BPECompositionRoot& operator=(BPECompositionRoot&&) = delete;

  const std::shared_ptr<BPE_LifecycleNode>& node() const noexcept;

  // Called by lifecycle callbacks, after the config_file parameter is known.
  bool configure(const std::string& config_file);
  bool cleanup();
  const std::shared_ptr<IBlockScheduler>& scheduler() const noexcept { return scheduler_; }
  BlockPluginManager& plugin_manager() noexcept { return *plugin_manager_; }

private:
  pluginlib::ClassLoader<IBlockScheduler> scheduler_loader_;
  std::shared_ptr<IBlockScheduler> scheduler_;
  std::shared_ptr<BPE_LifecycleNode> node_;
  std::shared_ptr<BlockPluginManager> plugin_manager_;
};

} // namespace bpe
