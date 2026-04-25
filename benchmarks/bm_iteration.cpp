#include <campello/core/core.hpp>
#include <nanobench.h>
#include <iostream>
#include <vector>

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

// Raw AoS baseline
struct AoSEntity {
    Position pos;
    Velocity vel;
};

// Raw SoA baseline
struct SoAEntities {
    std::vector<Position> pos;
    std::vector<Velocity> vel;
};

static void bench_aos(ankerl::nanobench::Bench& b, int n) {
    std::vector<AoSEntity> entities;
    entities.reserve(n);
    for (int i = 0; i < n; ++i) {
        entities.push_back({Position{float(i), 0, 0}, Velocity{1, 2, 3}});
    }

    b.run("aos " + std::to_string(n), [&]() {
        for (auto& e : entities) {
            e.pos.x += e.vel.dx;
            e.pos.y += e.vel.dy;
            e.pos.z += e.vel.dz;
        }
        ankerl::nanobench::doNotOptimizeAway(entities.data());
    });
}

static void bench_soa(ankerl::nanobench::Bench& b, int n) {
    SoAEntities entities;
    entities.pos.reserve(n);
    entities.vel.reserve(n);
    for (int i = 0; i < n; ++i) {
        entities.pos.push_back({float(i), 0, 0});
        entities.vel.push_back({1, 2, 3});
    }

    b.run("soa " + std::to_string(n), [&]() {
        for (int i = 0; i < n; ++i) {
            entities.pos[i].x += entities.vel[i].dx;
            entities.pos[i].y += entities.vel[i].dy;
            entities.pos[i].z += entities.vel[i].dz;
        }
        ankerl::nanobench::doNotOptimizeAway(entities.pos.data());
    });
}

static void bench_ecs_each(ankerl::nanobench::Bench& b, int n) {
    World world;
    for (int i = 0; i < n; ++i) {
        world.spawn_with(Position{float(i), 0, 0}, Velocity{1, 2, 3});
    }

    b.run("ecs each " + std::to_string(n), [&]() {
        world.query<Position, Velocity>().each([](Position& pos, Velocity& vel) {
            pos.x += vel.dx;
            pos.y += vel.dy;
            pos.z += vel.dz;
        });
        ankerl::nanobench::doNotOptimizeAway(&world);
    });
}

static void bench_ecs_rangefor(ankerl::nanobench::Bench& b, int n) {
    World world;
    for (int i = 0; i < n; ++i) {
        world.spawn_with(Position{float(i), 0, 0}, Velocity{1, 2, 3});
    }

    b.run("ecs range " + std::to_string(n), [&]() {
        for (auto [pos, vel] : world.query<Position, Velocity>()) {
            pos.x += vel.dx;
            pos.y += vel.dy;
            pos.z += vel.dz;
        }
        ankerl::nanobench::doNotOptimizeAway(&world);
    });
}

int main() {
    ankerl::nanobench::Bench b;
    b.title("Iteration: Position + Velocity")
     .unit("entity")
     .performanceCounters(true);

    for (int n : {100, 1000, 10000, 100000}) {
        bench_aos(b, n);
        bench_soa(b, n);
        bench_ecs_each(b, n);
        bench_ecs_rangefor(b, n);
    }

    b.render(ankerl::nanobench::templates::csv(), std::cout);
    return 0;
}
