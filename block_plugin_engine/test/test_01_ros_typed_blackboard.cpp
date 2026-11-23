#include <stdexcept>
#include <string>
#include <vector>

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

namespace {

struct NativeSample {
  std::vector<double> values;
  int sequence{0};
};

struct WrongNativeSample {
  double value{0.0};
};

TEST(RosTypedBlackboard, NativeCppObjectRoundTripWithoutRosSerialization) {
  const std::string label = "test_01_ros_typed_blackboard.native_vector";
  bpe::TypedProducer<NativeSample> producer;
  bpe::TypedConsumer<NativeSample> consumer;
  producer.bind(label);
  consumer.bind(label);

  {
    auto output = producer.write_access();
    output.value().values = {1.0, 2.5, 4.0};
    output.value().sequence = 7;
  }
  {
    auto input = consumer.read_access();
    ASSERT_TRUE(input.has_new_value());
    EXPECT_EQ(input.value().sequence, 7);
    EXPECT_EQ(input.value().values, (std::vector<double>{1.0, 2.5, 4.0}));
  }
  // Native channels do not construct an ROS serialized representation.
  auto probe = bpe::the_blackboard().register_consumer(label);
  auto raw = probe.read_access();
  EXPECT_FALSE(raw.data().serialized.has_value());
}

TEST(RosTypedBlackboard, NativeCppTypeMismatchIsRejectedAtBind) {
  const std::string label = "test_01_ros_typed_blackboard.native_type_mismatch";
  bpe::TypedProducer<NativeSample> producer;
  producer.bind(label);
  bpe::TypedConsumer<WrongNativeSample> wrong;
  EXPECT_THROW(wrong.bind(label), std::runtime_error);
}

TEST(RosTypedBlackboard, RosValueWithoutBoundaryDoesNotAllocateSerialization) {
  const std::string label = "test_01_ros_typed_blackboard.ros_native_only";
  bpe::TypedProducer<TestMessage> producer;
  producer.bind(label);
  {
    auto output = producer.write_access();
    output.value().x = 3.0;
  }
  auto probe = bpe::the_blackboard().register_consumer(label);
  auto input = probe.read_access();
  EXPECT_FALSE(input.data().serialized.has_value());
}

} // namespace
