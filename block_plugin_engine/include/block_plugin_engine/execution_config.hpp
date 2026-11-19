#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace bpe {

enum class ExecutionPolicy { TIMER, EVENT };

struct ExecutionConfig {
  ExecutionPolicy policy{ExecutionPolicy::TIMER};
  std::chrono::nanoseconds period{0};
  std::chrono::nanoseconds offset{0};
  std::size_t thread{0};
};

} // namespace bpe
