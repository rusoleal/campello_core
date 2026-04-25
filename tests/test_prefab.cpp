#include <catch2/catch_test_macros.hpp>
#include <campello/core/core.hpp>
#include "test_components.hpp"
#include <string>

using namespace campello::core;

// Test-specific component — in anonymous namespace to avoid ODR violations
namespace {
struct Name {
    std::string value;
};
} // namespace

namespace campello::core {
template<> struct ComponentTraits<Name> : ComponentTraitsBase<Name> {
    static constexpr std::string_view name = "Name";
};
} // namespace campello::core

// ------------------------------------------------------------------
// Basic clone
// ------------------------------------------------------------------

TEST_CASE("Clone copies all components") {
    World world;
    Entity source = world.spawn_with(
        Position{10.0f, 20.0f, 30.0f},
        Velocity{1.0f, 2.0f, 3.0f},
        Health{50}
    );

    Entity clone_e = world.clone(source);
    (void)clone_e;

    REQUIRE(clone_e != source);
    REQUIRE(world.is_alive(clone_e));

    auto* p = world.get<Position>(clone_e);
    auto* v = world.get<Velocity>(clone_e);
    auto* h = world.get<Health>(clone_e);

    REQUIRE(p != nullptr);
    REQUIRE(v != nullptr);
    REQUIRE(h != nullptr);

    REQUIRE(p->x == 10.0f);
    REQUIRE(p->y == 20.0f);
    REQUIRE(p->z == 30.0f);
    REQUIRE(v->dx == 1.0f);
    REQUIRE(v->dy == 2.0f);
    REQUIRE(v->dz == 3.0f);
    REQUIRE(h->value == 50);
}

TEST_CASE("Clone of empty entity produces empty entity") {
    World world;
    Entity source = world.spawn();
    Entity clone_e = world.clone(source);
    (void)clone_e;

    REQUIRE(clone_e != source);
    REQUIRE(world.is_alive(clone_e));
    REQUIRE(world.query<Position>().count() == 0);
}

TEST_CASE("Clone of dead entity returns null_entity") {
    World world;
    Entity source = world.spawn_with(Position{1, 2, 3});
    world.despawn(source);
    Entity clone_e = world.clone(source);
    (void)clone_e;
    REQUIRE(clone_e == null_entity);
}

// ------------------------------------------------------------------
// Clone independence
// ------------------------------------------------------------------

TEST_CASE("Clone is independent of source") {
    World world;
    Entity source = world.spawn_with(Position{1.0f, 2.0f, 3.0f});
    Entity clone_e = world.clone(source);
    (void)clone_e;

    world.get<Position>(source)->x = 99.0f;

    REQUIRE(world.get<Position>(clone_e)->x == 1.0f);
}

TEST_CASE("Clone non-trivial types") {
    World world;
    Entity source = world.spawn_with(Name{"original"});
    Entity clone_e = world.clone(source);
    (void)clone_e;

    REQUIRE(world.get<Name>(clone_e)->value == "original");

    world.get<Name>(source)->value = "modified";
    REQUIRE(world.get<Name>(clone_e)->value == "original");
}

// ------------------------------------------------------------------
// Clone with on_add hooks
// ------------------------------------------------------------------

TEST_CASE("Clone fires on_add hooks") {
    World world;
    int hook_count = 0;
    world.on_add<Position>([&](Entity, Position&) { ++hook_count; });

    Entity source = world.spawn_with(Position{1.0f, 2.0f, 3.0f});
    REQUIRE(hook_count == 1); // fired for source

    Entity clone_e = world.clone(source);
    (void)clone_e;
    REQUIRE(hook_count == 2); // fired for clone
}

TEST_CASE("Clone fires on_add hooks for all components") {
    World world;
    int pos_hooks = 0;
    int vel_hooks = 0;
    world.on_add<Position>([&](Entity, Position&) { ++pos_hooks; });
    world.on_add<Velocity>([&](Entity, Velocity&) { ++vel_hooks; });

    Entity source = world.spawn_with(Position{1, 2, 3}, Velocity{4, 5, 6});
    Entity clone_e = world.clone(source);
    (void)clone_e;

    REQUIRE(pos_hooks == 2);
    REQUIRE(vel_hooks == 2);
}

// ------------------------------------------------------------------
// clone_many
// ------------------------------------------------------------------

TEST_CASE("clone_many produces independent copies") {
    World world;
    Entity source = world.spawn_with(Position{5.0f, 10.0f, 15.0f}, Health{42});
    auto clones = world.clone_many(source, 10);

    REQUIRE(clones.size() == 10);
    for (Entity e : clones) {
        REQUIRE(e != source);
        REQUIRE(world.is_alive(e));
        REQUIRE(world.get<Position>(e)->x == 5.0f);
        REQUIRE(world.get<Health>(e)->value == 42);
    }
}

TEST_CASE("clone_many returns empty for dead source") {
    World world;
    Entity source = world.spawn_with(Position{1, 2, 3});
    world.despawn(source);
    auto clones = world.clone_many(source, 5);
    REQUIRE(clones.empty());
}

TEST_CASE("clone_many with count 0 returns empty") {
    World world;
    Entity source = world.spawn_with(Position{1, 2, 3});
    auto clones = world.clone_many(source, 0);
    REQUIRE(clones.empty());
}

TEST_CASE("clone_many fires on_add hooks") {
    World world;
    int hook_count = 0;
    world.on_add<Position>([&](Entity, Position&) { ++hook_count; });

    Entity source = world.spawn_with(Position{1.0f, 2.0f, 3.0f});
    auto clones = world.clone_many(source, 5);

    REQUIRE(hook_count == 6); // 1 for source + 5 for clones
}

// ------------------------------------------------------------------
// Clone archetype consistency
// ------------------------------------------------------------------

TEST_CASE("Clone creates entity in same archetype") {
    World world;
    Entity source = world.spawn_with(Position{1, 2, 3}, Velocity{4, 5, 6});
    Entity clone_e = world.clone(source);
    (void)clone_e;

    auto count = world.query<Position, Velocity>().count();
    REQUIRE(count == 2); // source + clone
}

TEST_CASE("clone_many creates entities in same archetype") {
    World world;
    Entity source = world.spawn_with(Position{1, 2, 3});
    auto clones = world.clone_many(source, 100);

    REQUIRE(world.query<Position>().count() == 101);
}

// ------------------------------------------------------------------
// Clone with multi-chunk archetype
// ------------------------------------------------------------------

TEST_CASE("Query each_with_entity yields correct entities") {
    World world;
    Entity e1 = world.spawn_with(Position{1, 2, 3});
    Entity e2 = world.spawn_with(Position{4, 5, 6});
    Entity e3 = world.spawn_with(Position{7, 8, 9});

    int count = 0;
    world.query<Position>().each_with_entity([&](Entity e, Position& p) {
        ++count;
        if (e == e1) { REQUIRE(p.x == 1); }
        else if (e == e2) { REQUIRE(p.x == 4); }
        else if (e == e3) { REQUIRE(p.x == 7); }
    });
    REQUIRE(count == 3);
}

TEST_CASE("clone_many spans multiple chunks") {
    World world;
    Entity source = world.spawn_with(Position{1.0f, 2.0f, 3.0f});

    // Spawn enough clones to exceed chunk capacity
    auto clones = world.clone_many(source, 5000);

    REQUIRE(clones.size() == 5000);

    for (Entity e : clones) {
        auto* p = world.get<Position>(e);
        REQUIRE(p != nullptr);
        REQUIRE(p->x == 1.0f);
        REQUIRE(p->y == 2.0f);
        REQUIRE(p->z == 3.0f);
    }
}
