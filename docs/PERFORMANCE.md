# campello_core Performance Guide

This guide explains how to structure data and use the API for maximum performance.

## Quick Rules

1. **Use `each()` instead of range-for** — ~16× faster due to iterator overhead
2. **Batch spawns with `spawn_many`** — much faster than looped `spawn_with`
3. **Batch clones with `clone_many`** — much faster than looped `clone`
4. **Prefer fewer, larger archetypes** — query match cost scales linearly with archetype count
4. **Keep components small** — larger components mean fewer entities per chunk and worse cache locality
5. **Mark changes explicitly** — `mark_changed<T>(e)` is cheaper than full re-insertion
6. **Use `each_par` for CPU-heavy work** — chunk-level parallelism with no synchronization needed

---

## Benchmark Baselines

Measured on Intel Core i5-8500B @ 3.0 GHz (x86_64), AppleClang 17, macOS, Release-like build (`-O2 -DNDEBUG`) in `build-release/`:

| Operation | Throughput | Notes |
|---|---|---|
| Raw AoS iteration (100k) | ~964M entities/s | Hand-rolled struct-of-arrays, Position+Velocity |
| Raw SoA iteration (100k) | ~1.02B entities/s | Separate vectors, Position+Velocity |
| ECS `each()` (100k) | ~1.73B entities/s | ~1.7× faster than SoA on this config |
| ECS range-for (100k) | ~107M entities/s | ~16× slower than `each()` |
| Query match (500 archetypes) | ~186B entities/s | 50k entities, linear archetype scan |
| Query 8 components (10k) | ~23B entities/s | All 8 components in the query |
| Spawn 10k × 3 comp | ~1.22M entities/s | Looped `spawn_with`, ~8.2 ms per 10k |
| `spawn_many` 10k × 3 comp | ~9.60M entities/s | Bulk path, ~1.0 ms per 10k |
| `clone_many` 10k × 3 comp | ~6.75M entities/s | Bulk clone path, ~1.5 ms per 10k |
| `clone_many` 10k × 1 comp | ~18.1M entities/s | Bulk clone path, ~0.55 ms per 10k |
| Schedule 50 systems (10k) | ~14,347 frames/s | 50 independent systems, one frame |

**Note on units:** nanobench reports per-operation rates. For iteration and query benchmarks the "operation" is one full pass over the stated entity count, so the table already converts `ops/s × entity_count` into `entities/s`. For spawn, the operation is one batch of 10k spawns.

---

## Entity Spawning

### ❌ Slow: looped spawn

```cpp
for (int i = 0; i < 10000; ++i) {
    world.spawn_with(Position{...}, Velocity{...}); // resolves archetype every iteration
}
```

### ✅ Fast: bulk spawn

```cpp
auto entities = world.spawn_many<Position, Velocity>(10000,
    Position{0, 0, 0},
    Velocity{1, 0, 0}
);
```

`spawn_many` resolves the archetype once and bulk-allocates into chunks.

### ✅ Fast: bulk clone

```cpp
auto originals = world.query<Position, Velocity>();
vector<Entity> src;
originals.each([&](Entity e, Position&, Velocity&) { src.push_back(e); });

auto clones = world.clone_many(src); // resolves once, copies components in bulk
```

`clone_many` copies components in chunk-sized batches. Trivially relocatable types use a log(N) `memcpy` replication pattern, while non-trivial types use per-row copy construction.

---

## Query Iteration

### ❌ Slow: range-for

```cpp
for (auto [pos, vel] : world.query<Position, Velocity>()) {
    pos.x += vel.dx; // iterator overhead per entity
}
```

### ✅ Fast: `each()`

```cpp
world.query<Position, Velocity>().each([](Position& pos, Velocity& vel) {
    pos.x += vel.dx; // direct pointer arithmetic
});
```

### ✅ Parallel: `each_par()`

```cpp
detail::ThreadPool pool;
world.query<Position, Velocity>().each_par(pool, [](Position& pos, Velocity& vel) {
    pos.x += vel.dx; // each chunk runs on a different thread
});
```

**Caveat:** `each_par` only helps if the per-entity work is CPU-intensive. For simple additions, serial `each()` is often faster due to thread-dispatch overhead.

---

## Archetype Design

### ❌ Slow: many archetypes

```cpp
// 8 archetypes from 3 boolean components
for (int i = 0; i < 1000; ++i) {
    Entity e = world.spawn_with(A{i});
    if (i & 1) world.insert<B>(e, i);
    if (i & 2) world.insert<C>(e, i);
}
```

A query for `A` must scan all 8 archetypes. At 500 archetypes, query construction becomes a measurable bottleneck.

### ✅ Fast: fewer archetypes, use tags sparingly

If possible, flatten booleans into a single bitmask component:

```cpp
struct Flags { uint32_t mask; };
```

Or accept the archetype count if queries are cached and reused.

---

## Component Size

### ❌ Slow: giant components

```cpp
struct Huge { std::array<char, 1024> data; };
// chunk capacity = 16KB / 1032B ≈ 15 entities
```

Large components reduce entities per chunk, which:
- Increases chunk count
- Wastes memory (chunk padding)
- Reduces cache locality

### ✅ Fast: split into hot/cold data

```cpp
struct Transform { float x, y, z; };     // accessed every frame
struct Inventory { vector<Item> items; }; // accessed rarely
```

Keep the frequently accessed data small and contiguous.

---

## Change Detection

### ❌ Slow: re-insert to signal change

```cpp
world.insert<Health>(e, Health{new_hp}); // triggers archetype move if already present
```

### ✅ Fast: mutate + mark_changed

```cpp
world.get<Health>(e)->hp = new_hp;
world.mark_changed<Health>(e);
```

`mark_changed` only updates a single `uint32_t` tick value.

---

## Memory Overhead

Typical overhead per entity (dense population, 10k+ entities, from `bm_memory`):

| Archetype | Raw SoA | ECS | Overhead |
|---|---|---|---|
| 1 int component | 4 B | ~38 B | ~9.5× |
| Position + Velocity | 24 B | ~67 B | ~2.8× |
| 8 components (52 B) | 52 B | ~165 B | ~3.2× |

Overhead sources:
- **Entity array** — 8 bytes per slot
- **Chunk ticks** — 8 bytes per component per entity (added + changed)
- **EntityMeta** — 16 bytes per entity
- **Chunk padding** — alignment gaps between component arrays
- **Sparse chunk utilization** — chunks are 16 KB fixed; small entity counts waste space

**Tip:** For massive worlds with predictable entity counts, increase chunk size to reduce per-chunk metadata overhead:

```cpp
// Not yet exposed in public API; requires custom ArchetypeStorage construction
```

---

## Schedule Design

### Minimize system conflicts

Two systems that write the same component cannot run in parallel. Split work to maximize independence:

```cpp
// ❌ serializes physics + AI
schedule.add_system(physics_system, "physics").writes<Position>();
schedule.add_system(ai_system,     "ai").writes<Position>();

// ✅ parallel if they touch different components
schedule.add_system(physics_system, "physics").writes<Position>();
schedule.add_system(ai_system,     "ai").writes<Velocity>();
```

### Use explicit dependencies sparingly

Explicit `after_system` / `before_system` creates ordering edges in the DAG. Too many edges reduce parallelism.

---

## Despawn Patterns

### ❌ Slow: despawn inside iteration

```cpp
world.query<Health>().each([&](Entity e, Health&) {
    world.despawn(e); // invalidates active query
});
```

### ✅ Fast: deferred despawn

```cpp
CommandBuffer commands;
world.query<Health>().each([&](Entity e, Health& h) {
    if (h.hp <= 0) commands.despawn(e);
});
commands.apply(world);
```

Or collect entities and use `despawn_many`:

```cpp
vector<Entity> to_despawn;
world.query<Health>().each([&](Entity e, Health& h) {
    if (h.hp <= 0) to_despawn.push_back(e);
});
world.despawn_many(to_despawn);
```

---

## Summary Checklist

- [ ] Use `spawn_many` / `clone_many` / `insert_many` / `despawn_many` for batch operations
- [ ] Use `each()` instead of range-for for hot loops
- [ ] Keep components small; split hot/cold data
- [ ] Use `mark_changed` instead of re-insertion
- [ ] Minimize archetype count for heavily-queried component sets
- [ ] Use `each_par` only when per-entity work is expensive
- [ ] Never despawn inside an active query — buffer and batch
