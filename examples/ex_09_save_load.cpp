#include <campello/core/core.hpp>
#include <campello/core/snapshot.hpp>
#include <iostream>
#include <fstream>

using namespace campello::core;

struct Player { std::string name; int score = 0; };
struct Position { float x = 0, y = 0, z = 0; };

namespace campello::core {
template<> struct ComponentTraits<Player> : ComponentTraitsBase<Player> {
    static constexpr std::string_view name = "Player";
    static void reflect(ComponentBuilder& b) {
        b.property("name", &Player::name);
        b.property("score", &Player::score);
    }
};
template<> struct ComponentTraits<Position> : ComponentTraitsBase<Position> {
    static constexpr std::string_view name = "Position";
    static void reflect(ComponentBuilder& b) {
        b.property("x", &Position::x);
        b.property("y", &Position::y);
        b.property("z", &Position::z);
    }
};
} // namespace campello::core

int main() {
    // --- Build a world ---
    World world;
    world.register_component<Player>();
    world.register_component<Position>();

    world.spawn_with(Player{"alice", 42}, Position{1.0f, 2.0f, 3.0f});
    world.spawn_with(Player{"bob", 99},   Position{4.0f, 5.0f, 6.0f});
    world.spawn_with(Player{"carol", 7},  Position{7.0f, 8.0f, 9.0f});

    std::cout << "Original world:\n";
    world.query<Player, Position>().each([](const Player& p, const Position& pos) {
        std::cout << "  " << p.name << " score=" << p.score
                  << " pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
    });

    // --- Snapshot to JSON ---
    std::string json = world.snapshot();
    std::cout << "\nSnapshot JSON:\n" << json << "\n\n";

    // --- Save to file ---
    {
        std::ofstream out("savegame.json");
        out << json;
    }
    std::cout << "Wrote savegame.json\n";

    // --- Restore into a fresh world ---
    World restored;
    restored.register_component<Player>();
    restored.register_component<Position>();
    restored.restore(json);

    std::cout << "\nRestored world:\n";
    restored.query<Player, Position>().each([](const Player& p, const Position& pos) {
        std::cout << "  " << p.name << " score=" << p.score
                  << " pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
    });

    // Verify counts match
    std::cout << "\nOriginal entities: " << world.query<Player>().count() << "\n";
    std::cout << "Restored entities: " << restored.query<Player>().count() << "\n";

    return 0;
}
