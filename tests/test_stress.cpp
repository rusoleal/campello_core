#include <catch2/catch_test_macros.hpp>
#include <campello/core/core.hpp>
#include "test_components.hpp"
#include <random>
#include <unordered_map>
#include <string>

using namespace campello::core;

// Components for stress testing -------------------------------------------------

struct IntValue {
    int value = 0;
};

struct FloatValue {
    float value = 0.0f;
};

struct StringValue {
    std::string value;
    StringValue() = default;
    explicit StringValue(std::string v) : value(std::move(v)) {}
};

struct BigComponent {
    std::uint64_t data[32]; // 256 bytes
    std::uint64_t id = 0;
};

namespace campello::core {
template<> struct ComponentTraits<IntValue>    : ComponentTraitsBase<IntValue>    { static constexpr std::string_view name = "IntValue"; };
template<> struct ComponentTraits<FloatValue>  : ComponentTraitsBase<FloatValue>  { static constexpr std::string_view name = "FloatValue"; };
template<> struct ComponentTraits<StringValue> : ComponentTraitsBase<StringValue> { static constexpr std::string_view name = "StringValue"; };
template<> struct ComponentTraits<BigComponent> : ComponentTraitsBase<BigComponent> { static constexpr std::string_view name = "BigComponent"; };
} // namespace campello::core

// Deterministic pseudo-RNG for reproducible fuzz tests --------------------------

class FastLCG {
    std::uint64_t state_;
public:
    explicit FastLCG(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return state_;
    }
    std::uint32_t u32() { return static_cast<std::uint32_t>(next()); }
    std::uint32_t range(std::uint32_t max) { return max == 0 ? 0 : u32() % max; }
    bool coin() { return (u32() & 1) != 0; }
};

// External oracle to verify ECS integrity ---------------------------------------

struct EntityState {
    bool alive = false;
    int int_val = 0;
    float float_val = 0.0f;
    std::string string_val;
    bool has_int = false;
    bool has_float = false;
    bool has_string = false;
};

static void verify_entity(const World& world, Entity e, const EntityState& expected) {
    REQUIRE(world.is_alive(e) == expected.alive);
    if (!expected.alive) return;

    auto* iv = world.get<IntValue>(e);
    REQUIRE((iv != nullptr) == expected.has_int);
    if (iv && expected.has_int) REQUIRE(iv->value == expected.int_val);

    auto* fv = world.get<FloatValue>(e);
    REQUIRE((fv != nullptr) == expected.has_float);
    if (fv && expected.has_float) REQUIRE(fv->value == expected.float_val);

    auto* sv = world.get<StringValue>(e);
    REQUIRE((sv != nullptr) == expected.has_string);
    if (sv && expected.has_string) REQUIRE(sv->value == expected.string_val);
}

// Phase 1: Archetype Stress Tests ----------------------------------------------

TEST_CASE("Stress: deterministic fuzz spawn/insert/remove/despawn") {
    World world;
    std::vector<EntityState> oracle;
    std::vector<Entity> entities;
    FastLCG rng(42);

    // --- Spawn phase ---
    for (int i = 0; i < 500; ++i) {
        Entity e = world.spawn();
        EntityState st;
        st.alive = true;

        if (rng.coin()) {
            world.insert<IntValue>(e, i);
            st.has_int = true;
            st.int_val = i;
        }
        if (rng.coin()) {
            world.insert<FloatValue>(e, static_cast<float>(i) * 0.5f);
            st.has_float = true;
            st.float_val = static_cast<float>(i) * 0.5f;
        }
        if (rng.coin()) {
            std::string s = "entity_" + std::to_string(i);
            world.insert<StringValue>(e, s);
            st.has_string = true;
            st.string_val = s;
        }

        std::uint32_t idx = entity_index(e);
        if (idx >= oracle.size()) oracle.resize(idx + 1);
        oracle[idx] = st;
        entities.push_back(e);
    }

    // Verify after spawn
    for (Entity e : entities) verify_entity(world, e, oracle[entity_index(e)]);

    // --- Mutation phase ---
    for (int round = 0; round < 200; ++round) {
        std::uint32_t idx = rng.range(static_cast<std::uint32_t>(entities.size()));
        Entity e = entities[idx];
        if (!world.is_alive(e)) continue;

        EntityState& st = oracle[entity_index(e)];
        int op = rng.range(6);
        switch (op) {
            case 0: // insert IntValue
                if (!st.has_int) {
                    world.insert<IntValue>(e, round * 1000 + idx);
                    st.has_int = true;
                    st.int_val = round * 1000 + static_cast<int>(idx);
                }
                break;
            case 1: // remove IntValue
                if (st.has_int) {
                    world.remove<IntValue>(e);
                    st.has_int = false;
                }
                break;
            case 2: // insert FloatValue
                if (!st.has_float) {
                    world.insert<FloatValue>(e, static_cast<float>(round));
                    st.has_float = true;
                    st.float_val = static_cast<float>(round);
                }
                break;
            case 3: // remove FloatValue
                if (st.has_float) {
                    world.remove<FloatValue>(e);
                    st.has_float = false;
                }
                break;
            case 4: // insert StringValue
                if (!st.has_string) {
                    std::string s = "round_" + std::to_string(round) + "_idx_" + std::to_string(idx);
                    world.insert<StringValue>(e, s);
                    st.has_string = true;
                    st.string_val = s;
                }
                break;
            case 5: // remove StringValue
                if (st.has_string) {
                    world.remove<StringValue>(e);
                    st.has_string = false;
                }
                break;
        }
    }

    // Verify after mutations
    for (Entity e : entities) {
        if (world.is_alive(e)) verify_entity(world, e, oracle[entity_index(e)]);
    }

    // --- Despawn phase (remove every 3rd) ---
    for (std::size_t i = 0; i < entities.size(); i += 3) {
        Entity e = entities[i];
        if (world.is_alive(e)) {
            world.despawn(e);
            oracle[entity_index(e)].alive = false;
        }
    }

    // Final verify
    for (Entity e : entities) {
        verify_entity(world, e, oracle[entity_index(e)]);
    }
}

TEST_CASE("Stress: repeated insert/remove on same entity") {
    World world;
    Entity e = world.spawn();

    // Move the entity back and forth between archetypes many times
    for (int i = 0; i < 100; ++i) {
        world.insert<IntValue>(e, i);
        REQUIRE(world.get<IntValue>(e)->value == i);

        world.insert<FloatValue>(e, static_cast<float>(i));
        REQUIRE(world.get<IntValue>(e)->value == i);
        REQUIRE(world.get<FloatValue>(e)->value == static_cast<float>(i));

        world.remove<IntValue>(e);
        REQUIRE(world.get<IntValue>(e) == nullptr);
        REQUIRE(world.get<FloatValue>(e)->value == static_cast<float>(i));

        world.remove<FloatValue>(e);
        REQUIRE(world.get<FloatValue>(e) == nullptr);
    }
}

TEST_CASE("Stress: swap-back at every row index") {
    World world;
    std::vector<Entity> entities;

    // Spawn 10 entities with a unique IntValue each
    for (int i = 0; i < 10; ++i) {
        Entity e = world.spawn_with(IntValue{i});
        entities.push_back(e);
    }

    // Remove from row 0 (not last) — triggers swap with row 9
    world.despawn(entities[0]);
    REQUIRE(world.get<IntValue>(entities[9])->value == 9); // swapped entity intact

    // Remove from row 4 (middle) — triggers swap
    world.despawn(entities[4]);
    for (int i = 1; i < 10; ++i) {
        if (i == 0 || i == 4) continue;
        auto* iv = world.get<IntValue>(entities[i]);
        REQUIRE(iv != nullptr);
        REQUIRE(iv->value == i);
    }

    // Remove from last row (no swap)
    world.despawn(entities[8]);
    for (int i = 1; i < 10; ++i) {
        if (i == 0 || i == 4 || i == 8) continue;
        auto* iv = world.get<IntValue>(entities[i]);
        REQUIRE(iv != nullptr);
        REQUIRE(iv->value == i);
    }
}

TEST_CASE("Stress: chunk boundary with single-component archetype") {
    // Archetype with only IntValue: ~16KB / 8 bytes ≈ 2048 entities per chunk
    World world;
    std::vector<Entity> entities;

    // Spawn enough to exceed one chunk
    for (int i = 0; i < 2500; ++i) {
        Entity e = world.spawn_with(IntValue{i});
        entities.push_back(e);
    }

    // Verify all
    for (int i = 0; i < 2500; ++i) {
        auto* iv = world.get<IntValue>(entities[i]);
        REQUIRE(iv != nullptr);
        REQUIRE(iv->value == i);
    }

    // Remove from the boundary (entities around index 2048)
    for (int i = 2040; i < 2060; ++i) {
        world.despawn(entities[i]);
    }

    // Verify survivors
    for (int i = 0; i < 2500; ++i) {
        if (i >= 2040 && i < 2060) {
            REQUIRE(!world.is_alive(entities[i]));
        } else {
            auto* iv = world.get<IntValue>(entities[i]);
            REQUIRE(iv != nullptr);
            REQUIRE(iv->value == i);
        }
    }
}

TEST_CASE("Stress: non-trivial StringValue survives archetype moves") {
    World world;
    std::vector<Entity> entities;

    for (int i = 0; i < 50; ++i) {
        Entity e = world.spawn_with(StringValue{"initial_" + std::to_string(i)});
        entities.push_back(e);
    }

    // Add IntValue to every other entity (moves to new archetype)
    for (int i = 0; i < 50; i += 2) {
        world.insert<IntValue>(entities[i], i * 10);
    }

    // Verify strings intact
    for (int i = 0; i < 50; ++i) {
        auto* sv = world.get<StringValue>(entities[i]);
        REQUIRE(sv != nullptr);
        REQUIRE(sv->value == "initial_" + std::to_string(i));
    }

    // Remove IntValue from some (moves back)
    for (int i = 0; i < 50; i += 4) {
        world.remove<IntValue>(entities[i]);
    }

    // Verify strings still intact
    for (int i = 0; i < 50; ++i) {
        auto* sv = world.get<StringValue>(entities[i]);
        REQUIRE(sv != nullptr);
        REQUIRE(sv->value == "initial_" + std::to_string(i));
    }
}

TEST_CASE("Stress: large component near chunk capacity") {
    // BigComponent is 256 bytes. Chunk capacity ≈ 16KB / (8 + 256) ≈ 60
    World world;
    std::vector<Entity> entities;

    for (int i = 0; i < 200; ++i) {
        BigComponent bc{};
        bc.id = static_cast<std::uint64_t>(i);
        Entity e = world.spawn_with(bc);
        entities.push_back(e);
    }

    // Verify all ids
    for (int i = 0; i < 200; ++i) {
        auto* bc = world.get<BigComponent>(entities[i]);
        REQUIRE(bc != nullptr);
        REQUIRE(bc->id == static_cast<std::uint64_t>(i));
    }

    // Despawn half
    for (int i = 0; i < 200; i += 2) {
        world.despawn(entities[i]);
    }

    // Verify survivors
    for (int i = 1; i < 200; i += 2) {
        auto* bc = world.get<BigComponent>(entities[i]);
        REQUIRE(bc != nullptr);
        REQUIRE(bc->id == static_cast<std::uint64_t>(i));
    }
}

TEST_CASE("Stress: many archetypes with overlapping components") {
    World world;
    std::vector<std::pair<Entity, int>> entities; // entity + expected id

    for (int i = 0; i < 100; ++i) {
        Entity e = world.spawn();
        int mask = i % 8; // 8 possible archetype combinations
        if (mask & 1) world.insert<IntValue>(e, i);
        if (mask & 2) world.insert<FloatValue>(e, static_cast<float>(i));
        if (mask & 4) world.insert<StringValue>(e, std::to_string(i));
        entities.push_back({e, i});
    }

    // Verify all
    for (auto& [e, expected_id] : entities) {
        int mask = expected_id % 8;
        if (mask & 1) {
            auto* iv = world.get<IntValue>(e);
            REQUIRE(iv != nullptr);
            REQUIRE(iv->value == expected_id);
        }
        if (mask & 2) {
            auto* fv = world.get<FloatValue>(e);
            REQUIRE(fv != nullptr);
            REQUIRE(fv->value == static_cast<float>(expected_id));
        }
        if (mask & 4) {
            auto* sv = world.get<StringValue>(e);
            REQUIRE(sv != nullptr);
            REQUIRE(sv->value == std::to_string(expected_id));
        }
    }
}

// Phase 1: Query Safety --------------------------------------------------------

TEST_CASE("Stress: query does not crash on empty archetype") {
    World world;
    int count = 0;
    world.query<IntValue, FloatValue>().each([&](IntValue&, FloatValue&) {
        ++count;
    });
    REQUIRE(count == 0);
}

TEST_CASE("Stress: query over many archetypes") {
    World world;
    int expected = 0;
    std::vector<Entity> with_pos;

    for (int i = 0; i < 200; ++i) {
        Entity e = world.spawn_with(Position{static_cast<float>(i), 0.0f, 0.0f});
        with_pos.push_back(e);
        if (i % 3 == 0) {
            world.insert<Velocity>(e, 1.0f, 2.0f, 3.0f);
            ++expected;
        }
    }

    int count = 0;
    world.query<Position, Velocity>().each([&](Position&, Velocity&) {
        ++count;
    });
    REQUIRE(count == expected);
}

// Phase 1: Resource Safety -----------------------------------------------------

TEST_CASE("Stress: resource init and get after many operations") {
    World world;
    world.init_resource<IntValue>(42);

    // Perform many entity operations while resource exists
    for (int i = 0; i < 100; ++i) {
        Entity e = world.spawn_with(IntValue{i});
        if (i % 2 == 0) world.despawn(e);
    }

    auto& res = world.resource<IntValue>();
    REQUIRE(res.value == 42);
}

// Phase 1: Remaining gap tests -------------------------------------------------

struct alignas(64) AlignedData {
    std::uint64_t value = 0;
};

namespace campello::core {
template<> struct ComponentTraits<AlignedData> : ComponentTraitsBase<AlignedData> {
    static constexpr std::string_view name = "AlignedData";
};
} // namespace campello::core

TEST_CASE("Stress: custom alignment is respected in SoA layout") {
    World world;
    std::vector<Entity> entities;

    for (int i = 0; i < 100; ++i) {
        AlignedData ad{};
        ad.value = static_cast<std::uint64_t>(i);
        entities.push_back(world.spawn_with(ad));
    }

    for (int i = 0; i < 100; ++i) {
        auto* ad = world.get<AlignedData>(entities[i]);
        REQUIRE(ad != nullptr);
        REQUIRE(ad->value == static_cast<std::uint64_t>(i));

        // Verify 64-byte alignment
        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ad);
        REQUIRE((addr % 64) == 0);
    }

    // Move to new archetype and verify alignment is preserved
    for (int i = 0; i < 100; i += 2) {
        world.insert<IntValue>(entities[i], i);
    }

    for (int i = 0; i < 100; ++i) {
        auto* ad = world.get<AlignedData>(entities[i]);
        REQUIRE(ad != nullptr);
        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ad);
        REQUIRE((addr % 64) == 0);
    }
}

struct SelfRef {
    int value = 42;
    int* ptr = nullptr;
};

namespace campello::core {
template<> struct ComponentTraits<SelfRef> : ComponentTraitsBase<SelfRef> {
    static constexpr std::string_view name = "SelfRef";
};
} // namespace campello::core

TEST_CASE("Stress: self-referential type becomes dangling after archetype move") {
    World world;
    Entity e = world.spawn_with(SelfRef{});

    // Fix up self-reference to point to the actual chunk location
    auto* before = world.get<SelfRef>(e);
    REQUIRE(before != nullptr);
    before->ptr = &before->value;
    REQUIRE(before->ptr == &before->value);

    // Move to new archetype — the object is relocated
    world.insert<IntValue>(e, 1);

    auto* after = world.get<SelfRef>(e);
    REQUIRE(after != nullptr);
    // The pointer is now dangling (points to old memory location)
    REQUIRE(after->ptr != &after->value);
    // But the value itself is intact because SelfRef is trivially copyable
    REQUIRE(after->value == 42);
}

struct ThrowingMove {
    int value = 0;
    static bool should_throw;

    ThrowingMove() = default;
    explicit ThrowingMove(int v) : value(v) {}

    ThrowingMove(ThrowingMove&& other) noexcept(false) {
        if (should_throw) {
            throw std::runtime_error("intentional move failure");
        }
        value = other.value;
    }

    ThrowingMove(const ThrowingMove& other) : value(other.value) {}
    ThrowingMove& operator=(const ThrowingMove&) = default;
};

bool ThrowingMove::should_throw = false;

namespace campello::core {
template<> struct ComponentTraits<ThrowingMove> : ComponentTraitsBase<ThrowingMove> {
    static constexpr std::string_view name = "ThrowingMove";
};
} // namespace campello::core

TEST_CASE("Stress: throwing move constructor is handled without crash") {
    World world;
    Entity e = world.spawn_with(ThrowingMove{123});

    REQUIRE(world.get<ThrowingMove>(e)->value == 123);

    // First, verify normal move works
    ThrowingMove::should_throw = false;
    world.insert<IntValue>(e, 1);
    REQUIRE(world.get<ThrowingMove>(e)->value == 123);

    // Now trigger a throwing move by adding another component
    ThrowingMove::should_throw = true;
    bool caught = false;
    try {
        world.insert<FloatValue>(e, 1.0f);
    } catch (const std::runtime_error&) {
        caught = true;
    }

    REQUIRE(caught);
    ThrowingMove::should_throw = false;

    // After the exception, the world should still be usable.
    // The entity may be in a partially-moved state; we just verify no crash.
    REQUIRE(world.is_alive(e));

    // Subsequent operations should not crash
    world.despawn(e);
    REQUIRE(!world.is_alive(e));
}
