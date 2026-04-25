#include <catch2/catch_test_macros.hpp>
#include "test_components.hpp"

using namespace campello::core;

TEST_CASE("World spawn and despawn") {
    World world;
    Entity e = world.spawn();
    REQUIRE(world.is_alive(e));
    world.despawn(e);
    REQUIRE(!world.is_alive(e));
}

TEST_CASE("World insert and get") {
    World world;
    Entity e = world.spawn();
    world.insert<Position>(e, 1.0f, 2.0f, 3.0f);

    Position* pos = world.get<Position>(e);
    REQUIRE(pos != nullptr);
    REQUIRE(pos->x == 1.0f);
    REQUIRE(pos->y == 2.0f);
    REQUIRE(pos->z == 3.0f);
}

TEST_CASE("World has component") {
    World world;
    Entity e = world.spawn();
    REQUIRE(!world.has<Position>(e));
    world.insert<Position>(e, 0.0f, 0.0f, 0.0f);
    REQUIRE(world.has<Position>(e));
    REQUIRE(!world.has<Velocity>(e));
}

TEST_CASE("World remove component") {
    World world;
    Entity e = world.spawn();
    world.insert<Position>(e, 1.0f, 2.0f, 3.0f);
    world.insert<Velocity>(e, 0.1f, 0.2f, 0.3f);
    REQUIRE(world.has<Position>(e));
    REQUIRE(world.has<Velocity>(e));

    world.remove<Velocity>(e);
    REQUIRE(world.has<Position>(e));
    REQUIRE(!world.has<Velocity>(e));
}

TEST_CASE("World spawn_with") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 2.0f, 3.0f}, Velocity{0.1f, 0.2f, 0.3f});
    REQUIRE(world.is_alive(e));
    REQUIRE(world.has<Position>(e));
    REQUIRE(world.has<Velocity>(e));
}

TEST_CASE("World multiple entities") {
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 100; ++i) {
        entities.push_back(world.spawn_with(Position{float(i), 0.0f, 0.0f}));
    }
    for (int i = 0; i < 100; ++i) {
        Position* pos = world.get<Position>(entities[i]);
        REQUIRE(pos != nullptr);
        REQUIRE(pos->x == float(i));
    }
}


TEST_CASE("World spawn_many") {
    World world;
    auto entities = world.spawn_many(100, Position{1.0f, 2.0f, 3.0f}, Velocity{0.1f, 0.2f, 0.3f});
    REQUIRE(entities.size() == 100);
    for (Entity e : entities) {
        REQUIRE(world.is_alive(e));
        REQUIRE(world.has<Position>(e));
        REQUIRE(world.has<Velocity>(e));
        REQUIRE(world.get<Position>(e)->x == 1.0f);
        REQUIRE(world.get<Velocity>(e)->dx == 0.1f);
    }
}

TEST_CASE("World despawn_many") {
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 50; ++i) {
        entities.push_back(world.spawn_with(Position{float(i), 0.0f, 0.0f}));
    }
    world.despawn_many(entities);
    for (Entity e : entities) {
        REQUIRE(!world.is_alive(e));
    }
}

TEST_CASE("World insert_many") {
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 50; ++i) {
        entities.push_back(world.spawn_with(Position{float(i), 0.0f, 0.0f}));
    }
    world.insert_many(entities, Velocity{1.0f, 2.0f, 3.0f});
    for (Entity e : entities) {
        REQUIRE(world.has<Velocity>(e));
        REQUIRE(world.get<Velocity>(e)->dx == 1.0f);
    }
}

TEST_CASE("World on_add hook") {
    World world;
    int hook_count = 0;
    world.on_add<Position>([&](Entity, Position&) {
        ++hook_count;
    });

    Entity e = world.spawn();
    world.insert<Position>(e, 1.0f, 2.0f, 3.0f);
    REQUIRE(hook_count == 1);

    world.insert<Position>(e, 4.0f, 5.0f, 6.0f); // already has component, no hook
    REQUIRE(hook_count == 1);

    Entity e2 = world.spawn();
    world.insert<Position>(e2, 0.0f, 0.0f, 0.0f);
    REQUIRE(hook_count == 2);
}

TEST_CASE("World on_remove hook") {
    World world;
    int hook_count = 0;
    world.on_remove<Position>([&](Entity, Position&) {
        ++hook_count;
    });

    Entity e = world.spawn();
    world.insert<Position>(e, 1.0f, 2.0f, 3.0f);
    world.remove<Position>(e);
    REQUIRE(hook_count == 1);

    world.remove<Position>(e); // already removed, no hook
    REQUIRE(hook_count == 1);
}

TEST_CASE("World on_remove called during despawn") {
    World world;
    int hook_count = 0;
    world.on_remove<Position>([&](Entity, Position&) {
        ++hook_count;
    });

    Entity e = world.spawn_with(Position{1.0f, 2.0f, 3.0f});
    world.despawn(e);
    REQUIRE(hook_count == 1);
}

// Phase 6: Error handling edge cases -------------------------------------------

TEST_CASE("World get on dead entity returns nullptr") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.despawn(e);
    REQUIRE(world.get<Position>(e) == nullptr);
}

TEST_CASE("World has on dead entity returns false") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.despawn(e);
    REQUIRE(!world.has<Position>(e));
}

TEST_CASE("World remove on dead entity is no-op") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.despawn(e);
    world.remove<Position>(e); // should not crash
    REQUIRE(true);
}

TEST_CASE("World despawn on dead entity is no-op") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.despawn(e);
    world.despawn(e); // should not crash
    REQUIRE(!world.is_alive(e));
}

TEST_CASE("World res without init returns null wrapper") {
    World world;
    auto r = world.res<Position>();
    REQUIRE(!r); // operator bool returns false
    REQUIRE(r.get() == nullptr);
}

TEST_CASE("World res_mut without init returns null wrapper") {
    World world;
    auto rm = world.res_mut<Position>();
    REQUIRE(!rm); // operator bool returns false
    REQUIRE(rm.get() == nullptr);
}

// Phase 6: Removed component tracking ------------------------------------------

TEST_CASE("World removed tracks single component removal") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.remove<Position>(e);

    auto& removed = world.removed<Position>();
    REQUIRE(removed.size() == 1);
    REQUIRE(removed[0] == e);
}

TEST_CASE("World removed tracks despawned entity components") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f}, Velocity{0.1f, 0.0f, 0.0f});
    world.despawn(e);

    auto& removed_pos = world.removed<Position>();
    auto& removed_vel = world.removed<Velocity>();

    REQUIRE(removed_pos.size() == 1);
    REQUIRE(removed_vel.size() == 1);
    REQUIRE(removed_pos[0] == e);
    REQUIRE(removed_vel[0] == e);
}

TEST_CASE("World removed is cleared on increment_change_tick") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.remove<Position>(e);

    REQUIRE(world.removed<Position>().size() == 1);

    world.increment_change_tick();

    REQUIRE(world.removed<Position>().empty());
}

TEST_CASE("World removed is empty for non-removed components") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.remove<Position>(e);

    REQUIRE(world.removed<Velocity>().empty());
}

TEST_CASE("World removed does not duplicate on double remove") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.remove<Position>(e);
    world.remove<Position>(e); // already removed, no-op

    REQUIRE(world.removed<Position>().size() == 1);
}

TEST_CASE("World removed does not track if entity already dead") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 0.0f, 0.0f});
    world.despawn(e);
    world.despawn(e); // already dead, no-op

    REQUIRE(world.removed<Position>().size() == 1);
}

TEST_CASE("World memory stats track chunk usage") {
    World world;

    auto s0 = world.memory_stats();
    REQUIRE(s0.alive_entities == 0);
    REQUIRE(s0.chunk_count == 0);
    REQUIRE(s0.total_bytes() == 0);

    Entity e = world.spawn_with(Position{1.0f, 2.0f, 3.0f});
    auto s1 = world.memory_stats();
    REQUIRE(s1.alive_entities == 1);
    REQUIRE(s1.chunk_count >= 1);
    REQUIRE(s1.chunk_data_bytes > 0);
    REQUIRE(s1.chunk_ticks_bytes > 0);
    REQUIRE(s1.entity_meta_bytes >= sizeof(EntityMeta));
    REQUIRE(s1.total_capacity >= 1);

    world.despawn(e);
    auto s2 = world.memory_stats();
    REQUIRE(s2.alive_entities == 0);
    // Chunks are not freed on despawn, so chunk_count may stay > 0
    REQUIRE(s2.chunk_count == s1.chunk_count);
    REQUIRE(s2.chunk_data_bytes == s1.chunk_data_bytes);
}
