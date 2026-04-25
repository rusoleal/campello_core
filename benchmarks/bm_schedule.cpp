#include <campello/core/core.hpp>
#include <nanobench.h>
#include <iostream>

using namespace campello::core;

struct A { int v; };
struct B { int v; };
struct C { int v; };

namespace campello::core {
template<> struct ComponentTraits<A> : ComponentTraitsBase<A> { static constexpr std::string_view name = "A"; };
template<> struct ComponentTraits<B> : ComponentTraitsBase<B> { static constexpr std::string_view name = "B"; };
template<> struct ComponentTraits<C> : ComponentTraitsBase<C> { static constexpr std::string_view name = "C"; };
} // namespace campello::core

static void system_a(Query<A> q) {
    q.each([](A& a) { a.v += 1; });
}

static void system_b(Query<B> q) {
    q.each([](B& b) { b.v += 1; });
}

static void system_c(Query<C> q) {
    q.each([](C& c) { c.v += 1; });
}

int main() {
    ankerl::nanobench::Bench b;
    b.title("Schedule overhead")
     .unit("frame")
     .performanceCounters(true);

    for (int n : {1000, 10000, 100000}) {
        // Build a world with disjoint archetypes so systems can run in parallel
        World world;
        for (int i = 0; i < n; ++i) {
            world.spawn_with(A{i});
            world.spawn_with(B{i});
            world.spawn_with(C{i});
        }

        b.run("sequential 3 systems " + std::to_string(n), [&]() {
            system_a(world.query<A>());
            system_b(world.query<B>());
            system_c(world.query<C>());
            ankerl::nanobench::doNotOptimizeAway(&world);
        });

        Schedule sched;
            sched.add_system([](World& w) { system_a(w.query<A>()); });
            sched.add_system([](World& w) { system_b(w.query<B>()); });
            sched.add_system([](World& w) { system_c(w.query<C>()); });

        b.run("schedule 3 systems " + std::to_string(n), [&]() {
            sched.run(world);
            ankerl::nanobench::doNotOptimizeAway(&world);
        });
    }

    // System count scaling
    for (int sys_count : {5, 10, 20, 50}) {
        World world;
        for (int i = 0; i < 10000; ++i) {
            world.spawn_with(A{i});
        }

        Schedule sched;
        for (int s = 0; s < sys_count; ++s) {
            sched.add_system([](World& w) {
                w.query<A>().each([](A& a) { a.v += 1; });
            });
        }

        b.run("schedule " + std::to_string(sys_count) + " systems", [&]() {
            sched.run(world);
            ankerl::nanobench::doNotOptimizeAway(&world);
        });
    }

    b.render(ankerl::nanobench::templates::csv(), std::cout);
    return 0;
}
