#include <catch2/catch_test_macros.hpp>
#include <campello/core/schedule.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include "test_components.hpp"

using namespace campello::core;

struct Counter {
    std::atomic<int> value{0};
};

TEST_CASE("Schedule sequential execution") {
    World world;
    world.init_resource<Counter>();

    Schedule schedule(0); // 0 threads = sequential
    schedule.add_system([](World& w) {
        w.resource<Counter>().value++;
    });
    schedule.add_system([](World& w) {
        w.resource<Counter>().value++;
    });

    schedule.run(world);
    REQUIRE(world.resource<Counter>().value == 2);
}

TEST_CASE("Schedule parallel execution") {
    World world;
    world.init_resource<Counter>();

    Schedule schedule; // default threads = parallel
    schedule.set_min_parallel_duration(std::chrono::nanoseconds{0}); // force real dispatch for this test
    schedule.add_system([](World& w) {
        w.resource<Counter>().value++;
    });
    schedule.add_system([](World& w) {
        w.resource<Counter>().value++;
    });

    schedule.run(world);
    REQUIRE(world.resource<Counter>().value == 2);
}

TEST_CASE("Schedule stage ordering") {
    World world;
    world.init_resource<Counter>();

    Schedule schedule(0);
    int step = 0;
    schedule.add_system([&step](World& w) {
        w.resource<Counter>().value = 1;
        REQUIRE(step == 0);
        step = 1;
    }).in_stage(Stage::PreUpdate);

    schedule.add_system([&step](World& w) {
        REQUIRE(w.resource<Counter>().value == 1);
        REQUIRE(step == 1);
        step = 2;
    }).in_stage(Stage::Update);

    schedule.run(world);
    REQUIRE(step == 2);
}

TEST_CASE("Schedule conflict detection prevents bad parallelization") {
    World world;
    Entity e = world.spawn_with(Position{0.0f, 0.0f, 0.0f}, Velocity{1.0f, 0.0f, 0.0f});

    Schedule schedule; // parallel
    schedule.set_min_parallel_duration(std::chrono::nanoseconds{0}); // force real dispatch for this test
    schedule.add_system([](World& w) {
        for (auto [pos, vel] : w.query<Position, Velocity>()) {
            pos.x += vel.dx;
        }
    }).writes_components<Position>().reads_components<Velocity>();

    schedule.add_system([](World& w) {
        for (auto [pos, vel] : w.query<Position, Velocity>()) {
            pos.x += vel.dx;
        }
    }).writes_components<Position>().reads_components<Velocity>();

    // Should not crash even with parallel execution
    schedule.run(world);
    REQUIRE(world.get<Position>(e)->x == 2.0f);
}


TEST_CASE("Schedule explicit dependencies") {
    World world;
    world.init_resource<Counter>();

    Schedule schedule(0); // sequential for deterministic test
    int step = 0;

    schedule.add_system([&step](World& w) {
        REQUIRE(step == 0);
        w.resource<Counter>().value = 10;
        step = 1;
    }, "init").in_stage(Stage::Update);

    schedule.add_system([&step](World& w) {
        REQUIRE(step == 1);
        REQUIRE(w.resource<Counter>().value == 10);
        w.resource<Counter>().value = 20;
        step = 2;
    }, "process").in_stage(Stage::Update).after_system("init");

    schedule.add_system([&step](World& w) {
        REQUIRE(step == 2);
        REQUIRE(w.resource<Counter>().value == 20);
        step = 3;
    }, "finalize").in_stage(Stage::Update).after_system("process");

    schedule.run(world);
    REQUIRE(step == 3);
    REQUIRE(world.resource<Counter>().value == 20);
}

TEST_CASE("Schedule before_system dependency") {
    World world;
    world.init_resource<Counter>();

    Schedule schedule(0);
    int step = 0;

    schedule.add_system([&step](World& w) {
        REQUIRE(step == 1);
        w.resource<Counter>().value = 20;
        step = 2;
    }, "second").in_stage(Stage::Update);

    schedule.add_system([&step](World& w) {
        REQUIRE(step == 0);
        w.resource<Counter>().value = 10;
        step = 1;
    }, "first").in_stage(Stage::Update).before_system("second");

    schedule.run(world);
    REQUIRE(step == 2);
}

// Phase 6: Parallel resource access tests --------------------------------------

struct NonAtomicCounter {
    int value = 0;
};

TEST_CASE("Schedule parallel read-only resource access is safe") {
    World world;
    world.init_resource<NonAtomicCounter>();
    world.resource<NonAtomicCounter>().value = 42;

    std::atomic<int> sum{0};

    Schedule schedule; // parallel
    schedule.set_min_parallel_duration(std::chrono::nanoseconds{0}); // force real dispatch for this test
    schedule.add_system([&sum](World& w) {
        sum.fetch_add(w.resource<NonAtomicCounter>().value);
    }).reads_resources<NonAtomicCounter>();

    schedule.add_system([&sum](World& w) {
        sum.fetch_add(w.resource<NonAtomicCounter>().value);
    }).reads_resources<NonAtomicCounter>();

    // No conflict (both read-only), so they run in parallel.
    // TSan-verified: concurrent reads of non-atomic int are safe.
    schedule.run(world);
    REQUIRE(sum.load() == 84); // 42 + 42
}

TEST_CASE("Schedule write-read resource conflict is serialized") {
    World world;
    world.init_resource<NonAtomicCounter>();

    Schedule schedule; // parallel
    schedule.set_min_parallel_duration(std::chrono::nanoseconds{0}); // force real dispatch for this test
    // System 0 writes the resource. Because it is registered first and
    // conflicts with system 1, the DAG ensures it runs before system 1.
    schedule.add_system([](World& w) {
        w.resource<NonAtomicCounter>().value = 10;
    }).writes_resources<NonAtomicCounter>();

    schedule.add_system([](World& w) {
        // If the schedule incorrectly parallelized these systems,
        // this read would race with the write above and TSan would flag it.
        REQUIRE(w.resource<NonAtomicCounter>().value == 10);
    }).reads_resources<NonAtomicCounter>();

    schedule.run(world);
}

TEST_CASE("Schedule write-write resource conflict is serialized") {
    World world;
    world.init_resource<NonAtomicCounter>();

    Schedule schedule; // parallel
    schedule.set_min_parallel_duration(std::chrono::nanoseconds{0}); // force real dispatch for this test
    // System 0 writes first, then system 1 overwrites.
    schedule.add_system([](World& w) {
        w.resource<NonAtomicCounter>().value = 10;
    }).writes_resources<NonAtomicCounter>();

    schedule.add_system([](World& w) {
        w.resource<NonAtomicCounter>().value = 20;
    }).writes_resources<NonAtomicCounter>();

    schedule.run(world);
    REQUIRE(world.resource<NonAtomicCounter>().value == 20);
}

TEST_CASE("Schedule mixed component and resource conflict is serialized") {
    World world;
    world.spawn_with(Position{0.0f, 0.0f, 0.0f});
    world.init_resource<NonAtomicCounter>();

    Schedule schedule; // parallel
    schedule.set_min_parallel_duration(std::chrono::nanoseconds{0}); // force real dispatch for this test
    schedule.add_system([](World& w) {
        for (auto [pos] : w.query<Position>()) {
            pos.x = 1.0f;
        }
        w.resource<NonAtomicCounter>().value = 100;
    }).writes_components<Position>().writes_resources<NonAtomicCounter>();

    schedule.add_system([](World& w) {
        for (auto [pos] : w.query<Position>()) {
            REQUIRE(pos.x == 1.0f);
        }
        REQUIRE(w.resource<NonAtomicCounter>().value == 100);
    }).reads_components<Position>().reads_resources<NonAtomicCounter>();

    schedule.run(world);
}

// Phase 8: adaptive cost-aware dispatch ------------------------------------

TEST_CASE("Schedule keeps a cheap batch inline on its first run") {
    World world;
    world.init_resource<Counter>();

    Schedule schedule; // default threshold (20us), no warmup yet
    std::atomic<std::thread::id> system_thread{};
    schedule.add_system([&](World& w) {
        system_thread = std::this_thread::get_id();
        w.resource<Counter>().value++;
    }).writes_resources<Counter>();
    schedule.add_system([](World& w) {
        w.resource<Counter>().value++;
    }); // no declared access -> independent, would batch with the system above

    schedule.run(world);
    REQUIRE(world.resource<Counter>().value == 2);
    // First run has no prior timing measurement for either system (assumed
    // cheap), so the batch should run inline on the calling thread rather
    // than being dispatched to the pool.
    REQUIRE(system_thread.load() == std::this_thread::get_id());
}

TEST_CASE("Schedule dispatches to real threads once systems are measured as expensive") {
    World world;

    Schedule schedule; // default threads, default 20us threshold
    std::atomic<int> concurrent{0};
    std::atomic<int> max_concurrent{0};

    auto slow_system = [&](World&) {
        int c = ++concurrent;
        int prev = max_concurrent.load();
        while (c > prev && !max_concurrent.compare_exchange_weak(prev, c)) {}
        std::this_thread::sleep_for(std::chrono::microseconds(200)); // well above the 20us threshold
        --concurrent;
    };
    schedule.add_system(slow_system).writes_components<Position>();
    schedule.add_system(slow_system).writes_components<Velocity>();

    schedule.run(world); // warms up last-measured duration for both systems
    max_concurrent = 0;
    schedule.run(world); // should now dispatch both to the pool concurrently

    REQUIRE(max_concurrent.load() == 2);
}
