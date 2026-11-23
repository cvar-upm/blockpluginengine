# Blackboard: commit before signalling an event

> **CRITICAL — FOR DEVELOPERS AND AI CODE GENERATORS**
>
> **Finish the `write_access()` scope before calling `IEventSignal::signal()`.**
> The signal requests activation of a pipeline. It does **not** commit a
> Blackboard write. The grant commits when it is destroyed (RAII). Emitting
> the event while a write grant is still alive may wake a reader that sees
> an older, already committed version. The notification may be coalesced,
> so a second wake-up is not guaranteed.

Correct:

```cpp
{
  auto grant = producer_.write_access();
  update_in_place(grant.value());
}  // Grant destroyed; the new version is now committed.
signal_->signal();
```

Incorrect:

```cpp
auto grant = producer_.write_access();
update_in_place(grant.value());
signal_->signal();  // The new version has not yet been committed!
```

**Do not copy data merely to shorten grant lifetime.** A Blackboard grant is
not a conventional mutex critical section. Producers may perform useful
computation *in-place* in the reserved slot, and consumers may process their
stable snapshot directly. Avoid carrying grants across unrelated, blocking
operations, but do not introduce temporary copies just to release them early.

The Blackboard stores the latest committed version, not a queue of all
versions. An EVENT pipeline can coalesce notifications; a periodic reader
may skip intermediate versions too. Use `has_new_value()` (and missed-version
indicators where relevant), and never assume one callback per write.

The concurrency integration test
`bpe_test/test/test_05_multischeduler_blackboard_concurrency.cpp` runs one
periodic writer and two readers in **three independent scheduler threads**:
one EVENT reader and one TIMER reader. The test generates at least 10,000
self-checking versions, verifies each reader's sequence and checksum, and
requires both readers to reach the last version. Although an EVENT reader is
woken after the current commit, it can run concurrently with a **subsequent**
write; it is therefore still a useful concurrency stress workload.
