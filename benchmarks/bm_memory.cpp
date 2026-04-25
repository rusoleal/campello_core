#include <campello/core/core.hpp>
#include <iostream>
#include <iomanip>
#include <vector>

using namespace campello::core;

struct Pos { float x, y, z; };
struct Vel { float dx, dy, dz; };
struct Mass { float value; };
struct Health { int hp; int max_hp; };
struct TagA { int v; };
struct TagB { int v; };
struct TagC { int v; };
struct TagD { int v; };

namespace campello::core {
template<> struct ComponentTraits<Pos>    : ComponentTraitsBase<Pos>    { static constexpr std::string_view name = "Pos"; };
template<> struct ComponentTraits<Vel>    : ComponentTraitsBase<Vel>    { static constexpr std::string_view name = "Vel"; };
template<> struct ComponentTraits<Mass>   : ComponentTraitsBase<Mass>   { static constexpr std::string_view name = "Mass"; };
template<> struct ComponentTraits<Health> : ComponentTraitsBase<Health> { static constexpr std::string_view name = "Health"; };
template<> struct ComponentTraits<TagA>   : ComponentTraitsBase<TagA>   { static constexpr std::string_view name = "TagA"; };
template<> struct ComponentTraits<TagB>   : ComponentTraitsBase<TagB>   { static constexpr std::string_view name = "TagB"; };
template<> struct ComponentTraits<TagC>   : ComponentTraitsBase<TagC>   { static constexpr std::string_view name = "TagC"; };
template<> struct ComponentTraits<TagD>   : ComponentTraitsBase<TagD>   { static constexpr std::string_view name = "TagD"; };
} // namespace campello::core

static void print_header() {
    std::cout << std::left << std::setw(28) << "scenario"
              << std::right << std::setw(10) << "entities"
              << std::setw(12) << "chunks"
              << std::setw(14) << "chunk_data"
              << std::setw(14) << "chunk_ticks"
              << std::setw(14) << "entity_meta"
              << std::setw(14) << "total"
              << std::setw(12) << "b/entity"
              << std::setw(12) << "util%"
              << "\n";
    std::cout << std::string(118, '-') << "\n";
}

static void print_row(const char* name, const World::MemoryStats& s) {
    std::cout << std::left << std::setw(28) << name
              << std::right << std::setw(10) << s.alive_entities
              << std::setw(12) << s.chunk_count
              << std::setw(14) << s.chunk_data_bytes
              << std::setw(14) << s.chunk_ticks_bytes
              << std::setw(14) << s.entity_meta_bytes
              << std::setw(14) << s.total_bytes()
              << std::setw(12) << std::fixed << std::setprecision(1) << s.bytes_per_entity()
              << std::setw(11) << std::fixed << std::setprecision(1) << (s.chunk_utilization() * 100.0)
              << "%\n";
}

int main() {
    print_header();

    // 1. Single small component
    for (int n : {100, 1000, 10000}) {
        World world;
        for (int i = 0; i < n; ++i) {
            world.spawn_with(TagA{i});
        }
        print_row(("1 comp x " + std::to_string(n)).c_str(), world.memory_stats());
    }

    // 2. Position + Velocity (typical game archetype)
    for (int n : {100, 1000, 10000}) {
        World world;
        for (int i = 0; i < n; ++i) {
            world.spawn_with(Pos{float(i), 0, 0}, Vel{1, 2, 3});
        }
        print_row(("Pos+Vel x " + std::to_string(n)).c_str(), world.memory_stats());
    }

    // 3. Eight components (heavy archetype)
    for (int n : {100, 1000, 10000}) {
        World world;
        for (int i = 0; i < n; ++i) {
            world.spawn_with(TagA{i}, TagB{i}, TagC{i}, TagD{i},
                             Pos{float(i), 0, 0}, Vel{1, 2, 3},
                             Mass{1.0f}, Health{100, 100});
        }
        print_row(("8 comp x " + std::to_string(n)).c_str(), world.memory_stats());
    }

    // 4. Many archetypes (fragmentation / edge-cache overhead)
    {
        World world;
        std::vector<Entity> entities;
        entities.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            Entity e = world.spawn_with(TagA{i});
            if (i & 1) world.insert<TagB>(e, i);
            if (i & 2) world.insert<TagC>(e, i);
            if (i & 4) world.insert<TagD>(e, i);
            entities.push_back(e);
        }
        print_row("1000 entities, 8 archs", world.memory_stats());
    }

    // 5. Sparse population (despawn 90% to show utilization drop)
    {
        World world;
        std::vector<Entity> entities;
        entities.reserve(10000);
        for (int i = 0; i < 10000; ++i) {
            entities.push_back(world.spawn_with(Pos{float(i), 0, 0}, Vel{1, 2, 3}));
        }
        for (int i = 0; i < 10000; i += 10) {
            world.despawn(entities[i]);
        }
        print_row("10k Pos+Vel, 90% alive", world.memory_stats());
    }

    // 6. Nearly empty chunks (despawn 95%)
    {
        World world;
        std::vector<Entity> entities;
        entities.reserve(10000);
        for (int i = 0; i < 10000; ++i) {
            entities.push_back(world.spawn_with(Pos{float(i), 0, 0}, Vel{1, 2, 3}));
        }
        for (int i = 0; i < 10000; ++i) {
            if (i % 20 != 0) world.despawn(entities[i]);
        }
        print_row("10k Pos+Vel, 5% alive", world.memory_stats());
    }

    // 7. Raw baseline: hand-rolled SoA for Pos+Vel 10k
    {
        std::vector<Pos> pos(10000);
        std::vector<Vel> vel(10000);
        std::size_t raw = pos.capacity() * sizeof(Pos) + vel.capacity() * sizeof(Vel);
        std::cout << std::left << std::setw(28) << "raw SoA Pos+Vel x 10000"
                  << std::right << std::setw(10) << 10000
                  << std::setw(12) << "-"
                  << std::setw(14) << raw
                  << std::setw(14) << "-"
                  << std::setw(14) << "-"
                  << std::setw(14) << raw
                  << std::setw(12) << std::fixed << std::setprecision(1) << (double(raw) / 10000.0)
                  << std::setw(12) << "100.0%\n";
    }

    return 0;
}
