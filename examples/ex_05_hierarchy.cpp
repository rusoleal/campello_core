#include <campello/core/core.hpp>
#include <iostream>

using namespace campello::core;

struct Name { std::string value; };

namespace campello::core {
template<> struct ComponentTraits<Name> : ComponentTraitsBase<Name> {
    static constexpr std::string_view name = "Name";
};
} // namespace campello::core

void print_hierarchy(const World& world, Entity entity, int depth = 0) {
    if (!world.is_alive(entity)) return;
    const auto* n = world.get<Name>(entity);
    std::string indent(depth * 2, ' ');
    std::cout << indent << (n ? n->value : "entity") << "\n";

    if (const auto* children = world.get<Children>(entity)) {
        for (Entity child : children->entities) {
            print_hierarchy(world, child, depth + 1);
        }
    }
}

int main() {
    World world;

    Entity root = world.spawn_with(Name{"root"});
    Entity child1 = world.spawn_with(Name{"child1"});
    Entity child2 = world.spawn_with(Name{"child2"});
    Entity grandchild = world.spawn_with(Name{"grandchild"});

    world.set_parent(child1, root);
    world.set_parent(child2, root);
    world.set_parent(grandchild, child1);

    std::cout << "Hierarchy:\n";
    print_hierarchy(world, root);

    // Despawning root cascades to all children
    world.despawn(root);
    std::cout << "After despawn root:\n";
    std::cout << "root alive: " << world.is_alive(root) << "\n";
    std::cout << "child1 alive: " << world.is_alive(child1) << "\n";
    std::cout << "grandchild alive: " << world.is_alive(grandchild) << "\n";

    return 0;
}
