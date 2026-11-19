#pragma once

#include <memory>
#include <string_view>

namespace bpe {

// A notification is a hint to execute a pipeline; no payload is transported.
class IEventSignal {
public:
  virtual ~IEventSignal() = default;
  virtual bool signal() noexcept = 0;
};

// Lookup is intended for configure(), never for the real-time execute() path.
class IEventSignalProvider {
public:
  virtual ~IEventSignalProvider() = default;
  virtual std::shared_ptr<IEventSignal> get_signal(std::string_view name) = 0;
};

// Optional block capability: obtain any number of named emitters on configure().
class IEventSignalUser {
public:
  virtual ~IEventSignalUser() = default;
  virtual void set_event_signal_provider(IEventSignalProvider& provider) = 0;
};

} // namespace bpe
