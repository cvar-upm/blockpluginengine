#include "block_plugin_engine/blackboard.hpp"

namespace bpe {

BpeBlackboard& the_blackboard() {
  static BpeBlackboard instance;
  return instance;
}

} // namespace bpe
