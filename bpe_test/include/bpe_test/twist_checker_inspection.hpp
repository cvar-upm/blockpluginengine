#pragma once

#include <cstddef>

namespace bpe_test {

class ITwistCheckerInspection {
public:
  virtual ~ITwistCheckerInspection() = default;

  virtual std::size_t checked_samples() const noexcept = 0;
  virtual std::size_t errors() const noexcept = 0;
  virtual std::size_t missed_samples() const noexcept = 0;
};

} // namespace bpe_test
