#include <catch2/catch_test_macros.hpp>
#include "test_components.hpp"

using namespace campello::core;

TEST_CASE("Commands deferred spawn_with") {
    World world;
    world.commands().spawn_with(Position{1.0f, 2.0f, 3.0f});
    REQUIRE(world.query<Position>().count() == 0);
    world.apply_commands();
    REQUIRE(world.query<Position>().count() == 1);
}

TEST_CASE("Commands deferred insert and remove") {
    World world;
    Entity e = world.spawn();
    world.commands().insert<Position>(e, 1.0f, 2.0f, 3.0f);
    REQUIRE(!world.has<Position>(e));
    world.apply_commands();
    REQUIRE(world.has<Position>(e));

    world.commands().remove<Position>(e);
    world.apply_commands();
    REQUIRE(!world.has<Position>(e));
}

TEST_CASE("Commands deferred despawn") {
    World world;
    Entity e = world.spawn_with(Position{0.0f, 0.0f, 0.0f});
    REQUIRE(world.is_alive(e));
    world.commands().despawn(e);
    REQUIRE(world.is_alive(e));
    world.apply_commands();
    REQUIRE(!world.is_alive(e));
}
