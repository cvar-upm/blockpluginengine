#pragma once

#include <stdexcept>

namespace bpe::lfbb {

template <typename ProtectedData>
ProducerGrantFactory<ProtectedData>
Blackboard<ProtectedData>::register_producer(const std::string& label) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto [iterator, inserted] = entries_.try_emplace(label);
  (void)inserted;

  if (iterator->second.has_producer) {
    throw std::runtime_error("Producer already registered for label");
  } else {
    iterator->second.has_producer = true;
  }

  return ProducerGrantFactory<ProtectedData>(&(iterator->second.ring));
}

template <typename ProtectedData>
ConsumerGrantFactory<ProtectedData>
Blackboard<ProtectedData>::register_consumer(const std::string& label) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto [iterator, inserted] = entries_.try_emplace(label);
  (void)inserted;
  iterator->second.ring.register_consumer();

  return ConsumerGrantFactory<ProtectedData>(&(iterator->second.ring));
}

template <typename ProtectedData> bool Blackboard<ProtectedData>::all_have_producer() const {
  std::lock_guard<std::mutex> lock(mutex_);

  bool all_registered = true;
  for (const auto& [label, entry] : entries_) {
    (void)label;
    if (!entry.has_producer) {
      all_registered = false;
    }
  }

  return all_registered;
}

template <typename ProtectedData>
std::vector<std::string> Blackboard<ProtectedData>::labels_without_producer() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::string> result;
  result.reserve(entries_.size());

  for (const auto& [label, entry] : entries_) {
    if (!entry.has_producer) {
      result.push_back(label);
    }
  }

  return result;
}

} // namespace bpe::lfbb
