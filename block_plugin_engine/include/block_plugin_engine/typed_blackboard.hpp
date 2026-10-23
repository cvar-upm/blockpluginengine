#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <rclcpp/serialization.hpp>
#include <rosidl_runtime_cpp/traits.hpp>

#include "bpe/lfbb/consumer_grant.hpp"
#include "bpe/lfbb/consumer_grant_factory.hpp"
#include "bpe/lfbb/producer_grant.hpp"
#include "bpe/lfbb/producer_grant_factory.hpp"
#include "block_plugin_engine/blackboard.hpp"
#include "block_plugin_engine/ros_type_registry.hpp"

namespace bpe {

namespace detail {

template <typename T> class RosProtectedDataInitializer {
public:
  void initialize(RosProtectedData& data) const {
    data.native = std::make_shared<T>();
  }
};

template <typename T>
void serialize_native(const void* native, rclcpp::SerializedMessage* serialized) {
  static const rclcpp::Serialization<T> serialization;
  serialization.serialize_message(native, serialized);
}

template <typename T>
void deserialize_native(const rclcpp::SerializedMessage* serialized, void* native) {
  static const rclcpp::Serialization<T> serialization;
  serialization.deserialize_message(serialized, native);
}

template <typename T>
const RosTypeInfo& register_typed_ros_type(const std::string& label) {
  return the_ros_type_registry().register_typed(
      label,
      rosidl_generator_traits::name<T>(),
      &serialize_native<T>,
      &deserialize_native<T>);
}

using ProducerFactory = decltype(
    std::declval<BpeBlackboard&>().register_producer(std::declval<const std::string&>()));
using ConsumerFactory = decltype(
    std::declval<BpeBlackboard&>().register_consumer(std::declval<const std::string&>()));
using ProducerGrant = decltype(std::declval<ProducerFactory&>().write_access());
using ConsumerGrant = decltype(std::declval<ConsumerFactory&>().read_access());

} // namespace detail

template <typename T> class TypedProducer;
template <typename T> class TypedConsumer;
template <typename... Ts> class MultiConsumer;
template <typename... Ts> class MultiProducer;
template <typename... Ts> class MultiConsumerAccess;
template <typename... Ts> class MultiProducerAccess;
namespace detail {
template <std::size_t I, typename T> class ConsumerAccessElement;
template <std::size_t I, typename T> class ProducerAccessElement;
template <typename Indices, typename... Ts> class ConsumerAccessStorage;
template <typename Indices, typename... Ts> class ProducerAccessStorage;
} // namespace detail

template <typename T> class TypedProducerAdapter {
public:
  ~TypedProducerAdapter() noexcept(false) {
    if (type_info_->requires_serialization()) {
      try {
        type_info_->serialize(grant_.data().native.get(), grant_.data().serialized);
      } catch (...) {
        grant_.cancel();
        throw;
      }
    }
  }

  T& value() noexcept {
    return *static_cast<T*>(grant_.data().native.get());
  }

  TypedProducerAdapter(const TypedProducerAdapter&) = delete;
  TypedProducerAdapter& operator=(const TypedProducerAdapter&) = delete;
  TypedProducerAdapter(TypedProducerAdapter&&) = delete;
  TypedProducerAdapter& operator=(TypedProducerAdapter&&) = delete;

private:
  TypedProducerAdapter(detail::ProducerFactory& factory, const RosTypeInfo& type_info)
    : grant_(factory.write_access()), type_info_(&type_info) {}

  detail::ProducerGrant grant_;
  const RosTypeInfo* type_info_{nullptr};

  friend class TypedProducer<T>;
  template <std::size_t I, typename U> friend class detail::ProducerAccessElement;
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
  template <std::size_t I, typename U> friend class detail::ConsumerAccessElement;
};

template <typename T> class TypedProducer {
public:
  TypedProducer() = default;
  ~TypedProducer() = default;

  void bind(const std::string& label) {
    if (binding_.has_value()) {
      throw std::logic_error("TypedProducer already bound");
    } else {
      binding_.emplace(label);
    }
  }

  TypedProducerAdapter<T> write_access() {
    if (!binding_.has_value()) {
      throw std::logic_error("TypedProducer is not bound");
    }

    return TypedProducerAdapter<T>(binding_->factory, *binding_->type_info);
  }

  TypedProducer(const TypedProducer&) = delete;
  TypedProducer& operator=(const TypedProducer&) = delete;
  TypedProducer(TypedProducer&&) = delete;
  TypedProducer& operator=(TypedProducer&&) = delete;

private:
  struct Binding {
    explicit Binding(const std::string& label)
      : type_info(&detail::register_typed_ros_type<T>(label)),
        factory(the_blackboard().register_producer(
          label,
          detail::RosProtectedDataInitializer<T>{})) {}

    const RosTypeInfo* type_info;
    detail::ProducerFactory factory;
  };

  std::optional<Binding> binding_;

  template <typename Indices, typename... Us> friend class detail::ProducerAccessStorage;
  template <typename... Us> friend class MultiProducer;
};

template <typename T> class TypedConsumer {
public:
  TypedConsumer() = default;
  ~TypedConsumer() = default;

  void bind(const std::string& label) {
    if (binding_.has_value()) {
      throw std::logic_error("TypedConsumer already bound");
    } else {
      binding_.emplace(label);
    }
  }

  TypedConsumerAdapter<T> read_access() {
    if (!binding_.has_value()) {
      throw std::logic_error("TypedConsumer is not bound");
    }

    return TypedConsumerAdapter<T>(binding_->factory);
  }

  TypedConsumer(const TypedConsumer&) = delete;
  TypedConsumer& operator=(const TypedConsumer&) = delete;
  TypedConsumer(TypedConsumer&&) = delete;
  TypedConsumer& operator=(TypedConsumer&&) = delete;

private:
  struct Binding {
    explicit Binding(const std::string& label)
      : type_info(&detail::register_typed_ros_type<T>(label)),
        factory(the_blackboard().register_consumer(
          label,
          detail::RosProtectedDataInitializer<T>{})) {}

    const RosTypeInfo* type_info;
    detail::ConsumerFactory factory;
  };

  std::optional<Binding> binding_;

  template <typename Indices, typename... Us> friend class detail::ConsumerAccessStorage;
  template <typename... Us> friend class MultiConsumer;
};


// Each element constructs its non-movable adapter directly as a base subobject.
namespace detail {

template <std::size_t I, typename T>
class ConsumerAccessElement : public TypedConsumerAdapter<T> {
public:
  explicit ConsumerAccessElement(ConsumerFactory& factory)
      : TypedConsumerAdapter<T>(factory) {}
};

template <std::size_t I, typename T>
class ProducerAccessElement : public TypedProducerAdapter<T> {
public:
  ProducerAccessElement(ProducerFactory& factory, const RosTypeInfo& info)
      : TypedProducerAdapter<T>(factory, info) {}
};

template <typename Indices, typename... Ts> class ConsumerAccessStorage;
template <std::size_t... Is, typename... Ts>
class ConsumerAccessStorage<std::index_sequence<Is...>, Ts...>
    : private ConsumerAccessElement<Is, Ts>... {
public:
  explicit ConsumerAccessStorage(TypedConsumer<Ts>&... consumers)
      : ConsumerAccessElement<Is, Ts>(consumers.binding_->factory)... {}

  template <std::size_t I>
  decltype(auto) get() noexcept {
    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
    return static_cast<TypedConsumerAdapter<T>&>(
        static_cast<ConsumerAccessElement<I, T>&>(*this));
  }

  template <std::size_t I>
  decltype(auto) get() const noexcept {
    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
    return static_cast<const TypedConsumerAdapter<T>&>(
        static_cast<const ConsumerAccessElement<I, T>&>(*this));
  }
};

template <typename Indices, typename... Ts> class ProducerAccessStorage;
template <std::size_t... Is, typename... Ts>
class ProducerAccessStorage<std::index_sequence<Is...>, Ts...>
    : private ProducerAccessElement<Is, Ts>... {
public:
  explicit ProducerAccessStorage(TypedProducer<Ts>&... producers)
      : ProducerAccessElement<Is, Ts>(producers.binding_->factory,
                                       *producers.binding_->type_info)... {}

  template <std::size_t I>
  decltype(auto) get() noexcept {
    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
    return static_cast<TypedProducerAdapter<T>&>(
        static_cast<ProducerAccessElement<I, T>&>(*this));
  }
};

} // namespace detail

template <typename... Ts>
class MultiConsumerAccess
    : public detail::ConsumerAccessStorage<std::index_sequence_for<Ts...>, Ts...> {
  using Base = detail::ConsumerAccessStorage<std::index_sequence_for<Ts...>, Ts...>;
public:
  explicit MultiConsumerAccess(TypedConsumer<Ts>&... consumers) : Base(consumers...) {}
  using Base::get;
  MultiConsumerAccess(const MultiConsumerAccess&) = delete;
  MultiConsumerAccess& operator=(const MultiConsumerAccess&) = delete;
  MultiConsumerAccess(MultiConsumerAccess&&) = delete;
  MultiConsumerAccess& operator=(MultiConsumerAccess&&) = delete;
};

template <typename... Ts>
class MultiProducerAccess
    : public detail::ProducerAccessStorage<std::index_sequence_for<Ts...>, Ts...> {
  using Base = detail::ProducerAccessStorage<std::index_sequence_for<Ts...>, Ts...>;
public:
  explicit MultiProducerAccess(TypedProducer<Ts>&... producers) : Base(producers...) {}
  using Base::get;
  MultiProducerAccess(const MultiProducerAccess&) = delete;
  MultiProducerAccess& operator=(const MultiProducerAccess&) = delete;
  MultiProducerAccess(MultiProducerAccess&&) = delete;
  MultiProducerAccess& operator=(MultiProducerAccess&&) = delete;
};

template <typename... Ts> class MultiConsumer {
public:
  template <typename... Labels>
  void bind(const Labels&... labels) {
    static_assert(sizeof...(Labels) == sizeof...(Ts), "One label per consumer required");
    bind_impl(std::index_sequence_for<Ts...>{}, labels...);
  }

  MultiConsumerAccess<Ts...> read_access() {
    std::apply([](auto&... consumers) {
      if (!(... && consumers.binding_.has_value()))
        throw std::logic_error("MultiConsumer contains an unbound consumer");
    }, consumers_);
    return std::apply([](auto&... consumers) {
      return MultiConsumerAccess<Ts...>(consumers...);
    }, consumers_);
  }

private:
  template <std::size_t... Is, typename... Labels>
  void bind_impl(std::index_sequence<Is...>, const Labels&... labels) {
    (std::get<Is>(consumers_).bind(labels), ...);
  }
  std::tuple<TypedConsumer<Ts>...> consumers_;
};

template <typename... Ts> class MultiProducer {
public:
  template <typename... Labels>
  void bind(const Labels&... labels) {
    static_assert(sizeof...(Labels) == sizeof...(Ts), "One label per producer required");
    bind_impl(std::index_sequence_for<Ts...>{}, labels...);
  }

  MultiProducerAccess<Ts...> write_access() {
    std::apply([](auto&... producers) {
      if (!(... && producers.binding_.has_value()))
        throw std::logic_error("MultiProducer contains an unbound producer");
    }, producers_);
    return std::apply([](auto&... producers) {
      return MultiProducerAccess<Ts...>(producers...);
    }, producers_);
  }

private:
  template <std::size_t... Is, typename... Labels>
  void bind_impl(std::index_sequence<Is...>, const Labels&... labels) {
    (std::get<Is>(producers_).bind(labels), ...);
  }
  std::tuple<TypedProducer<Ts>...> producers_;
};

} // namespace bpe
