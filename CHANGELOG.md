# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[0.1.0]: https://github.com/rusoleal/campello/releases/tag/v0.1.0
