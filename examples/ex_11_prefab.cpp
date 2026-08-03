// Prefab / entity cloning example
//
// Demonstrates how to use World::clone() and World::clone_many() to create
// independent copies of an existing entity (a "prefab").

#include <campello/core/core.hpp>
#include <iostream>
#include <vector>

using namespace campello::core;

struct Position { float x = 0, y = 0, z = 0; };
struct Velocity { float dx = 0, dy = 0, dz = 0; };
struct Health { int value = 100; };
struct Name { std::string value; };

namespace campello::core {
template<> struct ComponentTraits<Position> : ComponentTraitsBase<Position> { static constexpr std::string_view name = "Position"; };
template<> struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity> { static constexpr std::string_view name = "Velocity"; };
template<> struct ComponentTraits<Health>   : ComponentTraitsBase<Health>   { static constexpr std::string_view name = "Health"; };
template<> struct ComponentTraits<Name>     : ComponentTraitsBase<Name>     { static constexpr std::string_view name = "Name"; };
} // namespace campello::core

int main() {
    World world;

    // Track how many times Health is added (clone fires on_add hooks).
    int health_add_count = 0;
    world.on_add<Health>([&](Entity, Health&) { ++health_add_count; });

    // Create a prefab entity: a spaceship with Position, Velocity, Health, and Name.
    Entity prefab = world.spawn_with(
        Position{0.0f, 0.0f, 0.0f},
        Velocity{10.0f, 0.0f, 0.0f},
        Health{100},
        Name{std::string("fighter")}
    );

    std::cout << "Prefab entity: " << prefab << "\n";

    // Clone the prefab once.
    Entity ship1 = world.clone(prefab);
    std::cout << "Cloned ship1: " << ship1 << "\n";

    // Clone the prefab many times (bulk clone).
    auto squadron = world.clone_many(prefab, 5);
    std::cout << "Cloned squadron of " << squadron.size() << " ships\n";

    // Verify all clones have the same components and values.
    auto verify_ship = [&](Entity e, const std::string& label) {
        auto* pos = world.get<Position>(e);
        auto* vel = world.get<Velocity>(e);
        auto* hp  = world.get<Health>(e);
        auto* name = world.get<Name>(e);

        std::cout << label << ": pos=(" << pos->x << "," << pos->y << "," << pos->z << ")"
                  << " vel=(" << vel->dx << "," << vel->dy << "," << vel->dz << ")"
                  << " hp=" << hp->value
                  << " name=" << name->value
                  << "\n";
    };

    verify_ship(prefab, "prefab");
    verify_ship(ship1,  "ship1");
    verify_ship(squadron[2], "squadron[2]");

    // Demonstrate independence: mutate ship1 without affecting the prefab.
    world.get<Health>(ship1)->value = 50;
    world.get<Name>(ship1)->value = "damaged fighter";

    std::cout << "After damaging ship1:\n";
    std::cout << "  prefab hp=" << world.get<Health>(prefab)->value
              << " name=" << world.get<Name>(prefab)->value << "\n";
    std::cout << "  ship1  hp=" << world.get<Health>(ship1)->value
              << " name=" << world.get<Name>(ship1)->value << "\n";

    // on_add<Health> fired for the prefab, ship1, and each squadron member.
    std::cout << "health on_add hook fires: " << health_add_count << "\n";

    return 0;
}
