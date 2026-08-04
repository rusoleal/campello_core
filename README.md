# Campello Core

The **Entity-Component-System (ECS) backbone** of the [Campello engine](https://github.com/rusoleal/campello).

`campello_core` is a **header-only C++20** library providing the runtime composition model upon which all other Campello modules (renderer, physics, audio, widgets, net, input) are built.

## Philosophy

- **Zero dependencies** (except for testing and benchmarks).
- **Composable, not monolithic**: the ECS provides the *mechanism*; other modules provide the *policy*.
- **Editor-first**: every public API is introspectable via compile-time-generated reflection.
- **Data-oriented**: archetype-based storage, cache-friendly iteration, custom arena allocator.
- **Parallel by design**: the schedule executor analyzes read/write dependencies and runs safe systems concurrently, over a `ThreadPool` meant to be shared across every Campello module in your process, not owned separately by each one.

## Features

- **Archetype-based ECS** with SoA chunk storage and sparse-set entity indexing
- **Declarative queries** with automatic archetype matching and caching
- **Deferred command buffers** for safe entity mutation inside systems
- **Type-safe event bus** per frame
- **Resource container** with runtime borrow checking (`Res<T>` / `ResMut<T>`)
- **Parallel schedule executor** with implicit dependency analysis, explicit `.after()` / `.before()` system ordering, and a public `ThreadPool` (see [Threading](#threading)) shareable across your whole engine, not private to the ECS
- **Bulk operations**: `spawn_many`, `despawn_many`, `insert_many`
- **Change detection**: `Added<C>`, `Changed<C>` query filters and `removed<C>()` tracking
- **Component hooks**: `on_add<C>`, `on_remove<C>`
- **Native parent/child hierarchies** with automatic cascade despawn
- **Reflection system** for editor property grids
- **JSON serialization** of components via reflection

## Quick Start

```cpp
#include <campello/core/core.hpp>
#include <iostream>

using namespace campello::core;

struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };

namespace campello::core {
template<> struct ComponentTraits<Position> : ComponentTraitsBase<Position> {
    static constexpr std::string_view name = "Position";
};
template<> struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity> {
    static constexpr std::string_view name = "Velocity";
};
} // namespace campello::core

int main() {
    World world;

    auto e = world.spawn_with(Position{0, 0, 0}, Velocity{1, 0, 0});

    for (auto [pos, vel] : world.query<Position, Velocity>()) {
        pos.x += vel.dx;
    }

    std::cout << world.get<Position>(e)->x << "\n"; // 1
    return 0;
}
```

## Build

```bash
cmake -B build -S . -DCAMPELLO_CORE_BUILD_TESTS=ON -DCAMPELLO_CORE_BUILD_EXAMPLES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Requirements
- CMake 3.20+
- C++20 compiler (GCC 12+, Clang 15+, MSVC 2022+)

## Integration

`campello_core` is header-only. Add it as a subdirectory or install it:

```cmake
find_package(campello_core REQUIRED)
target_link_libraries(your_target PRIVATE campello::core)
```

Compile-time version check:
```cpp
#include <campello/core/core.hpp>

static_assert(CAMPELLO_CORE_VERSION >= 100); // 0.1.0
```

## Architecture

```
World
├── EntityAllocator (generation + index recycling)
├── EntityMeta[] (archetype, chunk, row)
├── ArchetypeStorage
│   ├── Archetype[] (sorted component sets)
│   │   └── Chunk[] (SoA contiguous blocks, ~16 KB)
│   └── Archetype graph (cached add/remove edges)
├── ComponentRegistry (TypeId → TypeInfo)
├── ReflectRegistry (ComponentId → ComponentInfo)
├── EventStorage (per-type ring buffers)
├── ResourceStorage (type-erased singletons)
└── CommandBuffer (deferred operations)
```

## Threading

`campello::core::ThreadPool` (`thread_pool.hpp`) is public API, not an implementation detail private to the ECS — it's meant to be **the one threading mechanism shared across an entire Campello application**, not something each module (this ECS, the renderer, physics, audio, ...) reinvents independently.

Own a single `ThreadPool` at the top of your application — wherever you also own your render loop, input loop, etc. — sized with headroom for those other threads, and pass it by reference into every subsystem that wants parallelism:

```cpp
ThreadPool shared_pool(std::thread::hardware_concurrency() - 3); // reserve threads for render/input/audio

Schedule gameplay_schedule(shared_pool);
Schedule physics_schedule(shared_pool);   // same pool, no extra OS threads
world.query<Position, Velocity>().each_par(shared_pool, [](Position& p, Velocity& v) { ... });
// any other Campello module accepting a ThreadPool& should take the same shared_pool
```

Both `Schedule` and `each_par()` also support owning their own pool if you don't need cross-module sharing:

```cpp
Schedule schedule;      // owns its own pool, sized to hardware_concurrency()
Schedule schedule(0);   // 0 threads: fully sequential, deterministic ordering
```

But every `Schedule` (and every ad-hoc `ThreadPool` some other module creates) that *doesn't* share the same pool adds its own independent set of OS threads competing for the same cores — real oversubscription risk in a game process that already has dedicated render/input/audio threads. Prefer one process-wide pool once you have more than one module that wants parallelism.

## License

MIT
