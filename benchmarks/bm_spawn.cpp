#include <campello/core/core.hpp>
#include <nanobench.h>
#include <iostream>
#include <vector>

using namespace campello::core;

struct A { int v; };
struct B { int v; };
struct C { int v; };

namespace campello::core {
template<> struct ComponentTraits<A> : ComponentTraitsBase<A> { static constexpr std::string_view name = "A"; };
template<> struct ComponentTraits<B> : ComponentTraitsBase<B> { static constexpr std::string_view name = "B"; };
template<> struct ComponentTraits<C> : ComponentTraitsBase<C> { static constexpr std::string_view name = "C"; };
} // namespace campello::core

int main() {
    ankerl::nanobench::Bench b;
    b.title("Spawn / Despawn / Insert throughput")
     .unit("operation")
     .performanceCounters(true);

    // --- Spawn ---
    for (int n : {100, 1000, 10000}) {
        b.run("spawn " + std::to_string(n) + " x 1 comp", [&]() {
            World world;
            for (int i = 0; i < n; ++i) {
                world.spawn_with(A{i});
            }
            ankerl::nanobench::doNotOptimizeAway(&world);
        });

        b.run("spawn " + std::to_string(n) + " x 3 comp", [&]() {
            World world;
            for (int i = 0; i < n; ++i) {
                world.spawn_with(A{i}, B{i}, C{i});
            }
            ankerl::nanobench::doNotOptimizeAway(&world);
        });
    }

    // --- Despawn from middle ---
    for (int n : {100, 1000, 10000}) {
        b.run("despawn middle " + std::to_string(n), [&]() {
            World world;
            std::vector<Entity> entities;
            entities.reserve(n);
            for (int i = 0; i < n; ++i) {
                entities.push_back(world.spawn_with(A{i}));
            }
            for (int i = 0; i < n; i += 2) {
                world.despawn(entities[i]);
            }
            ankerl::nanobench::doNotOptimizeAway(&world);
        });
    }

    // --- Insert component on existing entities ---
    for (int n : {100, 1000, 10000}) {
        b.run("insert B on " + std::to_string(n), [&]() {
            World world;
            std::vector<Entity> entities;
            entities.reserve(n);
            for (int i = 0; i < n; ++i) {
                entities.push_back(world.spawn_with(A{i}));
            }
            for (int j = 0; j < n; ++j) {
                world.insert<B>(entities[j], j);
            }
            ankerl::nanobench::doNotOptimizeAway(&world);
        });
    }

    b.render(ankerl::nanobench::templates::csv(), std::cout);
    return 0;
}
