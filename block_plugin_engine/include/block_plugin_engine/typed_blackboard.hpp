#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "bpe/lfbb/consumer_grant.hpp"
#include "bpe/lfbb/consumer_grant_factory.hpp"
#include "bpe/lfbb/producer_grant.hpp"
#include "bpe/lfbb/producer_grant_factory.hpp"
#include "block_plugin_engine/blackboard.hpp"

namespace bpe {

namespace detail {

template <typename T> class RosProtectedDataInitializer {
public:
  void initialize(RosProtectedData& data) const {
    data.native = std::make_shared<T>();
  }
};

using ProducerFactory = decltype(
    std::declval<BpeBlackboard&>().register_producer(std::declval<const std::string&>()));
using ConsumerFactory = decltype(
    std::declval<BpeBlackboard&>().register_consumer(std::declval<const std::string&>()));
using ProducerGrant = decltype(std::declval<ProducerFactory&>().write_access());
using ConsumerGrant = decltype(std::declval<ConsumerFactory&>().read_access());

} // namespace detail

template <typename T> class TypedProducer;
template <typename T> class TypedConsumer;

template <typename T> class TypedProducerAdapter {
public:
  ~TypedProducerAdapter() = default;

  T& value() noexcept {
    return *static_cast<T*>(grant_.data().native.get());
  }

  TypedProducerAdapter(const TypedProducerAdapter&) = delete;
  TypedProducerAdapter& operator=(const TypedProducerAdapter&) = delete;
  TypedProducerAdapter(TypedProducerAdapter&&) = delete;
  TypedProducerAdapter& operator=(TypedProducerAdapter&&) = delete;

private:
  explicit TypedProducerAdapter(detail::ProducerFactory& factory)
    : grant_(factory.write_access()) {}

  detail::ProducerGrant grant_;

  friend class TypedProducer<T>;
};

template <typename T> class TypedConsumerAdapter {
public:
  ~TypedConsumerAdapter() = default;

  const T& value() const noexcept {
    return *static_cast<const T*>(grant_.data().native.get());
  }

  std::uint64_t sequence() const noexcept {
    return grant_.sequence();
  }

  std::uint64_t timestamp_ns() const noexcept {
    return grant_.timestamp_ns();
  }

  lfbb::ReadStatus status() const noexcept {
    return grant_.status();
  }

  bool has_new_value() const noexcept {
    return grant_.has_new_value();
  }

  bool has_missed() const noexcept {
    return grant_.has_missed();
  }

  std::uint64_t missed() const noexcept {
    return grant_.missed();
  }

  TypedConsumerAdapter(const TypedConsumerAdapter&) = delete;
  TypedConsumerAdapter& operator=(const TypedConsumerAdapter&) = delete;
  TypedConsumerAdapter(TypedConsumerAdapter&&) = delete;
  TypedConsumerAdapter& operator=(TypedConsumerAdapter&&) = delete;

private:
  explicit TypedConsumerAdapter(detail::ConsumerFactory& factory) : grant_(factory.read_access()) {}

  detail::ConsumerGrant grant_;

  friend class TypedConsumer<T>;
};

template <typename T> class TypedProducer {
public:
  explicit TypedProducer(const std::string& label)
    : factory_(
        the_blackboard().register_producer(label, detail::RosProtectedDataInitializer<T>{})) {}

  ~TypedProducer() = default;

  TypedProducerAdapter<T> write_access() {
    return TypedProducerAdapter<T>(factory_);
  }

  TypedProducer(const TypedProducer&) = delete;
  TypedProducer& operator=(const TypedProducer&) = delete;
  TypedProducer(TypedProducer&&) = delete;
  TypedProducer& operator=(TypedProducer&&) = delete;

private:
  detail::ProducerFactory factory_;
};

template <typename T> class TypedConsumer {
public:
  explicit TypedConsumer(const std::string& label)
    : factory_(
        the_blackboard().register_consumer(label, detail::RosProtectedDataInitializer<T>{})) {}

  ~TypedConsumer() = default;

  TypedConsumerAdapter<T> read_access() {
    return TypedConsumerAdapter<T>(factory_);
  }

  TypedConsumer(const TypedConsumer&) = delete;
  TypedConsumer& operator=(const TypedConsumer&) = delete;
  TypedConsumer(TypedConsumer&&) = delete;
  TypedConsumer& operator=(TypedConsumer&&) = delete;

private:
  detail::ConsumerFactory factory_;
};

} // namespace bpe
