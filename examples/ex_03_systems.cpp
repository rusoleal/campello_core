#include <campello/core/core.hpp>
#include <iostream>

using namespace campello::core;

struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };

struct Time {
    float delta_seconds = 0.016f;
};

namespace campello::core {
template<> struct ComponentTraits<Position> : ComponentTraitsBase<Position> {
    static constexpr std::string_view name = "Position";
};
template<> struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity> {
    static constexpr std::string_view name = "Velocity";
};
} // namespace campello::core

void move_system(World& world) {
    float dt = world.resource<Time>().delta_seconds;
    for (auto [pos, vel] : world.query<Position, Velocity>()) {
        pos.x += vel.dx * dt;
        pos.y += vel.dy * dt;
        pos.z += vel.dz * dt;
    }
}

void print_system(World& world) {
    for (auto [pos] : world.query<Position>()) {
        std::cout << "(" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
    }
}

int main() {
    World world;
    world.init_resource<Time>(0.033f);

    world.spawn_with(Position{0, 0, 0}, Velocity{1, 2, 3});
    world.spawn_with(Position{10, 0, 0}, Velocity{-1, 0, 0});

    Schedule schedule;
    schedule.add_system(move_system)
            .in_stage(Stage::Update)
            .reads_components<Position, Velocity>()
            .writes_components<Position>()
            .reads_resources<Time>();

    schedule.add_system(print_system).in_stage(Stage::PostUpdate);

    std::cout << "Frame 1:\n";
    schedule.run(world);

    std::cout << "Frame 2:\n";
    schedule.run(world);

    return 0;
}
