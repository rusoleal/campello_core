# Campello Core — Agent Instructions

## Project Identity
`campello_core` is the **header-only C++20 ECS backbone** of the Campello engine.

## Build & Test
```bash
# Configure with tests and examples
cmake -B build -S . -DCAMPELLO_CORE_BUILD_TESTS=ON -DCAMPELLO_CORE_BUILD_EXAMPLES=ON

# Build
cmake --build build --parallel

# Run tests
ctest --test-dir build --output-on-failure

# Run a single test binary
./build/tests/campello_core_tests
```

## Coding Standards
- **C++20 minimum**. Use `requires`, concepts, `auto` parameters, designated initializers.
- **Header-only**: all implementation lives in `include/campello/core/`. No `.cpp` files in `src/`.
- **Namespace**: everything public lives in `campello::core`. Implementation details in `campello::core::detail`.
- **Naming**:
  - Types: `PascalCase` (`World`, `Query`, `Entity`)
  - Functions: `snake_case` (`spawn`, `query`, `is_alive`)
  - Concepts: `snake_case` (`queryable`, `component`)
  - Macros: `CAMPELLO_CORE_*` prefix only when absolutely necessary (avoid if possible)
- **No exceptions for control flow**: use `std::optional`, `expected` (or a lightweight equivalent), or assert/panic for programmer errors.
- **No RTTI for hot paths**: reflection is template-generated and stored in registries.
- **Thread safety**: `World` is not thread-safe. Parallelism happens at the `Schedule` level only.

## Architecture Decisions (Locked)
1. **Header-only** — maximizes inlining across sibling modules.
2. **Parallel schedule executor** — implicit parallelism from day one.
3. **Native parent/child hierarchies** — `Parent` + `Children` components with automatic bookkeeping.
4. **Built-in serialization** — JSON/binary via reflection.
5. **Custom arena allocator** — for chunk storage.

## Testing
Every new public API must have a test in `tests/`. Stress tests should verify behavior with 100k+ entities.
