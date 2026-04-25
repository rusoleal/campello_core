#include <catch2/catch_test_macros.hpp>
#include "test_components.hpp"

using namespace campello::core;

TEST_CASE("Reflection registration") {
    World world;
    world.register_component<Position>();

    const ComponentInfo* info = world.reflect_registry().info(component_type_id<Position>());
    REQUIRE(info != nullptr);
    REQUIRE(info->name == "Position");
    REQUIRE(info->size == sizeof(Position));
    REQUIRE(info->properties.size() == 3);

    const Property* px = info->find_property("x");
    REQUIRE(px != nullptr);
    REQUIRE(px->offset == offsetof(Position, x));
}

TEST_CASE("World visit components") {
    World world;
    Entity e = world.spawn_with(Position{1.0f, 2.0f, 3.0f});

    int count = 0;
    world.visit(e, [&](ComponentId /*id*/, void* ptr, const ComponentInfo* info) {
        if (info && info->name == "Position") {
            Position* pos = static_cast<Position*>(ptr);
            REQUIRE(pos->x == 1.0f);
            REQUIRE(pos->y == 2.0f);
            REQUIRE(pos->z == 3.0f);
        }
        ++count;
    });
    REQUIRE(count == 1);
}
