#include <catch2/catch_test_macros.hpp>
#include <campello/core/world.hpp>

using namespace campello::core;

TEST_CASE("Hierarchy set_parent") {
    World world;
    Entity parent = world.spawn();
    Entity child = world.spawn();

    world.set_parent(child, parent);
    REQUIRE(world.has<Parent>(child));
    REQUIRE(world.get<Parent>(child)->entity == parent);
    REQUIRE(world.has<Children>(parent));
    REQUIRE(world.get<Children>(parent)->entities.size() == 1);
    REQUIRE(world.get<Children>(parent)->entities[0] == child);
}

TEST_CASE("Hierarchy remove_parent") {
    World world;
    Entity parent = world.spawn();
    Entity child = world.spawn();
    world.set_parent(child, parent);
    REQUIRE(world.has<Parent>(child));

    world.remove_parent(child);
    REQUIRE(!world.has<Parent>(child));
    REQUIRE(world.get<Children>(parent)->entities.empty());
}

TEST_CASE("Hierarchy cascade despawn") {
    World world;
    Entity parent = world.spawn();
    Entity child1 = world.spawn();
    Entity child2 = world.spawn();
    world.set_parent(child1, parent);
    world.set_parent(child2, parent);

    REQUIRE(world.is_alive(parent));
    REQUIRE(world.is_alive(child1));
    REQUIRE(world.is_alive(child2));

    world.despawn(parent);
    REQUIRE(!world.is_alive(parent));
    REQUIRE(!world.is_alive(child1));
    REQUIRE(!world.is_alive(child2));
}

TEST_CASE("Hierarchy reparent") {
    World world;
    Entity p1 = world.spawn();
    Entity p2 = world.spawn();
    Entity child = world.spawn();
    world.set_parent(child, p1);
    REQUIRE(world.get<Children>(p1)->entities.size() == 1);

    world.set_parent(child, p2);
    REQUIRE(world.get<Children>(p1)->entities.empty());
    REQUIRE(world.get<Children>(p2)->entities.size() == 1);
    REQUIRE(world.get<Parent>(child)->entity == p2);
}

// Phase 1: Hierarchy edge cases ------------------------------------------------

TEST_CASE("Hierarchy deep nesting") {
    World world;
    const int depth = 100;
    std::vector<Entity> chain;

    Entity root = world.spawn();
    chain.push_back(root);

    for (int i = 0; i < depth; ++i) {
        Entity child = world.spawn();
        world.set_parent(child, chain.back());
        chain.push_back(child);
    }

    // Verify chain integrity
    for (std::size_t i = 1; i < chain.size(); ++i) {
        auto* p = world.get<Parent>(chain[i]);
        REQUIRE(p != nullptr);
        REQUIRE(p->entity == chain[i - 1]);
    }

    // Despawn root — all descendants should be cleaned up
    world.despawn(root);
    for (Entity e : chain) {
        REQUIRE(!world.is_alive(e));
    }
}

TEST_CASE("Hierarchy cyclic reference should be prevented") {
    World world;
    Entity a = world.spawn();
    Entity b = world.spawn();

    world.set_parent(b, a);
    REQUIRE(world.get<Parent>(b)->entity == a);

    // Attempting to make a cycle: a -> b, but b is already child of a
    // set_parent should reject this to prevent infinite recursion in despawn.
    world.set_parent(a, b);

    // Cycle was prevented — a should still have no parent
    REQUIRE(world.get<Parent>(a) == nullptr);
    REQUIRE(world.get<Parent>(b)->entity == a);

    // Despawn should work normally without infinite recursion
    world.despawn(a);
    REQUIRE(!world.is_alive(a));
    REQUIRE(!world.is_alive(b));
}

TEST_CASE("Hierarchy mass reparenting") {
    World world;
    Entity p1 = world.spawn();
    Entity p2 = world.spawn();
    std::vector<Entity> children;

    for (int i = 0; i < 200; ++i) {
        Entity c = world.spawn();
        world.set_parent(c, p1);
        children.push_back(c);
    }

    REQUIRE(world.get<Children>(p1)->entities.size() == 200);
    REQUIRE(!world.has<Children>(p2));

    // Reparent all to p2
    for (Entity c : children) {
        world.set_parent(c, p2);
    }

    REQUIRE(world.get<Children>(p1)->entities.empty());
    REQUIRE(world.get<Children>(p2)->entities.size() == 200);

    for (Entity c : children) {
        REQUIRE(world.get<Parent>(c)->entity == p2);
    }
}

TEST_CASE("Hierarchy despawn root with many descendants") {
    World world;
    Entity root = world.spawn();
    std::vector<Entity> all;
    all.push_back(root);

    // Build a wide tree: root -> 50 children, each -> 10 grandchildren
    for (int i = 0; i < 50; ++i) {
        Entity child = world.spawn();
        world.set_parent(child, root);
        all.push_back(child);
        for (int j = 0; j < 10; ++j) {
            Entity grandchild = world.spawn();
            world.set_parent(grandchild, child);
            all.push_back(grandchild);
        }
    }

    REQUIRE(all.size() == 1 + 50 + 500);

    world.despawn(root);

    for (Entity e : all) {
        REQUIRE(!world.is_alive(e));
    }
}
