#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace bpe::test {

void clear_events();
void record_event(const std::string& event);
std::vector<std::string> events();

// Wait until a specific record is present (other lifecycle records may interleave).
bool wait_for_event(const std::string& name, std::chrono::milliseconds timeout);
bool wait_for_event_count(const std::string& name, std::size_t count,
                          std::chrono::milliseconds timeout);

// Wait until at least count events have been recorded.
bool wait_for_events(std::size_t count, std::chrono::milliseconds timeout);

} // namespace bpe::test