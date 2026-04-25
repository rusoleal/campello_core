#include <campello/core/core.hpp>
#include <iostream>

using namespace campello::core;

struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };
struct Static {};

namespace campello::core {
template<> struct ComponentTraits<Position> : ComponentTraitsBase<Position> {
    static constexpr std::string_view name = "Position";
};
template<> struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity> {
    static constexpr std::string_view name = "Velocity";
};
template<> struct ComponentTraits<Static> : ComponentTraitsBase<Static> {
    static constexpr std::string_view name = "Static";
};
} // namespace campello::core

int main() {
    World world;

    // Moving entities
    world.spawn_with(Position{0, 0, 0}, Velocity{1, 0, 0});
    world.spawn_with(Position{1, 0, 0}, Velocity{2, 0, 0});

    // Static entities
    world.spawn_with(Position{5, 5, 5}, Static{});
    world.spawn_with(Position{6, 6, 6});

    std::cout << "All positions: " << world.query<Position>().count() << "\n";
    std::cout << "Moving: " << world.query<Position, Velocity>().count() << "\n";

    return 0;
}
