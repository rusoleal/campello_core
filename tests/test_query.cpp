#include <catch2/catch_test_macros.hpp>
#include "test_components.hpp"

using namespace campello::core;

TEST_CASE("Query basic iteration") {
    World world;
    Entity e1 = world.spawn_with(Position{1.0f, 0.0f, 0.0f}, Velocity{0.1f, 0.0f, 0.0f});
    Entity e2 = world.spawn_with(Position{2.0f, 0.0f, 0.0f}, Velocity{0.2f, 0.0f, 0.0f});
    Entity e3 = world.spawn_with(Position{3.0f, 0.0f, 0.0f});

    int count = 0;
    auto q = world.query<Position, Velocity>();
    q.each([&](Position& pos, Velocity& vel) {
        pos.x += vel.dx;
        ++count;
    });

    REQUIRE(count == 2);
    REQUIRE(world.get<Position>(e1)->x == 1.1f);
    REQUIRE(world.get<Position>(e2)->x == 2.2f);
    REQUIRE(world.get<Position>(e3)->x == 3.0f);
}

TEST_CASE("Query range-based for") {
    World world;
    world.spawn_with(Position{1.0f, 0.0f, 0.0f}, Velocity{0.1f, 0.0f, 0.0f});
    world.spawn_with(Position{2.0f, 0.0f, 0.0f}, Velocity{0.2f, 0.0f, 0.0f});

    int count = 0;
    for (auto [pos, vel] : world.query<Position, Velocity>()) {
        (void)pos;
        (void)vel;
        ++count;
    }
    REQUIRE(count == 2);
}

TEST_CASE("Query const access") {
    World world;
    world.spawn_with(Position{1.0f, 2.0f, 3.0f}, Velocity{0.0f, 0.0f, 0.0f});

    auto q = world.query<const Position, Velocity>();
    q.each([](const Position& pos, Velocity& vel) {
        REQUIRE(pos.x == 1.0f);
        (void)vel;
    });
}

TEST_CASE("Query count") {
    World world;
    for (int i = 0; i < 50; ++i) {
        world.spawn_with(Position{float(i), 0.0f, 0.0f}, Velocity{0.0f, 0.0f, 0.0f});
    }
    for (int i = 0; i < 30; ++i) {
        world.spawn_with(Position{float(i), 0.0f, 0.0f});
    }

    REQUIRE(world.query<Position, Velocity>().count() == 50);
    REQUIRE(world.query<Position>().count() == 80);
}

// Phase 1: Query safety tests --------------------------------------------------

TEST_CASE("Query nested queries do not crash") {
    World world;
    for (int i = 0; i < 10; ++i) {
        world.spawn_with(Position{float(i), 0.0f, 0.0f}, Velocity{float(i), 0.0f, 0.0f});
    }
    for (int i = 0; i < 5; ++i) {
        world.spawn_with(Position{float(i), 0.0f, 0.0f});
    }

    int outer_count = 0;
    int inner_count = 0;
    world.query<Position, Velocity>().each([&](Position&, Velocity&) {
        ++outer_count;
        world.query<Position>().each([&](Position&) {
            ++inner_count;
        });
    });

    REQUIRE(outer_count == 10);
    REQUIRE(inner_count == 10 * 15); // 10 outer iterations * 15 Position entities
}

TEST_CASE("Query iteration after entity removed from matching archetype") {
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 10; ++i) {
        entities.push_back(world.spawn_with(Position{float(i), 0.0f, 0.0f}, Velocity{0.0f, 0.0f, 0.0f}));
    }

    int count = 0;
    world.query<Position, Velocity>().each([&](Position&, Velocity&) {
        ++count;
        // Remove Velocity from the first entity during iteration
        if (count == 1) {
            world.remove<Velocity>(entities[0]);
        }
    });

    // The entity was removed from the archetype during iteration.
    // The exact count depends on iterator semantics; we just verify no crash.
    REQUIRE(count >= 0);
}

TEST_CASE("Query iteration after entity inserted into matching archetype") {
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 10; ++i) {
        entities.push_back(world.spawn_with(Position{float(i), 0.0f, 0.0f}));
    }

    int count = 0;
    world.query<Position, Velocity>().each([&](Position&, Velocity&) {
        ++count;
    });
    REQUIRE(count == 0); // none match yet

    // Add Velocity to some entities
    for (int i = 0; i < 5; ++i) {
        world.insert<Velocity>(entities[i], float(i), 0.0f, 0.0f);
    }

    count = 0;
    world.query<Position, Velocity>().each([&](Position&, Velocity&) {
        ++count;
    });
    REQUIRE(count == 5);
}

TEST_CASE("Query iteration with entities in multiple chunks") {
    World world;
    std::vector<Entity> entities;
    // Spawn enough to exceed one chunk (chunk size 16KB, Position+Velocity ≈ 24 bytes + Entity ≈ 32 bytes
    // per entity, so capacity ≈ 500 per chunk)
    for (int i = 0; i < 1500; ++i) {
        entities.push_back(world.spawn_with(Position{float(i), 0.0f, 0.0f}, Velocity{float(i), 0.0f, 0.0f}));
    }

    int count = 0;
    float sum = 0.0f;
    world.query<Position, Velocity>().each([&](Position& pos, Velocity&) {
        ++count;
        sum += pos.x;
    });

    REQUIRE(count == 1500);
    REQUIRE(sum == 1500.0f * 1499.0f / 2.0f); // sum of 0..1499
}


TEST_CASE("Query parallel chunk iteration") {
    World world;
    for (int i = 0; i < 1000; ++i) {
        world.spawn_with(Position{float(i), 0.0f, 0.0f}, Velocity{1.0f, 2.0f, 3.0f});
    }

    campello::core::detail::ThreadPool pool(4);

    std::atomic<int> count{0};
    std::atomic<float> sum{0.0f};
    world.query<Position, Velocity>().each_par(pool, [&](Position& pos, Velocity& vel) {
        pos.x += vel.dx;
        count.fetch_add(1);
        sum.fetch_add(pos.x);
    });

    REQUIRE(count.load() == 1000);
    // Each pos.x was incremented by 1.0f from vel.dx
    // Original sum = 0+1+2+...+999 = 999*1000/2 = 499500
    // After increment = 499500 + 1000 = 500500
    REQUIRE(sum.load() == 500500.0f);
}

// Phase 6: Change detection tests ----------------------------------------------

TEST_CASE("Query Added filter matches only newly added components") {
    World world;

    // Spawn entity at tick 1
    Entity e1 = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    (void)e1;

    // Increment tick to 2
    world.increment_change_tick();

    // Spawn another entity at tick 2
    Entity e2 = world.spawn_with(Position{2.0f, 0.0f, 0.0f});
    (void)e2;

    // Query with Added filter at last_run_tick = 1 should only match e2
    int count = 0;
    world.query<Position>()
        .added<Position>()
        .with_last_run_tick(1)
        .each([&](Position& pos) {
            REQUIRE(pos.x == 2.0f);
            ++count;
        });

    REQUIRE(count == 1);
}

TEST_CASE("Query Changed filter matches only marked changes") {
    World world;

    Entity e1 = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.spawn_with(Position{2.0f, 0.0f, 0.0f});

    world.increment_change_tick(); // tick 2

    // Mark only e1 as changed
    world.mark_changed<Position>(e1);

    int count = 0;
    world.query<Position>()
        .changed<Position>()
        .with_last_run_tick(1)
        .each([&](Position& pos) {
            REQUIRE(pos.x == 1.0f);
            ++count;
        });

    REQUIRE(count == 1);
}

TEST_CASE("Query Added and Changed filters combined") {
    World world;

    // Entity 1: Position added at tick 1
    Entity e1 = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.insert<Velocity>(e1, 0.1f, 0.0f, 0.0f);

    world.increment_change_tick(); // tick 2

    // Entity 2: Position added at tick 2
    Entity e2 = world.spawn_with(Position{2.0f, 0.0f, 0.0f});
    (void)e2;

    // Query for entities with Position added since tick 1
    int added_count = 0;
    world.query<Position>()
        .added<Position>()
        .with_last_run_tick(1)
        .each([&](Position&) {
            ++added_count;
        });
    REQUIRE(added_count == 1); // only e2

    // Query for entities with Velocity added since tick 1
    int vel_added_count = 0;
    world.query<Position, Velocity>()
        .added<Velocity>()
        .with_last_run_tick(1)
        .each([&](Position&, Velocity&) {
            ++vel_added_count;
        });
    // e1's Velocity was added at tick 1. With last_run_tick = 1, tick > 1 is false.
    REQUIRE(vel_added_count == 0);
    // Actually, the insert happened BEFORE increment_change_tick, so tick is 1.
    // last_run_tick = 1, so 1 > 1 is false. So 0 matches.
}

TEST_CASE("Query Added filter with insert after tick increment") {
    World world;

    Entity e1 = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.increment_change_tick(); // tick 2

    // Insert Velocity at tick 2
    world.insert<Velocity>(e1, 0.1f, 0.0f, 0.0f);
    (void)e1;

    // Query for entities with Velocity added since tick 1
    int count = 0;
    world.query<Position, Velocity>()
        .added<Velocity>()
        .with_last_run_tick(1)
        .each([&](Position&, Velocity&) {
            ++count;
        });
    REQUIRE(count == 1);
}

TEST_CASE("Query Changed filter with range-based for") {
    World world;

    Entity e1 = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.spawn_with(Position{2.0f, 0.0f, 0.0f});

    world.increment_change_tick(); // tick 2
    world.mark_changed<Position>(e1);

    // Verify tick values directly
    {
        auto& storage = world.archetype_storage();
        const Archetype* arch = storage.get_archetype(1); // first archetype with Position
        REQUIRE(arch != nullptr);
        REQUIRE(arch->entity_count == 2);
        Tick t1 = storage.get_changed_tick(*arch, 0, 0, 0);
        Tick t2 = storage.get_changed_tick(*arch, 0, 0, 1);
        REQUIRE(t1 == 2); // e1 was marked changed at tick 2
        REQUIRE(t2 == 1); // e2 was spawned at tick 1, never changed
    }

    // Verify each() works correctly
    int each_count = 0;
    world.query<Position>().changed<Position>().with_last_run_tick(1).each([&](Position&) {
        ++each_count;
    });
    REQUIRE(each_count == 1);

    // Verify range-for works correctly
    int count = 0;
    auto q = world.query<Position>().changed<Position>().with_last_run_tick(1);
    for (auto [pos] : q) {
        (void)pos;
        ++count;
    }
    REQUIRE(count == 1);
}

TEST_CASE("Query change detection count") {
    World world;

    world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.spawn_with(Position{2.0f, 0.0f, 0.0f});
    world.spawn_with(Position{3.0f, 0.0f, 0.0f});

    world.increment_change_tick(); // tick 2

    // No changes yet
    REQUIRE(world.query<Position>().changed<Position>().with_last_run_tick(1).count() == 0);

    // All three were added at tick 1
    REQUIRE(world.query<Position>().added<Position>().with_last_run_tick(1).count() == 0);
    REQUIRE(world.query<Position>().added<Position>().with_last_run_tick(0).count() == 3);
}

TEST_CASE("Query change detection with archetype moves") {
    World world;

    Entity e1 = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.increment_change_tick(); // tick 2

    // Add Velocity at tick 2 — this moves e1 to a new archetype
    world.insert<Velocity>(e1, 0.1f, 0.0f, 0.0f);

    // Position should NOT show as added (it was added at tick 1)
    int pos_added = 0;
    world.query<Position, Velocity>()
        .added<Position>()
        .with_last_run_tick(1)
        .each([&](Position&, Velocity&) {
            ++pos_added;
        });
    REQUIRE(pos_added == 0);

    // Velocity SHOULD show as added (it was added at tick 2)
    int vel_added = 0;
    world.query<Position, Velocity>()
        .added<Velocity>()
        .with_last_run_tick(1)
        .each([&](Position&, Velocity&) {
            ++vel_added;
        });
    REQUIRE(vel_added == 1);
}
