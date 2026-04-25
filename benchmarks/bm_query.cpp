#include <campello/core/core.hpp>
#include <nanobench.h>
#include <iostream>
#include <random>

using namespace campello::core;

struct C1 { int v; };
struct C2 { int v; };
struct C3 { int v; };
struct C4 { int v; };
struct C5 { int v; };
struct C6 { int v; };
struct C7 { int v; };
struct C8 { int v; };

namespace campello::core {
template<> struct ComponentTraits<C1> : ComponentTraitsBase<C1> { static constexpr std::string_view name = "C1"; };
template<> struct ComponentTraits<C2> : ComponentTraitsBase<C2> { static constexpr std::string_view name = "C2"; };
template<> struct ComponentTraits<C3> : ComponentTraitsBase<C3> { static constexpr std::string_view name = "C3"; };
template<> struct ComponentTraits<C4> : ComponentTraitsBase<C4> { static constexpr std::string_view name = "C4"; };
template<> struct ComponentTraits<C5> : ComponentTraitsBase<C5> { static constexpr std::string_view name = "C5"; };
template<> struct ComponentTraits<C6> : ComponentTraitsBase<C6> { static constexpr std::string_view name = "C6"; };
template<> struct ComponentTraits<C7> : ComponentTraitsBase<C7> { static constexpr std::string_view name = "C7"; };
template<> struct ComponentTraits<C8> : ComponentTraitsBase<C8> { static constexpr std::string_view name = "C8"; };
} // namespace campello::core

// Build a world with `num_archetypes` distinct archetypes, each with 100 entities.
// Every entity has C1. The query is for C1 only, so it must scan all archetypes.
static void bench_query_match_cost(ankerl::nanobench::Bench& b) {
    for (int num_archetypes : {1, 10, 100, 500}) {
        World world;
        std::mt19937 rng(123);
        std::uniform_int_distribution<int> dist(0, num_archetypes - 1);

        for (int i = 0; i < num_archetypes * 100; ++i) {
            int arch = dist(rng);
            Entity e = world.spawn_with(C1{i});
            if (arch & 1) world.insert<C2>(e, i);
            if (arch & 2) world.insert<C3>(e, i);
            if (arch & 4) world.insert<C4>(e, i);
            if (arch & 8) world.insert<C5>(e, i);
        }

        b.run("match " + std::to_string(num_archetypes) + " archetypes", [&]() {
            int count = 0;
            world.query<C1>().each([&](C1&) { ++count; });
            ankerl::nanobench::doNotOptimizeAway(count);
        });
    }
}

// Fixed 10k entities, varying number of components in the query
static void bench_query_component_count(ankerl::nanobench::Bench& b) {
    World world;
    for (int i = 0; i < 10000; ++i) {
        world.spawn_with(C1{i}, C2{i}, C3{i}, C4{i}, C5{i}, C6{i}, C7{i}, C8{i});
    }

    b.run("query 1 comp", [&]() {
        int count = 0;
        world.query<C1>().each([&](C1&) { ++count; });
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    b.run("query 2 comp", [&]() {
        int count = 0;
        world.query<C1, C2>().each([&](C1&, C2&) { ++count; });
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    b.run("query 4 comp", [&]() {
        int count = 0;
        world.query<C1, C2, C3, C4>().each([&](C1&, C2&, C3&, C4&) { ++count; });
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    b.run("query 8 comp", [&]() {
        int count = 0;
        world.query<C1, C2, C3, C4, C5, C6, C7, C8>().each([&](C1&, C2&, C3&, C4&, C5&, C6&, C7&, C8&) { ++count; });
        ankerl::nanobench::doNotOptimizeAway(count);
    });
}

// Cold vs warm query (first construction vs repeated use)
static void bench_query_cold_warm(ankerl::nanobench::Bench& b) {
    World world;
    for (int i = 0; i < 100000; ++i) {
        world.spawn_with(C1{i}, C2{i});
    }

    b.run("query cold", [&]() {
        World w;
        for (int i = 0; i < 100000; ++i) {
            w.spawn_with(C1{i}, C2{i});
        }
        int count = 0;
        w.query<C1, C2>().each([&](C1&, C2&) { ++count; });
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    b.run("query warm", [&]() {
        int count = 0;
        world.query<C1, C2>().each([&](C1&, C2&) { ++count; });
        ankerl::nanobench::doNotOptimizeAway(count);
    });
}

// Per-component index stress test: many archetypes, rare component in query.
// With a linear scan, the query must check every archetype.
// With a per-component index, it only checks archetypes containing the rare component.
static void bench_query_rare_component(ankerl::nanobench::Bench& b) {
    World world;
    // Create 500 archetypes, each with 100 entities.
    // Only ~5% of archetypes contain a "rare" component (C6).
    std::mt19937 rng(123);
    std::uniform_int_distribution<int> dist(0, 499);
    std::uniform_int_distribution<int> rare_dist(0, 19);

    for (int i = 0; i < 500 * 100; ++i) {
        int arch = dist(rng);
        Entity e = world.spawn_with(C1{i});
        if (arch & 1) world.insert<C2>(e, i);
        if (arch & 2) world.insert<C3>(e, i);
        if (arch & 4) world.insert<C4>(e, i);
        if (arch & 8) world.insert<C5>(e, i);
        if (rare_dist(rng) == 0) world.insert<C6>(e, i); // ~5% of entities
    }

    // Query for C1 (common) + C6 (rare).
    // With per-component index: start from C6's ~25 archetypes, verify C1.
    // Without: scan all ~500 archetypes, check both components.
    b.run("query rare C1+C6", [&]() {
        int count = 0;
        world.query<C1, C6>().each([&](C1&, C6&) { ++count; });
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    // Baseline: query for C1 alone (must still touch all archetypes with C1).
    b.run("query common C1", [&]() {
        int count = 0;
        world.query<C1>().each([&](C1&) { ++count; });
        ankerl::nanobench::doNotOptimizeAway(count);
    });
}

int main() {
    ankerl::nanobench::Bench b;
    b.title("Query performance")
     .unit("entity")
     .performanceCounters(true);

    bench_query_match_cost(b);
    bench_query_component_count(b);
    bench_query_cold_warm(b);
    bench_query_rare_component(b);

    b.render(ankerl::nanobench::templates::csv(), std::cout);
    return 0;
}
