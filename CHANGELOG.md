# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.1] - 2026-11-10

### Changed
- **`ThreadPool` without workers runs tasks inline.** `ThreadPool(0)` used to accept tasks that never ran; `enqueue()` now runs them on the calling thread and returns a ready future. Builds without threads (Emscripten without pthreads, where starting a `std::thread` aborts) always get a pool without workers (`ThreadPool::kThreadsAvailable`), so `Schedule`, `each_par()` and apps that own a pool work single-threaded on the web.

## [0.2.0] - 2026-08-03

### Added
- `Schedule(ThreadPool&)` — construct a `Schedule` from an externally-owned pool instead of always creating its own. `ThreadPool` is designed to be the **one shared threading mechanism for an entire Campello application**, not something the ECS, renderer, physics, audio, etc. each reinvent independently — own a single pool wherever your engine bootstraps (sized with headroom for render/input/audio threads) and pass it by reference into every `Schedule` instance and every `each_par()` call, in this library and any other Campello module that accepts a `ThreadPool&`. Without this, every default-constructed `Schedule` silently spins up its own `hardware_concurrency()` workers — a real oversubscription risk once more than one module wants parallelism in the same process. The caller must keep the shared pool alive for as long as anything using it is running. See the [Threading](README.md#threading) section in the README.
- `Schedule::set_min_parallel_duration()` / `min_parallel_duration()` — `Schedule` now tracks each system's actual wall-clock duration from its previous run and only dispatches an independent batch to the thread pool if the batch's summed last-measured cost clears this threshold (default 20us). Previously every independent batch was dispatched to the pool unconditionally, which was a real regression at small scale (see Fixed, below).
- `Removed<C>` query filter — `query<...>().removed<C>().with_last_run_tick(t)`, composable with `added`/`changed` and supported by `each()`, `each_with_entity()`, range-for, and `count()`. Iterates the world-level removal event buffer directly instead of requiring manual `world.removed<C>()` polling.
- Long-running stress executable `campello_core_long_stress` (`tests/test_long_stress.cpp`)
  - Configurable duration via `--minutes N` and reproducible seeds via `--seed S`
  - Mixed workload: spawn, mutate, despawn, schedule execution, queries, events, resources
  - Per-phase invariant checks against an external oracle
- Prefab/clone example `examples/ex_11_prefab.cpp` demonstrating `World::clone()` and `World::clone_many()`

### Changed
- `World::removed<T>()` now returns `std::vector<Entity>` by value (was `const std::vector<Entity>&`) and takes an optional `Tick since = 0` parameter, so callers can query removals since an arbitrary tick rather than only the current one. **Breaking:** code binding the old return value to a reference member or relying on reference semantics needs updating.
- `Schedule`'s default constructor now spins up `std::thread::hardware_concurrency()` worker threads. Previously `Schedule schedule;` left its thread pool null and always ran fully sequentially, regardless of how independent the systems were — pass `0` explicitly (`Schedule schedule(0);`) to get the old fully-sequential, deterministic-order behavior back.
- **Breaking:** `ThreadPool` moved from `campello::core::detail` to public `campello::core` (`thread_pool.hpp`, was `detail/thread_pool.hpp`). It was already de-facto public API — required by `each_par(ThreadPool&, ...)` and used directly in `examples/ex_08_thread_pool.cpp` — so leaving it under `detail::` both misrepresented it as an internal implementation detail and obscured that it's meant to be reused by other Campello modules as their shared threading primitive, not just by this ECS. Update `#include <campello/core/detail/thread_pool.hpp>` to `#include <campello/core/thread_pool.hpp>` and `campello::core::detail::ThreadPool` to `campello::core::ThreadPool`.

### Fixed
- Changed `null_entity` from `0` to `std::numeric_limits<Entity>::max()` to prevent collision with the first spawned entity. This fixes a critical swap-back metadata corruption bug: when entity 0 (value `0`) was swapped during archetype moves, the caller treated it as `null_entity` and skipped updating its `entity_meta_`, leading to stale component pointers.
- Fixed a heap-use-after-free in query iteration: `ArchetypeStorage::archetypes_` was a `std::vector<Archetype>`, so creating a new archetype (e.g. removing a component during `each()` produces a never-before-seen component combination) could reallocate the vector and invalidate `Archetype*` pointers cached by an in-flight query. Changed to `std::deque<Archetype>`, which keeps references/pointers stable across `push_back`.
- Fixed a data race in `World::query<Cs...>()`: `query_cache_` and `component_registry_` are shared mutable state (a `std::map` and a registry, both mutated on cache miss / first registration) with no synchronization, so two systems dispatched to different `Schedule` worker threads calling `query<...>()` concurrently could corrupt the underlying tree structure. This was invisible until the `Schedule` default-constructor fix above actually enabled concurrent dispatch — TSan caught it via `tests/test_long_stress.cpp`. Fixed with a mutex guarding both, released before iteration so the cost is bounded to one lock per system per frame, not per-entity.
- Fixed unconditional thread-pool dispatch making independent-system batches 16x slower (1k entities) to 3.8x slower (10k entities) than sequential execution — dispatch overhead (~14-15us per batch) dominated at small scale. Cost-aware dispatch (see Added, above) cuts this to ~1.9x/~1.1x slower while preserving the large-scale win (~1.3x faster at 100k entities). **Note:** because a system's cost is unknown until measured, a `Schedule`'s first `run()` call always executes inline regardless of thread availability; real parallel dispatch begins on the second call onward.

## [0.1.0] - 2026-04-24

### Added
- Archetype-based ECS with SoA chunk storage (~16 KB chunks) and sparse-set entity indexing
- Declarative `Query<Cs...>` with automatic archetype matching, caching, and per-component archetype index for O(rare) queries
- `Query::each()`, `each_par()` (parallel), `each_with_entity()` iteration APIs
- Deferred command buffers for safe entity mutation inside systems
- Type-safe event bus per frame
- Resource container with aligned allocation and atomic borrow tracking (`Res<T>` / `ResMut<T>`)
- Lock-based thread pool and parallel schedule executor with implicit dependency analysis
- Explicit system ordering: `.after_system()` / `.before_system()`
- Bulk operations: `spawn_many`, `despawn_many`, `insert_many`
- Change detection: `Added<C>`, `Changed<C>` query filters and `removed<C>()` tracking
- Component hooks: `on_add<C>`, `on_remove<C>`
- Native parent/child hierarchies with automatic cascade despawn
- Entity cloning: `World::clone(Entity)` and `World::clone_many(Entity, count)`
- World snapshots (save/restore full world state)
- Compile-time reflection system for editor property grids
- JSON serialization of components via reflection
- 122 tests (42119+ assertions), 5 nanobench benchmarks
- CI: GitHub Actions (Ubuntu/GCC+ASan, Windows/MSVC, macOS/AppleClang+TSan)
- 10 examples including a headless demo game (`ex_10_demo_game.cpp`)

### Fixed
- Removed spurious `destruct_row()` on uninitialized destination during archetype move (caused SIGABRT with `std::vector` components in Release)
- Fixed ODR violation in tests (`test_prefab.cpp` defined conflicting `Position` struct)
- Added `[[maybe_unused]]` on atomic compare-exchange result to satisfy `-Werror` under NDEBUG

### Performance
- Per-component archetype index (`component_to_archetypes_`) → ~13× speedup for rare-component queries
- Spatial grid collision in demo → 2344 FPS (was 12 FPS with naive O(n²))
- `each_with_entity()` for particle/emitter systems → 6044 FPS (2.6× improvement)

[0.2.1]: https://github.com/rusoleal/campello_core/releases/tag/v0.2.1
[0.2.0]: https://github.com/rusoleal/campello_core/releases/tag/v0.2.0
[0.1.0]: https://github.com/rusoleal/campello_core/releases/tag/v0.1.0
