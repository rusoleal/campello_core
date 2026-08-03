// Real-world profiling workload for campello_core.
//
// Designed to be captured with Xcode Instruments / xctrace or similar.
// Run a fixed-duration simulation of mixed ECS operations.

#include <campello/core/core.hpp>
#include <campello/core/schedule.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using namespace campello::core;

struct Position {
    float x = 0, y = 0, z = 0;
};

struct Velocity {
    float dx = 0, dy = 0, dz = 0;
};

struct Health {
    int value = 100;
};

namespace campello::core {

template<> struct ComponentTraits<Position> : ComponentTraitsBase<Position> {
    static constexpr std::string_view name = "Position";
};

template<> struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity> {
    static constexpr std::string_view name = "Velocity";
};

template<> struct ComponentTraits<Health> : ComponentTraitsBase<Health> {
    static constexpr std::string_view name = "Health";
};

} // namespace campello::core

int main(int argc, char** argv) {
    std::size_t entity_count = 50000;
    double duration_seconds = 5.0;

    if (argc > 1) entity_count = std::stoull(argv[1]);
    if (argc > 2) duration_seconds = std::stod(argv[2]);

    World world;

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> pos_dist(-1000.0f, 1000.0f);
    std::uniform_real_distribution<float> vel_dist(-1.0f, 1.0f);
    std::uniform_int_distribution<int> hp_dist(50, 150);

    std::vector<Entity> entities;
    entities.reserve(entity_count);

    for (std::size_t i = 0; i < entity_count; ++i) {
        Entity e = world.spawn_with(
            Position{pos_dist(rng), pos_dist(rng), pos_dist(rng)},
            Velocity{vel_dist(rng), vel_dist(rng), vel_dist(rng)},
            Health{hp_dist(rng)}
        );
        entities.push_back(e);
    }

    std::size_t frames = 0;
    std::size_t processed = 0;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        if (elapsed >= duration_seconds) break;

        // System 1: move everything with Position + Velocity.
        world.query<Position, Velocity>().each([&](Position& pos, Velocity& vel) {
            pos.x += vel.dx;
            pos.y += vel.dy;
            pos.z += vel.dz;
            ++processed;
        });

        // System 2: damage everything with Health.
        world.query<Health>().each([&](Health& h) {
            h.value -= 1;
        });

        // System 3: collect and despawn dead entities.
        std::vector<Entity> to_despawn;
        world.query<Health>().each_with_entity([&](Entity e, Health& h) {
            if (h.value <= 0) to_despawn.push_back(e);
        });
        world.despawn_many(to_despawn);

        // System 4: respawn to maintain population.
        std::size_t to_spawn = to_despawn.size();
        for (std::size_t i = 0; i < to_spawn; ++i) {
            world.spawn_with(
                Position{pos_dist(rng), pos_dist(rng), pos_dist(rng)},
                Velocity{vel_dist(rng), vel_dist(rng), vel_dist(rng)},
                Health{hp_dist(rng)}
            );
        }

        ++frames;
    }

    auto end = std::chrono::steady_clock::now();
    double total = std::chrono::duration<double>(end - start).count();

    std::cout << "entities: " << entity_count << "\n";
    std::cout << "frames: " << frames << "\n";
    std::cout << "processed: " << processed << "\n";
    std::cout << "total time: " << total << " s\n";
    std::cout << "ms/frame: " << (total / frames * 1000.0) << "\n";
    std::cout << "entities/s (move system): " << (processed / total) << "\n";

    return 0;
}
