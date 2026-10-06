#include <string>

#include <gtest/gtest.h>

#include "block_plugin_engine/typed_blackboard.hpp"

namespace {

struct TestMessage {
  int value{0};
  double gain{0.0};
};

TEST(TypedBlackboard, ProducerAndConsumerShareTypedNativeRepresentation) {
  bpe::TypedProducer<TestMessage> producer{"typed_blackboard.sample"};
  bpe::TypedConsumer<TestMessage> consumer{"typed_blackboard.sample"};

  {
    auto output = producer.write_access();
    output.value().value = 42;
    output.value().gain = 1.5;
  }

  auto input = consumer.read_access();

  ASSERT_TRUE(input.has_new_value());
  EXPECT_EQ(input.value().value, 42);
  EXPECT_DOUBLE_EQ(input.value().gain, 1.5);
}

TEST(TypedBlackboard, ConsumerCanRegisterBeforeProducerWhenItKnowsTheType) {
  bpe::TypedConsumer<TestMessage> consumer{"typed_blackboard.consumer_first"};
  bpe::TypedProducer<TestMessage> producer{"typed_blackboard.consumer_first"};

  {
    auto output = producer.write_access();
    output.value().value = 7;
  }

  auto input = consumer.read_access();

  ASSERT_TRUE(input.has_new_value());
  EXPECT_EQ(input.value().value, 7);
}

} // namespace
