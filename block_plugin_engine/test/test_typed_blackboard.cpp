#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "block_plugin_engine/typed_blackboard.hpp"

namespace {

struct TestMessage {
  int value{0};
  double gain{0.0};
};

TEST(TypedBlackboard, ProducerAndConsumerShareTypedNativeRepresentation) {
  bpe::TypedProducer<TestMessage> producer;
  bpe::TypedConsumer<TestMessage> consumer;

  producer.bind("typed_blackboard.sample");
  consumer.bind("typed_blackboard.sample");

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

TEST(TypedBlackboard, ConsumerCanBindBeforeProducerWhenItKnowsTheType) {
  bpe::TypedConsumer<TestMessage> consumer;
  bpe::TypedProducer<TestMessage> producer;

  consumer.bind("typed_blackboard.consumer_first");
  producer.bind("typed_blackboard.consumer_first");

  {
    auto output = producer.write_access();
    output.value().value = 7;
  }

  auto input = consumer.read_access();

  ASSERT_TRUE(input.has_new_value());
  EXPECT_EQ(input.value().value, 7);
}

TEST(TypedBlackboard, AccessBeforeBindIsRejected) {
  bpe::TypedProducer<TestMessage> producer;
  bpe::TypedConsumer<TestMessage> consumer;

  EXPECT_THROW(producer.write_access(), std::logic_error);
  EXPECT_THROW(consumer.read_access(), std::logic_error);
}

TEST(TypedBlackboard, RebindingIsRejected) {
  bpe::TypedProducer<TestMessage> producer;
  bpe::TypedConsumer<TestMessage> consumer;

  producer.bind("typed_blackboard.rebind_producer");
  consumer.bind("typed_blackboard.rebind_consumer");

  EXPECT_THROW(producer.bind("typed_blackboard.other_producer"), std::logic_error);
  EXPECT_THROW(consumer.bind("typed_blackboard.other_consumer"), std::logic_error);
}

} // namespace
