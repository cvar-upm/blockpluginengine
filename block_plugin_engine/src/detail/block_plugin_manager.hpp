#pragma once

#include <memory>
#include <string>
#include <vector>

#include <pluginlib/class_loader.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/node_interfaces/node_topics_interface.hpp>

#include "block_plugin_engine/iblock_plugin.hpp"
#include "detail/block_instance_config.hpp"

namespace bpe {

class IBlockScheduler;

class BlockPluginManager {
public:
  BlockPluginManager(const std::shared_ptr<IBlockScheduler>&,
                     const rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr&,
                     rclcpp::Logger,
                     const std::vector<std::string>& plugin_xml_paths = {});

  bool configure(const std::string& config_file);
  bool start();
  bool stop();
  bool cleanup();

private:
  struct BlockInstance {
    std::string name;
    std::shared_ptr<IBlockPlugin> plugin;
  };

  bool load_configuration(const std::string& config_file,
                          std::vector<BlockInstanceConfig>& configs) const;
  bool configure_instance(const BlockInstanceConfig& config);
  void rollback_configuration();

  std::shared_ptr<IBlockScheduler> scheduler_;
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr ros_topics_;
  rclcpp::Logger logger_;

  // Must outlive all plugin instances. Members are destroyed in reverse declaration order.
  pluginlib::ClassLoader<IBlockPlugin> loader_;
  std::vector<BlockInstance> blocks_;
};

} // namespace bpe
