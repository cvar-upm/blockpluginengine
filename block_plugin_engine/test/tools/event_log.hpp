#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace bpe::test {

void clear_events();
void record_event(const std::string& event);
std::vector<std::string> events();

// Wait until at least count events have been recorded.
bool wait_for_events(std::size_t count, std::chrono::milliseconds timeout);

} // namespace bpe::test