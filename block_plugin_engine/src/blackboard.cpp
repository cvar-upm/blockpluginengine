#include "block_plugin_engine/blackboard.hpp"

#include <memory>

#include "detail/runtime_state.hpp"

namespace bpe {
namespace {

std::unique_ptr<BpeBlackboard>& blackboard_instance() {
  static std::unique_ptr<BpeBlackboard> instance = std::make_unique<BpeBlackboard>();
  return instance;
}

} // namespace

BpeBlackboard& the_blackboard() {
  return *blackboard_instance();
}

namespace detail {

void reset_blackboard() {
  blackboard_instance() = std::make_unique<BpeBlackboard>();
}

} // namespace detail
} // namespace bpe
