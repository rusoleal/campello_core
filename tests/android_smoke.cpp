// Android NDK smoke test — compile-only validation that all headers
// instantiate correctly under Android Clang + libc++.

#include <campello/core/core.hpp>
#include <iostream>

using namespace campello::core;

struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };

namespace campello::core {
template<> struct ComponentTraits<Position> : ComponentTraitsBase<Position> {
    static constexpr std::string_view name = "Position";
};
template<> struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity> {
    static constexpr std::string_view name = "Velocity";
};
} // namespace campello::core

int main() {
    World world;

    // Spawn / query / iterate
    auto e1 = world.spawn_with(Position{1,2,3}, Velocity{4,5,6});
    world.query<Position, Velocity>().each([](Position&, Velocity&) {});

    // Hierarchy
    auto parent = world.spawn();
    auto child  = world.spawn();
    world.set_parent(child, parent);

    // Resources
    world.init_resource<int>(42);
    int& r = world.resource<int>();
    (void)r;

    // Events
    struct MyEvent { int value; };
    world.send<MyEvent>(MyEvent{7});
    for (const auto& ev : world.event_reader<MyEvent>()) { (void)ev; }

    // Commands
    world.commands().spawn_with(Position{0,0,0});
    world.apply_commands();

    // Clone
    auto cloned = world.clone(e1);
    (void)cloned;

    // Snapshot round-trip
    auto snap = world.snapshot();
    World world2;
    world2.restore(snap);

    // Version
    static_assert(CAMPELLO_CORE_VERSION_MAJOR == 0);

    std::cout << "Android smoke test OK\n";
    return 0;
}
