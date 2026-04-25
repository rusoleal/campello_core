#include <campello/core/core.hpp>
#include <iostream>

using namespace campello::core;

struct Position {
    float x = 0, y = 0, z = 0;
};

struct Velocity {
    float dx = 0, dy = 0, dz = 0;
};

namespace campello::core {

template<>
struct ComponentTraits<Position> : ComponentTraitsBase<Position> {
    static constexpr std::string_view name = "Position";
};

template<>
struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity> {
    static constexpr std::string_view name = "Velocity";
};

} // namespace campello::core

int main() {
    World world;

    // Spawn entities with components
    for (int i = 0; i < 5; ++i) {
        world.spawn_with(
            Position{float(i), 0.0f, 0.0f},
            Velocity{0.1f, 0.0f, 0.0f}
        );
    }

    // Query and mutate
    auto q = world.query<Position, Velocity>();
    q.each([](Position& pos, Velocity& vel) {
        pos.x += vel.dx;
    });

    // Print results
    for (auto [pos, vel] : world.query<Position, Velocity>()) {
        std::cout << "pos.x = " << pos.x << "\n";
    }

    return 0;
}
