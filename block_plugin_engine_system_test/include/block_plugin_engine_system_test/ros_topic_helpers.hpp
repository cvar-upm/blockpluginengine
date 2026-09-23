#pragma once

#include <memory>
#include <string>
#include <utility>

#include <rclcpp/message_memory_strategy.hpp>
#include <rclcpp/node_interfaces/node_topics_interface.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/publisher_factory.hpp>
#include <rclcpp/publisher_options.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/subscription_factory.hpp>
#include <rclcpp/subscription_options.hpp>

namespace bpe_system_test {

template <typename MessageT, typename CallbackT>
typename rclcpp::Subscription<MessageT>::SharedPtr create_subscription(
  const rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr& topics,
  const std::string& topic_name,
  const rclcpp::QoS& qos,
  CallbackT&& callback) {
  using SubscriptionT = rclcpp::Subscription<MessageT>;
  using MemoryStrategyT = typename SubscriptionT::MessageMemoryStrategyType;

  rclcpp::SubscriptionOptions options;
  auto memory_strategy = MemoryStrategyT::create_default();

  auto factory = rclcpp::create_subscription_factory<MessageT>(
    std::forward<CallbackT>(callback),
    options,
    memory_strategy);

  auto subscription_base =
    topics->create_subscription(topic_name, factory, qos);

  topics->add_subscription(
    subscription_base,
    options.callback_group);

  return std::dynamic_pointer_cast<SubscriptionT>(
    subscription_base);
}

template <typename MessageT>
typename rclcpp::Publisher<MessageT>::SharedPtr create_publisher(
  const rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr& topics,
  const std::string& topic_name,
  const rclcpp::QoS& qos) {
  using PublisherT = rclcpp::Publisher<MessageT>;
  using AllocatorT = std::allocator<void>;

  rclcpp::PublisherOptions options;

  auto factory =
    rclcpp::create_publisher_factory<MessageT, AllocatorT, PublisherT>(
      options);

  auto publisher_base =
    topics->create_publisher(topic_name, factory, qos);

  topics->add_publisher(
    publisher_base,
    options.callback_group);

  return std::dynamic_pointer_cast<PublisherT>(
    publisher_base);
}

} // namespace bpe_system_test
