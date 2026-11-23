# Blackboard access: modify in place, do not copy for synchronization

> **IMPORTANT — DESIGN CONTRACT FOR HUMAN DEVELOPERS AND AI CODE GENERATORS**
>
> **Do not copy a Blackboard value into a temporary object merely to shorten a read or write access.** This Blackboard is not a mutex-protected shared object. Its grant reserves a slot in a concurrent ring. A producer edits its **uncommitted slot in place**; a reader retains a **stable committed slot** for the full duration of its access. Copying the payload and writing it back adds unnecessary operations and latency and can increase allocation/serialization work.
>
> **Keep the access for the processing that actually needs the data.** Do not reduce its scope artificially using patterns designed for conventional mutexes. Do not retain grants across unrelated blocking I/O, indefinite waits, or asynchronous tasks; release them at a sensible processing boundary.

## Why the conventional mutex pattern is wrong here

In a conventional critical section, minimizing time under a lock may be important to reduce contention. BPE's ring does not impose that lock on readers or the producer during ordinary data access. It reserves distinct slots and maintains enough free capacity for the configured topology. A writer's changes become visible only when its grant is destroyed (RAII commit). A reader retains a stable committed version until its grant is destroyed.

**Preferred** — operate directly on the protected object:

```cpp
{
  auto access = producer_.write_access();
  auto& value = access.value();
  value.samples.resize(100);
  compute_samples(value.samples);  // Work in place while the grant is alive.
} // Commit once; no payload copy needed.
```

**Avoid** — a temporary copy used only to shorten the grant:

```cpp
Sample temporary;
compute_samples(temporary.samples);
{
  auto access = producer_.write_access();
  access.value() = std::move(temporary); // Unnecessary intermediate payload.
}
```

The second form may be appropriate if the object is created by a separate API or if an algorithm explicitly needs an independent buffer. It must **not** be the automatic default for "thread safety". The same principle applies to consumers: read and process the `const` object directly when possible rather than copying it to an intermediate snapshot solely to release the grant earlier.

## Constraints

- Configure registrations before execution: **one producer per label**, multiple separately registered consumers, and one active grant per endpoint.
- Write grants commit upon destruction; do not emit an event indicating availability **before** the write grant has been destroyed and committed.
- Read grants protect the selected version; they do not promise that it is the newest value by the time processing finishes. The Blackboard uses **latest-value**, not queued-message, semantics; inspect `has_new_value()` and `has_missed()` as needed.
- No operation on an external resource is automatically safe just because a Blackboard grant is held. Avoid indefinite waits or unrelated slow/blocking operations inside grant scope, especially in scheduler callbacks.
- A producer must complete all fields of a consistent sample before allowing the write grant to commit.

## Regression coverage

`bpe_test/test/test_04_blackboard_concurrency.cpp` forces two important cross-thread interleavings: a reader observes the last committed version while a producer holds a partially updated grant, and a producer continues publishing while a reader retains its earlier committed version. The latter is specifically a progress check while the read grant remains open.

These tests exercise the real typed Blackboard adapters with independent threads and explicit handshakes. They **do not yet** validate `MultiScheduler` scheduling or event-signal routing; a separate integration test should drive that scenario so that scheduler failures remain distinguishable from Blackboard correctness failures.
