# campello_core Architecture

This document describes the internal design of the campello_core ECS library. It is intended for contributors and advanced users who want to understand how data is stored, how queries work, and where the performance trade-offs lie.

## Table of Contents

1. [Overview](#overview)
2. [Entity Storage](#entity-storage)
3. [Archetypes & Chunks](#archetypes--chunks)
4. [Component Registry](#component-registry)
5. [Queries](#queries)
6. [Change Detection](#change-detection)
7. [Schedule & Threading](#schedule--threading)
8. [Events & Resources](#events--resources)

---

## Overview

campello_core is an **archetype-based ECS** (Entity-Component-System) library. Entities with the same set of components are stored together in contiguous chunks of memory (SoA layout). This maximizes cache locality during iteration while keeping insertion and removal of single components relatively fast.

Key design choices:
- **Header-only** — single `#include <campello/core/core.hpp>`
- **C++20** — concepts, strong typedefs, structured bindings
- **No external dependencies** — only the standard library
- **Thread pool** — lock-based task queue for parallel system execution

---

## Entity Storage

### Entity identifiers

```cpp
using Entity = std::uint64_t;  // (generation << 32) | index
```

An `Entity` is a 64-bit value split into:
- **Index** (lower 32 bits) — slot in the `entity_meta_` array
- **Generation** (upper 32 bits) — incremented on despawn to prevent ABA problems

### EntityMeta

```cpp
struct EntityMeta {
    ArchetypeId archetype;   // 0 = no components yet
    uint32_t    chunk_index; // which chunk within the archetype
    uint32_t    row_index;   // which row within the chunk
};
```

`entity_meta_[index]` gives the archetype location for any alive entity in O(1).

### EntityAllocator

A simple generational allocator:
- `generations_[index]` — current generation for each slot
- `free_indices_` — stack of recycled indices
- `next_index_` — monotonically increasing counter when no free slots exist

Despawning increments the generation and pushes the index onto the free list. `is_alive(entity)` checks `generations_[index] == generation`.

---

## Archetypes & Chunks

### Archetype

An **archetype** is a unique set of component types, identified by a sorted `vector<ComponentId>`. Each archetype owns one or more **chunks**.

```cpp
struct Archetype {
    ArchetypeId id;
    vector<ComponentId> components; // sorted
    vector<Chunk>       chunks;
    vector<ChunkTicks>  chunk_ticks;
    uint32_t            entity_count;
    unordered_map<ComponentId, ArchetypeId> add_edges;
    unordered_map<ComponentId, ArchetypeId> remove_edges;
};
```

**Edge caches** (`add_edges` / `remove_edges`) memoize the target archetype when adding or removing a single component, avoiding repeated lookups in the archetype map.

### Chunk

A chunk is a contiguous block of memory (default 16 KB) laid out in SoA (Structure of Arrays) format:

```
[ Entity[capacity] ] [ padding ] [ CompA[capacity] ] [ padding ] [ CompB[capacity] ] ...
```

```cpp
struct Chunk {
    std::byte* data;              // arena-allocated
    uint32_t   count;             // alive entities in this chunk
    uint32_t   capacity;          // max entities (computed from chunk_size / per-entity bytes)
    size_t     total_size;        // total allocated bytes
    vector<size_t> offsets;       // offsets[0] = Entity array, offsets[i+1] = component i
};
```

**Capacity calculation:**
```cpp
capacity = chunk_size / (sizeof(Entity) + sum(component_sizes))
```

If a component requires alignment > 1, padding is inserted between arrays.

### ChunkTicks

Parallel to each chunk, `ChunkTicks` stores per-component, per-row tick arrays:

```cpp
struct ChunkTicks {
    vector<vector<Tick>> added;    // [comp_index][row] = tick when added
    vector<vector<Tick>> changed;  // [comp_index][row] = tick when last modified
};
```

These are used by the `Added<C>` and `Changed<C>` query filters.

### ArchetypeStorage

`ArchetypeStorage` owns:
- `archetypes_` — dense vector of archetypes (IDs are 1-based indices)
- `archetype_map_` — `map<vector<ComponentId>, ArchetypeId>` for deduplication
- `allocator_` — `ArenaAllocator` backing all chunk data
- `chunk_size_` — configurable chunk size (default 16 KB)

**Entity lifecycle in chunks:**
1. **Spawn** — `allocate_entity()` finds a chunk with free space (or allocates a new one), appends the entity ID and default-constructs all components.
2. **Insert component** — `move_entity()` copies the entity from the old archetype to the new one (swap-back in source chunk), then updates `EntityMeta`.
3. **Remove component** — same as insert, but to a smaller archetype.
4. **Despawn** — `remove_entity()` swap-backs with the last row in the chunk, updates the swapped entity's `EntityMeta`, then destructs the removed row.

**Swap-back compaction:**
When an entity is removed from a chunk, the last entity in that chunk moves into the vacated slot. This keeps chunks dense but means entity order within a chunk is not stable.

---

## Component Registry

`ComponentRegistry` maps `ComponentId` → `TypeInfo`:

```cpp
struct TypeInfo {
    TypeId id;
    size_t size, alignment;
    bool trivially_destructible, trivially_copyable, trivially_relocatable;
    void (*construct)(void* ptr);
    void (*destruct)(void* ptr);
    void (*move)(void* dst, void* src);
    void (*copy)(void* dst, const void* src);
};
```

Registration happens lazily via `world.register_component<T>()`:
```cpp
template<typename T>
void register_component() {
    component_registry_.register_component<T>();
    ComponentBuilder builder(ComponentTraits<T>::name);
    builder.size(sizeof(T)).alignment(alignof(T))
           .relocatable(ComponentTraits<T>::trivially_relocatable);
    ComponentTraits<T>::reflect(builder);
    reflect_registry_.register_info(component_type_id<T>(), builder.build());
}
```

Users can specialize `ComponentTraits<T>` to provide:
- A human-readable `name`
- `reflect(ComponentBuilder&)` to declare properties for serialization/editor

---

## Queries

### Query construction

```cpp
auto q = world.query<Position, Velocity>();
```

1. Resolves `ComponentId`s for each template parameter
2. Scans all archetypes to find those containing all required components
3. Stores matching archetype IDs in `matching_archetypes_`

**Archetype caching:** The query stores the matched archetypes. On subsequent iterations, it checks `archetype_version_` (incremented whenever an archetype is created or an entity moves). If the version changed, the cache is rebuilt.

### Iteration

```cpp
q.each([](Position& pos, Velocity& vel) { ... });
```

Inner loop:
1. For each matching archetype, compute component indices once
2. For each chunk, iterate rows
3. Call `func(get_component_ref<Cs>(chunk, row, comp_idx)...)`

**Range-for:**
```cpp
for (auto [pos, vel] : world.query<Position, Velocity>()) { ... }
```
Returns a tuple of references. Slightly slower than `each()` due to iterator overhead.

### Parallel iteration

```cpp
world.query<Position, Velocity>().each_par(pool, [](Position& pos, Velocity& vel) { ... });
```

Dispatches each chunk as a separate task to the thread pool. Chunks are independent, so no synchronization is needed inside `func`.

---

## Change Detection

### How it works

Every time a component is:
- **Added** (spawn or insert) → `added_tick[comp][row] = current_tick`
- **Changed** via `mark_changed<T>(entity)` → `changed_tick[comp][row] = current_tick`

The global `change_tick_` starts at 1 and is incremented by `world.increment_change_tick()`.

### Query filters

```cpp
world.query<Position>()
    .added<Position>()           // only entities where added_tick > last_run_tick
    .changed<Position>()         // only entities where changed_tick > last_run_tick
    .with_last_run_tick(tick);   // set the comparison baseline
```

Filters are checked per-row inside the iteration loop.

### Removed tracking

```cpp
world.remove<Health>(e);        // records e in removed_ buffer
auto& list = world.removed<Health>(); // entities that lost Health this tick
world.increment_change_tick();  // clears removed_ buffers
```

There is **no `Removed<C>` query filter** — use `world.removed<C>()` and iterate the returned vector manually.

---

## Schedule & Threading

### SystemDescriptor

Systems are registered with a descriptor declaring their access:

```cpp
SystemDescriptor desc;
desc.reads_components = { component_type_id<Position>() };
desc.writes_components = { component_type_id<Velocity>() };
desc.reads_resources = { resource_type_id<Time>() };
desc.after_system = "physics";
```

### Conflict detection

Two systems conflict (must not run in parallel) if:
- They write the same component
- One writes and the other reads the same component
- They write the same resource
- One writes and the other reads the same resource

### Execution

Per stage:
1. Build a DAG where edges represent ordering constraints (explicit `after`/`before` + access conflicts)
2. Topological sort into batches
3. Each batch is dispatched to the thread pool as independent tasks
4. Wait for all tasks in the batch to finish before proceeding to the next batch

### ThreadPool

Lock-based task queue with `std::future` support. Default thread count = `hardware_concurrency()`.

---

## Events & Resources

### Events

```cpp
struct DamageEvent { Entity target; int amount; };
world.send(DamageEvent{e, 10});
```

Events are stored in a `vector<EventStorage>` ring buffer (one per event type). `world.read_events<DamageEvent>()` returns a span of unprocessed events. `world.clear_events<DamageEvent>()` empties the buffer.

### Resources

```cpp
world.init_resource<Time>({0.0f});
auto t = world.resource<Time>();       // Res<Time> (immutable borrow)
auto t = world.resource_mut<Time>();   // ResMut<Time> (mutable borrow)
```

Resources are stored in a `unordered_map<TypeId, void*>` with runtime borrow checking:
- `ResMut` checks `mutable_borrowed == 0`
- `Res` increments `immutable_borrows`
- Borrows are released in the destructor

Borrow tracking uses `std::atomic<int>` for thread safety during parallel schedule execution.

---

## Memory Layout Diagram

```
ArchetypeStorage
├── archetypes_[0]  (id=1)  {Position, Velocity}
│   ├── chunks[0]  (16 KB)
│   │   ├── data: [Entity[cap]][Pos[cap]][Vel[cap]]
│   │   ├── count: 1365
│   │   └── capacity: 1365
│   ├── chunk_ticks[0]
│   │   ├── added[0][cap]    -- Position added ticks
│   │   ├── added[1][cap]    -- Velocity added ticks
│   │   ├── changed[0][cap]  -- Position changed ticks
│   │   └── changed[1][cap]  -- Velocity changed ticks
│   └── add_edges / remove_edges
│
├── archetypes_[1]  (id=2)  {Position}
│   └── ...
│
└── ArenaAllocator (owns all chunk.data pointers)
```

---

## Known Limitations

1. **Self-referential types** — components with pointers to their own members become dangling after archetype moves (swap-back compaction). This is an inherent SoA limitation.
2. **Throwing move constructors** — exceptions during `insert`/`remove` propagate out without strong exception safety guarantee.
3. **Chunk memory not reclaimed** — empty chunks are not freed until the world is destroyed or `clear()` is called. Heavy despawning without respawning can waste memory.
4. **No `Removed<C>` query filter** — only `world.removed<C>()` vector access.
5. **Entity order instability** — swap-back compaction changes row indices.
