#include <catch2/catch_test_macros.hpp>
#include <campello/core/schedule.hpp>
#include <atomic>
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
