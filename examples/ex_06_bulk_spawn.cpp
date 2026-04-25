#include <campello/core/core.hpp>
#include <iostream>
#include <vector>

using namespace campello::core;

struct Position { float x = 0, y = 0, z = 0; };
struct Velocity { float dx = 0, dy = 0, dz = 0; };
struct Mass { float value = 1.0f; };

namespace campello::core {
template<> struct ComponentTraits<Position>  : ComponentTraitsBase<Position>  { static constexpr std::string_view name = "Position"; };
template<> struct ComponentTraits<Velocity>  : ComponentTraitsBase<Velocity>  { static constexpr std::string_view name = "Velocity"; };
template<> struct ComponentTraits<Mass>      : ComponentTraitsBase<Mass>      { static constexpr std::string_view name = "Mass"; };
} // namespace campello::core

int main() {
    World world;

    // spawn_many: create 1000 entities in a single archetype allocation
    auto entities = world.spawn_many<Position, Velocity>(1000,
        Position{0.0f, 0.0f, 0.0f},
        Velocity{1.0f, 0.0f, 0.0f}
    );
    std::cout << "Spawned " << entities.size() << " movers\n";

    // insert_many: add Mass to every other entity in one batch
    std::vector<Entity> half;
    half.reserve(500);
    for (std::size_t i = 0; i < entities.size(); i += 2) {
        half.push_back(entities[i]);
    }
    world.insert_many<Mass>(half, Mass{2.0f});
    std::cout << "Added Mass to " << half.size() << " entities\n";

    // Verify: count entities with Mass
    int with_mass = 0;
    world.query<Mass>().each([&](Mass&) { ++with_mass; });
    std::cout << "Entities with Mass: " << with_mass << "\n";

    // despawn_many: remove every third entity
    std::vector<Entity> to_despawn;
    to_despawn.reserve(334);
    for (std::size_t i = 0; i < entities.size(); i += 3) {
        to_despawn.push_back(entities[i]);
    }
    world.despawn_many(to_despawn);
    std::cout << "Despawned " << to_despawn.size() << " entities\n";

    // Verify survivors
    int survivors = 0;
    world.query<Position>().each([&](Position&) { ++survivors; });
    std::cout << "Survivors: " << survivors << "\n";

    return 0;
}
