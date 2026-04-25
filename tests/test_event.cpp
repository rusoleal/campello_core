#include <catch2/catch_test_macros.hpp>
#include <campello/core/world.hpp>

using namespace campello::core;

struct Collision {
    Entity a;
    Entity b;
    float force;
};

TEST_CASE("Event send and read") {
    World world;
    Entity e1 = world.spawn();
    Entity e2 = world.spawn();
    world.send(Collision{e1, e2, 100.0f});
    world.send(Collision{e2, e1, 50.0f});

    auto reader = world.event_reader<Collision>();
    REQUIRE(reader.size() == 2);
    REQUIRE(reader.begin()->force == 100.0f);
    REQUIRE((reader.begin() + 1)->force == 50.0f);
}

TEST_CASE("Event emit") {
    World world;
    world.emit<Collision>(null_entity, null_entity, 42.0f);

    auto reader = world.event_reader<Collision>();
    REQUIRE(reader.size() == 1);
    REQUIRE(reader.begin()->force == 42.0f);
}

TEST_CASE("Event clear") {
    World world;
    world.send(Collision{null_entity, null_entity, 1.0f});
    REQUIRE(!world.event_reader<Collision>().empty());
    world.clear_events();
    REQUIRE(world.event_reader<Collision>().empty());
}
