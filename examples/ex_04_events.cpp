#include <campello/core/core.hpp>
#include <iostream>

using namespace campello::core;

struct Collision {
    Entity a;
    Entity b;
    float impact_force;
};

struct Health { int value; };

namespace campello::core {
template<> struct ComponentTraits<Health> : ComponentTraitsBase<Health> {
    static constexpr std::string_view name = "Health";
};
} // namespace campello::core

void damage_system(World& world) {
    auto reader = world.event_reader<Collision>();
    for (const auto& col : reader) {
        std::cout << "Collision! force=" << col.impact_force << "\n";
        if (auto* hp = world.get<Health>(col.a)) {
            hp->value -= static_cast<int>(col.impact_force);
        }
    }
}

void cleanup_system(World& world) {
    auto& cmds = world.commands();
    for (auto [e, hp] : world.query<Entity, Health>()) {
        if (hp.value <= 0) {
            cmds.despawn(e);
        }
    }
    world.apply_commands();
}

int main() {
    World world;

    Entity e1 = world.spawn_with(Health{100});
    Entity e2 = world.spawn_with(Health{50});

    world.send(Collision{e1, e2, 30.0f});
    world.send(Collision{e2, e1, 60.0f});

    Schedule schedule(0); // sequential for deterministic output
    schedule.add_system(damage_system).in_stage(Stage::Update);
    schedule.add_system(cleanup_system).in_stage(Stage::PostUpdate);

    schedule.run(world);

    std::cout << "e1 alive: " << world.is_alive(e1) << "\n";
    std::cout << "e2 alive: " << world.is_alive(e2) << "\n";

    return 0;
}
