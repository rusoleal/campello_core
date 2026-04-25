#include <catch2/catch_test_macros.hpp>
#include <campello/core/types.hpp>

using namespace campello::core;

TEST_CASE("Entity creation and decomposition") {
    Entity e = make_entity(42, 7);
    REQUIRE(entity_index(e) == 42);
    REQUIRE(entity_generation(e) == 7);
}

TEST_CASE("Null entity") {
    REQUIRE(null_entity == 0);
}
