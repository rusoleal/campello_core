#include <catch2/catch_test_macros.hpp>
#include <campello/core/world.hpp>
#include <campello/core/snapshot.hpp>
#include <string>

using namespace campello::core;

struct SnapshotPos {
    float x = 0.0f;
    float y = 0.0f;
};

struct SnapshotVel {
    float dx = 0.0f;
    float dy = 0.0f;
};

template<>
struct campello::core::ComponentTraits<SnapshotPos> : ComponentTraitsBase<SnapshotPos> {
    static constexpr std::string_view name = "SnapshotPos";
    static void reflect(ComponentBuilder& b) {
        b.property("x", &SnapshotPos::x);
        b.property("y", &SnapshotPos::y);
    }
};

template<>
struct campello::core::ComponentTraits<SnapshotVel> : ComponentTraitsBase<SnapshotVel> {
    static constexpr std::string_view name = "SnapshotVel";
    static void reflect(ComponentBuilder& b) {
        b.property("dx", &SnapshotVel::dx);
        b.property("dy", &SnapshotVel::dy);
    }
};

TEST_CASE("Snapshot serializes and restores basic components", "[snapshot]") {
    World world;
    world.register_component<SnapshotPos>();
    world.register_component<SnapshotVel>();

    Entity e1 = world.spawn();
    world.insert<SnapshotPos>(e1, SnapshotPos{1.0f, 2.0f});
    world.insert<SnapshotVel>(e1, SnapshotVel{0.5f, -0.5f});

    Entity e2 = world.spawn();
    world.insert<SnapshotPos>(e2, SnapshotPos{3.0f, 4.0f});

    std::string snap = world.snapshot();
    REQUIRE(!snap.empty());

    World restored;
    restored.register_component<SnapshotPos>();
    restored.register_component<SnapshotVel>();
    restored.restore(snap);

    auto q = restored.query<SnapshotPos>();
    REQUIRE(q.count() == 2);

    int found = 0;
    q.each([&](const SnapshotPos& p) {
        if (p.x == 1.0f && p.y == 2.0f) {
            ++found;
        } else if (p.x == 3.0f && p.y == 4.0f) {
            ++found;
        }
    });
    REQUIRE(found == 2);

    auto qv = restored.query<SnapshotPos, SnapshotVel>();
    REQUIRE(qv.count() == 1);
    qv.each([&](const SnapshotPos& p, const SnapshotVel& v) {
        REQUIRE(p.x == 1.0f);
        REQUIRE(p.y == 2.0f);
        REQUIRE(v.dx == 0.5f);
        REQUIRE(v.dy == -0.5f);
    });
}

TEST_CASE("Snapshot produces valid JSON", "[snapshot]") {
    World world;
    world.register_component<SnapshotPos>();

    Entity e = world.spawn();
    world.insert<SnapshotPos>(e, SnapshotPos{1.0f, 2.0f});

    std::string snap = world.snapshot();
    REQUIRE(snap.find("{\"entities\":[") == 0);
    REQUIRE(snap.find("]}") == snap.size() - 2);
}

TEST_CASE("Snapshot of empty world", "[snapshot]") {
    World world;
    world.register_component<SnapshotPos>();

    std::string snap = world.snapshot();
    REQUIRE(snap == "{\"entities\":[]}");

    World restored;
    restored.register_component<SnapshotPos>();
    restored.restore(snap);

    auto q = restored.query<SnapshotPos>();
    REQUIRE(q.count() == 0);
}

TEST_CASE("Restore clears existing entities", "[snapshot]") {
    World world;
    world.register_component<SnapshotPos>();

    Entity e = world.spawn();
    world.insert<SnapshotPos>(e, SnapshotPos{1.0f, 2.0f});

    std::string snap = world.snapshot();

    world.spawn();
    world.spawn();
    REQUIRE(world.query<SnapshotPos>().count() == 1);

    world.restore(snap);
    REQUIRE(world.query<SnapshotPos>().count() == 1);
}

TEST_CASE("Clear removes all entities", "[snapshot]") {
    World world;
    world.register_component<SnapshotPos>();
    world.register_component<SnapshotVel>();

    Entity e1 = world.spawn();
    world.insert<SnapshotPos>(e1, SnapshotPos{1.0f, 2.0f});
    world.insert<SnapshotVel>(e1, SnapshotVel{0.1f, 0.2f});

    Entity e2 = world.spawn();
    world.insert<SnapshotPos>(e2, SnapshotPos{3.0f, 4.0f});

    world.clear();

    REQUIRE(world.query<SnapshotPos>().count() == 0);
    REQUIRE(world.query<SnapshotVel>().count() == 0);
}

TEST_CASE("Snapshot with multiple chunks", "[snapshot]") {
    World world;
    world.register_component<SnapshotPos>();

    // Spawn enough entities to span multiple chunks
    for (int i = 0; i < 2000; ++i) {
        Entity e = world.spawn();
        world.insert<SnapshotPos>(e, SnapshotPos{static_cast<float>(i), static_cast<float>(i * 2)});
    }

    std::string snap = world.snapshot();

    World restored;
    restored.register_component<SnapshotPos>();
    restored.restore(snap);

    auto q = restored.query<SnapshotPos>();
    REQUIRE(q.count() == 2000);
}

TEST_CASE("Snapshot with despawned entities skipped", "[snapshot]") {
    World world;
    world.register_component<SnapshotPos>();

    Entity e1 = world.spawn();
    world.insert<SnapshotPos>(e1, SnapshotPos{1.0f, 2.0f});

    Entity e2 = world.spawn();
    world.insert<SnapshotPos>(e2, SnapshotPos{3.0f, 4.0f});

    world.despawn(e1);

    std::string snap = world.snapshot();

    World restored;
    restored.register_component<SnapshotPos>();
    restored.restore(snap);

    auto q = restored.query<SnapshotPos>();
    REQUIRE(q.count() == 1);
    q.each([&](const SnapshotPos& p) {
        REQUIRE(p.x == 3.0f);
        REQUIRE(p.y == 4.0f);
    });
}

TEST_CASE("Restore with missing component type is ignored", "[snapshot]") {
    World world;
    world.register_component<SnapshotPos>();
    world.register_component<SnapshotVel>();

    Entity e = world.spawn();
    world.insert<SnapshotPos>(e, SnapshotPos{1.0f, 2.0f});
    world.insert<SnapshotVel>(e, SnapshotVel{0.5f, -0.5f});

    std::string snap = world.snapshot();

    World restored;
    restored.register_component<SnapshotPos>();
    // SnapshotVel NOT registered
    restored.restore(snap);

    auto q = restored.query<SnapshotPos>();
    REQUIRE(q.count() == 1);
    q.each([&](const SnapshotPos& p) {
        REQUIRE(p.x == 1.0f);
        REQUIRE(p.y == 2.0f);
    });
    REQUIRE(restored.query<SnapshotVel>().count() == 0);
}

struct Name {
    std::string value;
};

template<>
struct campello::core::ComponentTraits<Name> : ComponentTraitsBase<Name> {
    static constexpr std::string_view name = "Name";
    static void reflect(ComponentBuilder& b) {
        b.property("value", &Name::value);
    }
};

TEST_CASE("Snapshot round-trip preserves string components", "[snapshot]") {
    World world;
    world.register_component<Name>();

    Entity e = world.spawn();
    world.insert<Name>(e, Name{"hello world"});

    std::string snap = world.snapshot();

    World restored;
    restored.register_component<Name>();
    restored.restore(snap);

    auto q = restored.query<Name>();
    REQUIRE(q.count() == 1);
    q.each([&](const Name& n) {
        REQUIRE(n.value == "hello world");
    });
}

TEST_CASE("Snapshot on_remove hooks are called during clear", "[snapshot]") {
    World world;
    world.register_component<SnapshotPos>();

    int remove_count = 0;
    world.on_remove<SnapshotPos>([&](Entity, SnapshotPos&) {
        ++remove_count;
    });

    Entity e = world.spawn();
    world.insert<SnapshotPos>(e, SnapshotPos{1.0f, 2.0f});

    world.clear();
    REQUIRE(remove_count == 1);
}
