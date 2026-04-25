#include <campello/core/core.hpp>
#include <campello/core/detail/thread_pool.hpp>
#include <iostream>
#include <cmath>

using namespace campello::core;

struct Position { float x = 0, y = 0, z = 0; };
struct Velocity { float dx = 0, dy = 0, dz = 0; };

namespace campello::core {
template<> struct ComponentTraits<Position> : ComponentTraitsBase<Position> { static constexpr std::string_view name = "Position"; };
template<> struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity> { static constexpr std::string_view name = "Velocity"; };
} // namespace campello::core

int main() {
    World world;

    // Spawn 50k movers
    for (int i = 0; i < 50000; ++i) {
        world.spawn_with(
            Position{float(i), float(i), float(i)},
            Velocity{0.01f, 0.02f, 0.03f}
        );
    }

    // Thread pool with hardware-concurrency threads
    detail::ThreadPool pool;
    std::cout << "Thread pool: " << pool.size() << " threads\n";

    // Parallel chunk iteration — each chunk processed by a different thread
    world.query<Position, Velocity>().each_par(pool, [](Position& pos, Velocity& vel) {
        pos.x += vel.dx;
        pos.y += vel.dy;
        pos.z += vel.dz;
    });

    // Verify a few results
    int checked = 0;
    world.query<Position, Velocity>().each([&](Position& pos, Velocity&) {
        if (checked < 3) {
            std::cout << "Entity " << checked << ": pos=("
                      << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
        }
        ++checked;
    });
    std::cout << "Total entities updated: " << checked << "\n";

    return 0;
}
