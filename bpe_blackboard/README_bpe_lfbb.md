# BPE Blackboard

## 1. Purpose

This package provides the concurrent blackboard infrastructure used by the Block Plugin Engine (BPE).

It is implemented as a **ROS 2 package containing only a header-only C++ library**.

There are intentionally:

- no ROS 2 nodes;
- no executables;
- no runtime processes;
- no plugin implementations in this package.

ROS 2 is only relevant here as the build/package ecosystem in which the library is distributed. The concurrency infrastructure itself must remain independent from ROS 2 concepts unless a higher BPE-specific layer explicitly introduces them.

The namespace for the library is:

```cpp
namespace bpe::lfbb {
}
```

Implementation details that are not part of the public API live under:

```cpp
namespace bpe::lfbb::details {
}
```

---

## 2. Starting point

The existing `lfbb_blackboard` implementation is considered **stable and frozen**.

It will not be generalized or adapted for the new BPE requirements.

Instead, the new implementation will be developed independently inside the Block Plugin Engine repository, using the previous blackboard as:

- a behavioural reference;
- a source of proven concurrency algorithms;
- a reference for tests and invariants;
- a model from which responsibilities will be progressively separated.

The new design will be reconstructed **bottom-up**, beginning with the concurrent slot and moving upward through the ring, grants, factories, registration and final blackboard facade.

The goal is not to copy the old implementation structurally. The goal is to preserve its proven concurrency semantics while decomposing the responsibilities into smaller reusable components.

---

## 3. Main design principle

The previous implementation coupled two independent concerns:

1. the mechanism that protects concurrent access to a slot;
2. the concrete representation of the data stored in that slot.

The new design separates them.

The core abstraction becomes conceptually:

```cpp
template<typename ProtectedData>
class ConcurrentSlot;
```

The slot provides the concurrency guarantees.

`ProtectedData` is simply the object protected by the slot.

The concurrency mechanism must not depend on how `ProtectedData` is represented.

Conceptually:

```text
ConcurrentSlot<ProtectedData>
        |
        | provides concurrency protection
        v
   ProtectedData
```

This is the fundamental abstraction on which the rest of the design is built.

---

## 4. Composition instead of inheritance

The new blackboard is designed primarily through **composition**.

Different responsibilities must be implemented by different classes or policy objects.

Some components will be templates over the protected data type. Others may be templates over the ring, access mechanism or other policy types.

The final `Blackboard` object will compose these pieces into a convenient API.

The design must avoid introducing a single template parameter that is mechanically propagated everywhere unless each component genuinely needs it.

Each class should depend only on the abstractions required for its own responsibility.

Conceptually:

```text
                    Blackboard
                        |
                     composition
                        |
        +---------------+---------------+
        |               |               |
   registration     access API      factories/grants
        |                               |
        +---------------+---------------+
                        |
                 ConcurrentRing
                        |
                 ConcurrentSlot
                        |
                  ProtectedData
```

The exact shape may evolve during implementation. The separation of responsibilities is more important than preserving this exact diagram.

---

## 5. Concurrency vocabulary

The prefix `Concurrent` is reserved for components whose main responsibility is to provide or coordinate thread-safe concurrent access.

The initial terminology is:

```text
ConcurrentSlot
ConcurrentRing

ProducerGrant
ConsumerGrant

ProducerGrantFactory
ConsumerGrantFactory

TypedAccessor
NativeAccessor
SerializedAccessor

ProtectedDataInitializer

Blackboard
```

The naming rules are:

- `Concurrent*` — provides or coordinates concurrency;
- `*Grant` — owns a temporary access reservation using RAII;
- `*Accessor` — interprets or projects protected data;
- `*Initializer` — prepares protected storage;
- `*Factory` — creates grants or equivalent access objects;
- `Blackboard` — final facade and composition root for the data-sharing API.

---

## 6. `ProtectedData`

The object contained by a `ConcurrentSlot` is referred to conceptually as `ProtectedData`.

This name expresses the role of the object: it is the data whose concurrent access is being protected.

`ProtectedData` is **not itself responsible for thread safety**.

The concurrency guarantees belong to `ConcurrentSlot`.

For example:

```cpp
template<typename ProtectedData>
class ConcurrentSlot
{
    ProtectedData data_;
};
```

A `ProtectedData` implementation may be a simple object, an aggregate or a more complex structure.

For BPE, the protected data is expected eventually to contain at least two coherent representations of the same logical sample:

```text
ProtectedData
├── native representation
└── serialized representation
```

These are not independent messages. They are two representations of the same sample protected by the same slot.

---

## 7. `ConcurrentSlot`

`ConcurrentSlot` is the fundamental concurrency primitive.

It is responsible for protecting one `ProtectedData` instance.

Its behaviour must be derived from the proven semantics of the previous `SD_RingSlot`.

Its responsibilities include:

- writer acquisition;
- writer release;
- reader acquisition;
- reader release;
- reader count management;
- writer ownership state;
- sequence metadata;
- timestamp metadata.

The concurrency state and the protected data are separate concerns:

```text
ConcurrentSlot<ProtectedData>
├── concurrency state
│   ├── writer ownership
│   └── reader count
│
├── sequence metadata
├── timestamp metadata
└── ProtectedData
```

The concurrency algorithm must not inspect or depend on the internals of `ProtectedData`.

The first implementation should reproduce the concurrency behaviour of the previous blackboard before introducing BPE-specific storage.

---

## 8. `ConcurrentRing`

`ConcurrentRing` manages a circular collection of concurrent slots.

The abbreviated name `ConcurrentRing` is preferred over `ConcurrentRingBuffer`.

Its responsibilities are expected to include the behaviour previously associated with the shared ring state:

- ownership of the slot collection;
- ring capacity;
- power-of-two sizing;
- index mask;
- next write position;
- last committed position;
- selection of a writable slot;
- publication of the last committed slot;
- retrieval of the latest committed slot.

Conceptually:

```text
ConcurrentRing
    |
    +-- ConcurrentSlot
    +-- ConcurrentSlot
    +-- ConcurrentSlot
    +-- ...
```

The ring must not interpret the application payload stored inside the slots.

It coordinates slot selection and publication only.

The ring also owns the configuration-time accounting required to size and initialize its slots. Its template form is:

```cpp
template<typename ProtectedData, std::size_t InitializationMargin>
class ConcurrentRing;
```

`InitializationMargin` is not part of the wait-free algorithm. It expresses how many additional producer/consumer registrations may be accepted after an initialization point without supplying an initializer again.

The wait-free slot reserve is a separate invariant of the ring algorithm:

```cpp
static constexpr std::size_t kWaitFreeSlotMargin = 2;
```

One slot may be the latest committed sample while another must remain available for the next producer acquisition. Therefore physical capacity is derived from the maximum number of consumers plus this fixed wait-free margin. The producer is tracked as a registration endpoint but does not add another term to the wait-free capacity formula.

---

## 9. Grants

`ProducerGrant` and `ConsumerGrant` remain the RAII objects that represent temporary access rights.

Their main responsibility is **not type erasure**.

Their responsibility is to preserve a slot reservation for the lifetime of the grant.

A grant should therefore expose access to the protected object while maintaining the concurrency reservation.

Conceptually:

```text
ProducerGrant
    |
    +-- owns writer reservation
    |
    +-- gives mutable access to ProtectedData
```

and:

```text
ConsumerGrant
    |
    +-- owns reader reservation
    |
    +-- gives const access to ProtectedData
```

The intended semantics are therefore approximately:

```cpp
ProtectedData& ProducerGrant::data();
const ProtectedData& ConsumerGrant::data() const;
```

The exact public API will be fixed during implementation.

### 9.1. Producer grant

A producer grant reserves a slot for writing.

It gives mutable access to the protected data.

The reservation remains active until the grant is released or destroyed.

The logical sample is committed once.

### 9.2. Consumer grant

A consumer grant reserves the latest committed slot for reading.

It gives immutable access to the protected data.

The reservation remains active until the grant is released or destroyed.

---

## 10. Accessors

Type interpretation is deliberately separated from grants.

An accessor operates on a `ProtectedData` object and exposes one logical representation of it.

Conceptually:

```text
Grant
  |
  v
ProtectedData
  |
  +-- TypedAccessor<T> ----> T
  |
  +-- NativeAccessor ------> type-erased native representation
  |
  +-- SerializedAccessor --> serialized representation
```

The accessors may live entirely in BPE and do not need to be part of the generic concurrency infrastructure.

This keeps the blackboard generic while allowing BPE to define multiple views over the same protected sample.

### 10.1. `TypedAccessor<T>`

Provides access to the native representation as a known compile-time type `T`.

Conceptually:

```cpp
T& get(ProtectedData&);
const T& get(const ProtectedData&);
```

### 10.2. `NativeAccessor`

Provides access to the native representation without knowing `T` statically.

This is intended for runtime type-erased operations such as generic ROS deserialization.

Conceptually it may expose:

```cpp
void*;
const void*;
```

or an equivalent type-erased representation.

### 10.3. `SerializedAccessor`

Provides access to the serialized representation.

This is intended primarily for generic BPE boundary plugins.

For example:

```text
ROS publisher
    |
    v
ConsumerGrant
    |
    v
SerializedAccessor
    |
    v
serialized message
```

---

## 11. Initializers

Access and initialization are separate responsibilities.

Accessors interpret existing protected data.

They should not be responsible for constructing or materializing the storage.

A separate initialization mechanism is therefore expected.

The initial name is:

```text
ProtectedDataInitializer
```

Its responsibility is to prepare a `ProtectedData` instance for a concrete logical type.

For example, a BPE-specific initializer may create the native object associated with a compile-time `T` while preparing any additional representation required by the slot.

The generic blackboard does not own or retain the initializer type. Initialization is supplied only at registration time through a templated overload. The initializer contract is deliberately minimal:

```cpp
initializer.initialize(protected_data);
```

where `protected_data` is passed by reference.

Both producer and consumer registration have two forms:

```cpp
register_producer(...);
register_producer(..., initializer);

register_consumer(...);
register_consumer(..., initializer);
```

Registrations performed before the first initialization may omit the initializer. The first registration that supplies an initializer prepares every slot in the ring and reserves `InitializationMargin` future registrations. After that point, registrations without an initializer consume that margin. If the margin is exhausted, another registration without an initializer is rejected.

A later registration that supplies an initializer may expand the ring. Only newly created slots are initialized in that case, and the initialization margin is restored from that new configuration point. Existing initialized slots are never reinitialized merely because the ring grows.

This mechanism deliberately knows nothing about the logical type `T`. A BPE-specific initializer may know `T`, construct a native object or prepare any other representation, but `bpe_blackboard` only invokes the standard initialization callback on `ProtectedData`.

---

## 12. BPE protected data model

The expected BPE-specific protected data model contains two coherent representations of the same sample:

```text
ProtectedData
├── native
└── serialized
```

The native representation is the representation used by ordinary typed BPE blocks.

The serialized representation is the representation used by generic ROS boundaries.

These two pieces belong to the same slot and therefore share:

- the same writer reservation;
- the same reader reservations;
- the same sequence;
- the same timestamp;
- the same commit operation.

There must never be separate commits for native and serialized data.

---

## 13. Sample atomicity invariant

The most important semantic invariant is:

> One slot represents one logical sample.

Therefore:

```text
claim slot
    |
    +-- update native representation
    |
    +-- update serialized representation
    |
commit slot
```

and:

```text
acquire slot
    |
    +-- inspect native representation
    |
    +-- inspect serialized representation
    |
release slot
```

A consumer must never observe a native representation belonging to one sample together with a serialized representation belonging to another.

This invariant is guaranteed structurally by protecting both representations with the same `ConcurrentSlot`.

---

## 14. ROS boundary semantics

The future generic ROS boundary follows the same conceptual rules already adopted in BPE.

### 14.1. ROS output

A ROS publisher is a **blackboard consumer**.

It does not serialize the message.

The producer that creates the native sample is responsible for ensuring that the serialized representation is coherent before the slot is committed.

The publisher flow is:

```text
typed producer
      |
      +-- native representation
      |
      +-- serialized representation
      |
      v
single commit
      |
      v
ConsumerGrant
      |
      v
SerializedAccessor
      |
      v
generic ROS publisher
```

### 14.2. ROS input

A ROS subscriber is a **blackboard producer**.

It receives serialized data and updates both representations before committing the slot.

Conceptually:

```text
generic ROS subscriber
        |
        v
ProducerGrant
        |
        +-- SerializedAccessor
        |
        +-- NativeAccessor
        |
        v
runtime deserialization
        |
        v
single commit
```

The native object is expected to have been created during configuration.

The runtime subscriber therefore deserializes into an already existing object.

---

## 15. Type erasure

Type erasure is not a responsibility of the concurrent slot or the grant.

It is performed through the accessor layer.

The normal typed BPE code knows `T` statically and uses:

```text
TypedAccessor<T>
```

A generic ROS boundary operates on a runtime type and uses type-erased/native or serialized accessors.

This separation keeps the concurrency machinery independent from the concrete type system used by ROS.

---

## 16. Previous `shared_ptr<T>` move assignment

The previous blackboard included producer operations that allowed replacing the stored `shared_ptr<T>` by move or assignment.

This functionality existed mainly to support ROS 2 callback messages that could be moved directly into a slot.

That optimization is not a design requirement for the new implementation.

The corresponding BPE plugins become unnecessary once generic boundaries operate through the new native/serialized protected-data model.

The new architecture should therefore not be constrained by compatibility with the previous `operator=(std::shared_ptr<T>)` API.

If ownership-transfer operations are later useful, they should be reintroduced deliberately as part of the new storage model.

---

## 17. Implementation strategy

Development proceeds bottom-up.

The existing blackboard remains unchanged and serves as the behavioural baseline.

### Phase 1 — `ConcurrentSlot`

Reimplement the concurrency semantics of the existing slot.

Use the simplest possible protected data representation initially.

Verify:

- writer acquisition;
- reader acquisition;
- release;
- contention;
- sequence;
- timestamp;
- invariants under concurrency.

No ROS concepts are introduced.

### Phase 2 — `ConcurrentRing`

Build the circular ring on top of `ConcurrentSlot`.

Reproduce:

- slot selection;
- last committed publication;
- latest-slot reading;
- capacity rules;
- power-of-two sizing;
- behaviour with several consumers.

Again, no BPE-specific representation is required yet.

### Phase 3 — Grants

Implement producer and consumer grants as pure RAII reservation objects.

Verify that grants:

- preserve reservations correctly;
- release exactly once;
- provide mutable or const access as appropriate;
- do not interpret the protected data.

### Phase 4 — Factories

Introduce:

```text
ProducerGrantFactory
ConsumerGrantFactory
```

Only after the grant semantics are stable.

Factories should create grants and manage any per-factory state required by the public API.

They should not become data interpretation mechanisms.

### Phase 5 — Registration and Blackboard facade

Build the channel/label registration mechanism and compose the previous pieces into:

```text
Blackboard
```

At this point the new implementation should reproduce the relevant behaviour of the existing blackboard.

### Phase 6 — BPE `ProtectedData`

Introduce the real BPE protected data structure containing:

```text
native + serialized
```

without changing the concurrency algorithms.

### Phase 7 — Accessors and initialization

Introduce:

```text
TypedAccessor<T>
NativeAccessor
SerializedAccessor
ProtectedDataInitializer
```

and validate the typed and runtime access paths.

### Phase 8 — Generic ROS boundaries

Integrate the new blackboard with generic ROS subscriptions and publishers.

The ROS-specific implementation belongs above the blackboard concurrency infrastructure.

---

## 18. Development rule: generalize only from real use

The implementation should avoid speculative policy abstractions.

A responsibility should be extracted into a policy, template parameter or independent component only when there is a real second use or a clear reason to vary it.

The preferred process is:

```text
implement concrete behaviour
        |
        v
identify repeated responsibility
        |
        v
extract abstraction
        |
        v
verify both uses
```

This avoids producing a large policy framework whose abstractions have only one implementation.

---

## 19. Validation against the previous blackboard

The existing `lfbb_blackboard` implementation is the reference for concurrency behaviour.

Where practical, equivalent scenarios should be executed against both implementations.

Relevant behaviour includes:

- data integrity;
- ordering guarantees;
- latest committed slot semantics;
- missed update detection;
- concurrent readers;
- producer/consumer contention;
- ring capacity behaviour;
- stress scenarios;
- mixed producer/consumer thread layouts.

The new structure may differ substantially, but the proven concurrency properties must be preserved.

---

## 20. Header-only implementation

The package is a **C++20 header-only library**.

Nearly all components are expected to be templates.

Declarations and implementations should nevertheless remain separated for readability.

The convention is:

```text
public_or_component.hpp
details/public_or_component_impl.hpp
```

The implementation file is included at the end of the corresponding declaration header.

Example:

```cpp
// concurrent_slot.hpp

namespace bpe::lfbb::details {

template<typename ProtectedData>
class ConcurrentSlot
{
    // declaration
};

}  // namespace bpe::lfbb::details

#include "bpe/lfbb/details/concurrent_slot_impl.hpp"
```

And:

```cpp
// details/concurrent_slot_impl.hpp

namespace bpe::lfbb::details {

template<typename ProtectedData>
ConcurrentSlot<ProtectedData>::ConcurrentSlot(...)
{
    ...
}

}  // namespace bpe::lfbb::details
```

The same convention should be applied consistently to the template components.

---

## 21. Public and private namespaces

Public API:

```cpp
namespace bpe::lfbb {
}
```

Private implementation:

```cpp
namespace bpe::lfbb::details {
}
```

Initial candidates for public API include:

```text
Blackboard
ProducerGrant
ConsumerGrant
ProducerGrantFactory
ConsumerGrantFactory
TypedAccessor
NativeAccessor
SerializedAccessor
```

Initial candidates for implementation detail include:

```text
ConcurrentSlot
ConcurrentRing
```

This classification is not immutable during the first implementation iterations.

In particular, `ConcurrentSlot` and `ConcurrentRing` may be tested directly even while remaining implementation details.

No public API should require users to explicitly name a type from `bpe::lfbb::details`.

---

## 22. Suggested include layout

A possible initial organization is:

```text
include/
└── bpe/
    └── lfbb/
        ├── blackboard.hpp
        ├── producer_grant.hpp
        ├── consumer_grant.hpp
        ├── producer_grant_factory.hpp
        ├── consumer_grant_factory.hpp
        ├── typed_accessor.hpp
        ├── native_accessor.hpp
        ├── serialized_accessor.hpp
        ├── protected_data_initializer.hpp
        │
        └── details/
            ├── concurrent_slot.hpp
            ├── concurrent_slot_impl.hpp
            ├── concurrent_ring.hpp
            ├── concurrent_ring_impl.hpp
            ├── blackboard_impl.hpp
            ├── producer_grant_impl.hpp
            ├── consumer_grant_impl.hpp
            ├── producer_grant_factory_impl.hpp
            └── consumer_grant_factory_impl.hpp
```

The exact set of files should emerge incrementally.

Files should not be created merely to satisfy this proposed tree.

---

## 23. ROS 2 package structure

This component is distributed as a ROS 2 package but contains only a library.

Conceptually:

```text
package.xml
CMakeLists.txt
include/
test/
README.md
```

There are:

- no `src/` files required for the header-only library;
- no node targets;
- no executable targets;
- no component registration;
- no lifecycle node;
- no runtime launch files required by this package.

CMake should expose an interface/header-only target suitable for normal downstream use.

The package may depend on ROS 2 libraries only when required by public BPE data/accessor types. The low-level concurrency components should remain independent from ROS 2.

---

## 24. C++ standard

The package uses **C++20**.

C++20 features may be used when they improve the design, including:

- concepts;
- requires clauses;
- type traits;
- modern template constraints.

Concepts should be used to express real semantic requirements discovered during implementation.

They should not be introduced merely to formalize trivial implementation details.

---

## 25. Wait-free semantics

The intended concurrency model is **wait-free**, preserving the progress guarantees of the original blackboard.

For this implementation iteration, that progress property is considered an established design requirement of the reference algorithm. Its detailed justification can be documented independently without changing the implementation work described here.

The design must preserve the important property of the original blackboard:

- a producer reserves a writable slot atomically;
- readers reserve a committed slot atomically;
- readers do not interfere with one another beyond the reader count;
- consumers read the latest committed sample;
- the ring capacity prevents the writer from exhausting all usable slots while readers hold reservations.

The fixed wait-free sizing rule is:

```text
next_power_of_two(max(4, consumer_count + kWaitFreeSlotMargin))
```

with:

```text
kWaitFreeSlotMargin = 2
```

The two slots have a structural meaning: one may be the latest committed slot while another must remain available for the next producer write. This constant is independent from `InitializationMargin`.

At an initialization point, the ring first computes the structural wait-free capacity and then adds the configured reserve for future registrations:

```text
base_capacity =
    max(4,
        consumer_count
        + kWaitFreeSlotMargin)

capacity =
    next_power_of_two(
        base_capacity
        + InitializationMargin)
```

`InitializationMargin` is therefore never absorbed by the structural minimum capacity. Every future registration is treated as a possible consumer for this reservation, which preserves the wait-free capacity bound in the worst case. Producer registration consumes the initialization-registration budget but the producer itself is not added to `consumer_count`.

---

## 26. Key invariants

The following invariants define the architecture and should be preserved throughout development:

1. `ConcurrentSlot` owns the concurrency protection.
2. `ProtectedData` does not implement its own synchronization.
3. `ConcurrentRing` coordinates slots but does not interpret their data.
4. Grants represent reservation lifetime only.
5. Accessors interpret protected data but do not own concurrency reservations.
6. Initialization/materialization is separate from access.
7. Native and serialized forms belong to the same logical sample.
8. Native and serialized forms share one slot and one commit.
9. ROS publishers are blackboard consumers.
10. ROS subscribers are blackboard producers.
11. A generic ROS publisher does not serialize.
12. A generic ROS subscriber deserializes into an already materialized native object.
13. The low-level concurrency design remains independent from ROS concepts.
14. The old `lfbb_blackboard` implementation remains unchanged.
15. New abstractions are introduced progressively from real implementation needs.
16. The final blackboard is built by composition rather than by a deep inheritance hierarchy.
17. The package is C++20 and header-only.
18. The ROS 2 package contains no nodes or executables.
19. The concurrency model preserves the wait-free progress semantics of the reference blackboard.

---

## 27. Current status

The architectural direction and initial nomenclature are considered decided.

The ROS 2 package and the exported header-only library are both named:

```text
bpe_blackboard
```

Phases 1 through 5 are implemented.

The low-level concurrency layer is:

```text
bpe::lfbb::details::ConcurrentSlot<ProtectedData>
bpe::lfbb::details::ConcurrentRing<ProtectedData, InitializationMargin>
```

`ConcurrentSlot` preserves the slot reservation algorithm of the frozen `lfbb_blackboard` reference while separating concurrency protection from the protected data representation.

`ConcurrentRing` reconstructs the proven shared-ring behaviour on top of those slots:

- initial capacity of four slots;
- configuration-time growth based on registered consumers;
- fixed wait-free reserve `kWaitFreeSlotMargin = 2`;
- configurable `InitializationMargin` for registrations that may occur without an initializer;
- first-use initialization of all slots through `initializer.initialize(ProtectedData&)`;
- later initialized expansion of newly created slots only;
- power-of-two index masking;
- writable-slot selection with occupied-slot skipping;
- monotonically increasing commit sequence;
- timestamp assignment at commit;
- release/acquire publication of the latest committed slot.

Sequence and timestamp mutation is owned by `ConcurrentRing`; the corresponding setters on `ConcurrentSlot` are private implementation operations.

The public access layer is now:

```text
ProducerGrant<ProtectedData, InitializationMargin>
ConsumerGrant<ProtectedData, InitializationMargin>
ProducerGrantFactory<ProtectedData, InitializationMargin>
ConsumerGrantFactory<ProtectedData, InitializationMargin>
Blackboard<ProtectedData, InitializationMargin>
```

The grants are pure RAII reservation objects:

- a producer grant owns one writer reservation and commits it on destruction;
- a consumer grant owns one reader reservation and releases it on destruction;
- producer access is mutable;
- consumer access is const;
- grants do not interpret `ProtectedData`.

Factories represent logical endpoints. They prevent overlapping grants from the same endpoint. Consumer factories retain the last observed sequence so that `ReadStatus` and missed-sample accounting remain local to each consumer.

Factories are neither copyable nor movable. Each factory represents one stable endpoint created during initialization and retained by its owner for the lifetime of that endpoint. Copying would duplicate an endpoint without a corresponding registration, while moving would introduce endpoint relocation that is not part of the intended execution model. After initialization, the blackboard topology and its factories are expected to remain fixed; only grants are created and destroyed during execution.

`Blackboard<ProtectedData, InitializationMargin>` is currently the generic composition root. It owns one `ConcurrentRing<ProtectedData, InitializationMargin>` per label, delegates producer/consumer registration and initialization accounting to that ring, and creates the public factories. Producer uniqueness is enforced by the ring itself. The blackboard still does not interpret the logical message type stored inside `ProtectedData`.

The generic initialization hook is now implemented, but no BPE-specific `ProtectedData` or initializer exists in this package. The initializer type is a method-template parameter used only during configuration; it is not stored by the blackboard or ring.

The next development step is Phase 6:

> Introduce the BPE protected-data representation containing coherent native and serialized storage, without modifying the concurrency algorithms.

No changes to the existing `lfbb_blackboard` repository are required.
