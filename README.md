# Campello Core

The **Entity-Component-System (ECS) backbone** of the [Campello engine](https://github.com/rusoleal/campello).

`campello_core` is a **header-only C++20** library providing the runtime composition model upon which all other Campello modules (renderer, physics, audio, widgets, net, input) are built.

## Philosophy

- **Zero dependencies** (except for testing and benchmarks).
- **Composable, not monolithic**: the ECS provides the *mechanism*; other modules provide the *policy*.
- **Editor-first**: every public API is introspectable via compile-time-generated reflection.
- **Data-oriented**: archetype-based storage, cache-friendly iteration, custom arena allocator.
- **Parallel by design**: the schedule executor analyzes read/write dependencies and runs safe systems concurrently.

## Features

- **Archetype-based ECS** with SoA chunk storage and sparse-set entity indexing
- **Declarative queries** with automatic archetype matching and caching
- **Deferred command buffers** for safe entity mutation inside systems
- **Type-safe event bus** per frame
- **Resource container** with runtime borrow checking (`Res<T>` / `ResMut<T>`)
- **Parallel schedule executor** with implicit dependency analysis and explicit `.after()` / `.before()` system ordering
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

## License

MIT
