#include <catch2/catch_test_macros.hpp>
#include <campello/core/world.hpp>
#include <campello/core/serialize.hpp>

using namespace campello::core;

struct PlayerData {
    int health = 100;
    float speed = 5.5f;
    bool alive = true;
};

namespace campello::core {

template<>
struct ComponentTraits<PlayerData> : ComponentTraitsBase<PlayerData> {
    static constexpr std::string_view name = "PlayerData";
    static void reflect(ComponentBuilder& b) {
        b.property("health", &PlayerData::health)
         .property("speed", &PlayerData::speed)
         .property("alive", &PlayerData::alive);
    }
};

} // namespace campello::core

TEST_CASE("Serialize component to JSON") {
    TypeSerializerRegistry registry;
    register_primitive_serializers(registry);

    World world;
    world.register_component<PlayerData>();
    const ComponentInfo* info = world.reflect_registry().info(component_type_id<PlayerData>());
    REQUIRE(info != nullptr);

    PlayerData data{75, 3.5f, false};
    std::string json = serialize_component(*info, &data, registry);
    REQUIRE(json.find("\"health\":75") != std::string::npos);
    REQUIRE(json.find("\"speed\":3.5") != std::string::npos);
    REQUIRE(json.find("\"alive\":false") != std::string::npos);
}

TEST_CASE("Deserialize component from JSON") {
    TypeSerializerRegistry registry;
    register_primitive_serializers(registry);

    World world;
    world.register_component<PlayerData>();
    const ComponentInfo* info = world.reflect_registry().info(component_type_id<PlayerData>());
    REQUIRE(info != nullptr);

    PlayerData data{};
    std::string json = R"({"health":42,"speed":1.5,"alive":true})";
    bool ok = deserialize_component(*info, &data, json, registry);
    REQUIRE(ok);
    REQUIRE(data.health == 42);
    REQUIRE(data.speed == 1.5f);
    REQUIRE(data.alive == true);
}
