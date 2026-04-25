#include <campello/core/core.hpp>
#include <iostream>
#include <vector>

using namespace campello::core;

struct Position { float x = 0, y = 0, z = 0; };
struct Health { int hp = 100; };

namespace campello::core {
template<> struct ComponentTraits<Position> : ComponentTraitsBase<Position> { static constexpr std::string_view name = "Position"; };
template<> struct ComponentTraits<Health>   : ComponentTraitsBase<Health>   { static constexpr std::string_view name = "Health"; };
} // namespace campello::core

int main() {
    World world;
    std::vector<Entity> entities;

    // Spawn initial entities at tick 1
    entities.push_back(world.spawn_with(Position{0, 0, 0}, Health{100}));
    entities.push_back(world.spawn_with(Position{10, 0, 0}, Health{80}));

    // --- Frame 1: detect newly added components ---
    // Entities were added at tick 1, so last_run_tick=0 shows everything
    std::cout << "Frame 1: newly added Health components:\n";
    world.query<Health>().added<Health>().with_last_run_tick(0).each([](Health& h) {
        std::cout << "  Health hp=" << h.hp << "\n";
    });

    // Advance tick — this clears the "added" state for the next window
    world.increment_change_tick(); // now tick 2

    // --- Frame 2: modify one entity's health ---
    if (world.get<Health>(entities[0])->hp == 100) {
        world.get<Health>(entities[0])->hp = 90;
        world.mark_changed<Health>(entities[0]);
    }

    std::cout << "Frame 2: changed Health components (last_run_tick=1):\n";
    world.query<Health>().changed<Health>().with_last_run_tick(1).each([](Health& h) {
        std::cout << "  Health hp=" << h.hp << "\n";
    });

    // --- Frame 3: insert Health on a new entity ---
    world.increment_change_tick(); // now tick 3
    entities.push_back(world.spawn_with(Position{20, 0, 0}, Health{50}));

    std::cout << "Frame 3: added (last_run_tick=2):\n";
    world.query<Health>().added<Health>().with_last_run_tick(2).each([](Health& h) {
        std::cout << "  Health hp=" << h.hp << " (newly added)\n";
    });

    std::cout << "Frame 3: changed (last_run_tick=2):\n";
    world.query<Health>().changed<Health>().with_last_run_tick(2).each([](Health& h) {
        std::cout << "  Health hp=" << h.hp << " (modified)\n";
    });

    // --- Removed tracking ---
    world.increment_change_tick(); // now tick 4
    for (Entity e : entities) {
        world.remove<Health>(e);
    }

    auto& removed = world.removed<Health>();
    std::cout << "Frame 4: removed Health on " << removed.size() << " entities this tick\n";

    return 0;
}
