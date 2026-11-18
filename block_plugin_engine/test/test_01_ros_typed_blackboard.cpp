#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <geometry_msgs/msg/vector3.hpp>
#include <geometry_msgs/msg/twist.hpp>

#include "block_plugin_engine/typed_blackboard.hpp"

namespace {

using TestMessage = geometry_msgs::msg::Vector3;
using OtherMessage = geometry_msgs::msg::Twist;

TEST(RosTypedBlackboard, ProducerAndConsumerShareTypedNativeRepresentation) {
  bpe::TypedProducer<TestMessage> producer;
  bpe::TypedConsumer<TestMessage> consumer;

  producer.bind("test_01_ros_typed_blackboard.sample");
  consumer.bind("test_01_ros_typed_blackboard.sample");

  {
    auto output = producer.write_access();
    output.value().x = 42.0;
    output.value().y = 1.5;
  }

  auto input = consumer.read_access();

  ASSERT_TRUE(input.has_new_value());
  EXPECT_DOUBLE_EQ(input.value().x, 42.0);
  EXPECT_DOUBLE_EQ(input.value().y, 1.5);
}

TEST(RosTypedBlackboard, ConsumerCanBindBeforeProducerWhenItKnowsTheType) {
  bpe::TypedConsumer<TestMessage> consumer;
  bpe::TypedProducer<TestMessage> producer;

  consumer.bind("test_01_ros_typed_blackboard.consumer_first");
  producer.bind("test_01_ros_typed_blackboard.consumer_first");

  {
    auto output = producer.write_access();
    output.value().x = 7.0;
  }

  auto input = consumer.read_access();

  ASSERT_TRUE(input.has_new_value());
  EXPECT_DOUBLE_EQ(input.value().x, 7.0);
}

TEST(RosTypedBlackboard, AccessBeforeBindIsRejected) {
  bpe::TypedProducer<TestMessage> producer;
  bpe::TypedConsumer<TestMessage> consumer;

  EXPECT_THROW(producer.write_access(), std::logic_error);
  EXPECT_THROW(consumer.read_access(), std::logic_error);
}

TEST(RosTypedBlackboard, RebindingIsRejected) {
  bpe::TypedProducer<TestMessage> producer;
  bpe::TypedConsumer<TestMessage> consumer;

  producer.bind("test_01_ros_typed_blackboard.rebind_producer");
  consumer.bind("test_01_ros_typed_blackboard.rebind_consumer");

  EXPECT_THROW(producer.bind("test_01_ros_typed_blackboard.other_producer"), std::logic_error);
  EXPECT_THROW(consumer.bind("test_01_ros_typed_blackboard.other_consumer"), std::logic_error);
}


// MultiProducer and MultiConsumer group independent typed channels.
TEST(RosTypedBlackboard, MultiProducerAndMultiConsumerShareTypedValues) {
  bpe::MultiProducer<TestMessage, OtherMessage> producer;
  bpe::MultiConsumer<TestMessage, OtherMessage> consumer;

  producer.bind("test_01_ros_typed_blackboard.multi_vector",
                "test_01_ros_typed_blackboard.multi_twist");
  consumer.bind("test_01_ros_typed_blackboard.multi_vector",
                "test_01_ros_typed_blackboard.multi_twist");

  {
    auto output = producer.write_access();
    output.get<0>().value().x = 42.0;
    output.get<1>().value().angular.z = 1.5;
  }

  auto input = consumer.read_access();
  ASSERT_TRUE(input.get<0>().has_new_value());
  ASSERT_TRUE(input.get<1>().has_new_value());
  EXPECT_DOUBLE_EQ(input.get<0>().value().x, 42.0);
  EXPECT_DOUBLE_EQ(input.get<1>().value().angular.z, 1.5);
}

TEST(RosTypedBlackboard, MultiConsumerCanBindBeforeMultiProducer) {
  bpe::MultiConsumer<TestMessage, OtherMessage> consumer;
  bpe::MultiProducer<TestMessage, OtherMessage> producer;

  consumer.bind("test_01_ros_typed_blackboard.consumer_first_vector",
                "test_01_ros_typed_blackboard.consumer_first_twist");
  producer.bind("test_01_ros_typed_blackboard.consumer_first_vector",
                "test_01_ros_typed_blackboard.consumer_first_twist");

  {
    auto output = producer.write_access();
    output.get<0>().value().y = 7.0;
    output.get<1>().value().linear.x = 8.0;
  }

  auto input = consumer.read_access();
  ASSERT_TRUE(input.get<0>().has_new_value());
  ASSERT_TRUE(input.get<1>().has_new_value());
  EXPECT_DOUBLE_EQ(input.get<0>().value().y, 7.0);
  EXPECT_DOUBLE_EQ(input.get<1>().value().linear.x, 8.0);
}

TEST(RosTypedBlackboard, MultiAccessBeforeBindIsRejected) {
  bpe::MultiProducer<TestMessage, OtherMessage> producer;
  bpe::MultiConsumer<TestMessage, OtherMessage> consumer;

  EXPECT_THROW(producer.write_access(), std::logic_error);
  EXPECT_THROW(consumer.read_access(), std::logic_error);
}

TEST(RosTypedBlackboard, MultiRebindingIsRejected) {
  bpe::MultiProducer<TestMessage, OtherMessage> producer;
  bpe::MultiConsumer<TestMessage, OtherMessage> consumer;

  producer.bind("test_01_ros_typed_blackboard.rebind_multi_producer_a",
                "test_01_ros_typed_blackboard.rebind_multi_producer_b");
  consumer.bind("test_01_ros_typed_blackboard.rebind_multi_consumer_a",
                "test_01_ros_typed_blackboard.rebind_multi_consumer_b");

  EXPECT_THROW(producer.bind("test_01_ros_typed_blackboard.other_multi_producer_a",
                             "test_01_ros_typed_blackboard.other_multi_producer_b"),
               std::logic_error);
  EXPECT_THROW(consumer.bind("test_01_ros_typed_blackboard.other_multi_consumer_a",
                             "test_01_ros_typed_blackboard.other_multi_consumer_b"),
               std::logic_error);
}

// Multiple readers of one channel have independent consumption state.
// This is a sequential contract test, not a concurrent execution test.
TEST(RosTypedBlackboard, MultipleReadersOfSameChannelReceiveValuesIndependently) {
  bpe::TypedProducer<TestMessage> producer;
  bpe::TypedConsumer<TestMessage> reader_a;
  bpe::TypedConsumer<TestMessage> reader_b;

  producer.bind("test_01_ros_typed_blackboard.multiple_readers");
  reader_a.bind("test_01_ros_typed_blackboard.multiple_readers");
  reader_b.bind("test_01_ros_typed_blackboard.multiple_readers");

  {
    auto output = producer.write_access();
    output.value().x = 12.0;
  }

  {
    auto input = reader_a.read_access();
    ASSERT_TRUE(input.has_new_value());
    EXPECT_DOUBLE_EQ(input.value().x, 12.0);
  }
  {
    auto input = reader_b.read_access();
    ASSERT_TRUE(input.has_new_value());
    EXPECT_DOUBLE_EQ(input.value().x, 12.0);
  }

  {
    auto output = producer.write_access();
    output.value().x = 13.0;
  }

  {
    auto input = reader_b.read_access();
    ASSERT_TRUE(input.has_new_value());
    EXPECT_DOUBLE_EQ(input.value().x, 13.0);
  }
  {
    auto input = reader_a.read_access();
    ASSERT_TRUE(input.has_new_value());
    EXPECT_DOUBLE_EQ(input.value().x, 13.0);
  }
}

TEST(RosTypedBlackboard, TwoMultiConsumersReadTheSameChannels) {
  bpe::MultiProducer<TestMessage, OtherMessage> producer;
  bpe::MultiConsumer<TestMessage, OtherMessage> reader_a;
  bpe::MultiConsumer<TestMessage, OtherMessage> reader_b;

  producer.bind("test_01_ros_typed_blackboard.shared_multi_vector",
                "test_01_ros_typed_blackboard.shared_multi_twist");
  reader_a.bind("test_01_ros_typed_blackboard.shared_multi_vector",
                "test_01_ros_typed_blackboard.shared_multi_twist");
  reader_b.bind("test_01_ros_typed_blackboard.shared_multi_vector",
                "test_01_ros_typed_blackboard.shared_multi_twist");

  {
    auto output = producer.write_access();
    output.get<0>().value().z = 3.0;
    output.get<1>().value().linear.y = 4.0;
  }

  {
    auto input = reader_a.read_access();
    ASSERT_TRUE(input.get<0>().has_new_value());
    ASSERT_TRUE(input.get<1>().has_new_value());
    EXPECT_DOUBLE_EQ(input.get<0>().value().z, 3.0);
    EXPECT_DOUBLE_EQ(input.get<1>().value().linear.y, 4.0);
  }
  {
    auto input = reader_b.read_access();
    ASSERT_TRUE(input.get<0>().has_new_value());
    ASSERT_TRUE(input.get<1>().has_new_value());
    EXPECT_DOUBLE_EQ(input.get<0>().value().z, 3.0);
    EXPECT_DOUBLE_EQ(input.get<1>().value().linear.y, 4.0);
  }
}

} // namespace
