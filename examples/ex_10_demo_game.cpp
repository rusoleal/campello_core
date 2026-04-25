#include <campello/core/core.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <random>
#include <chrono>
#include <algorithm>

using namespace campello::core;
using namespace std::chrono;

// ------------------------------------------------------------------
// Components
// ------------------------------------------------------------------

struct Position {
    float x = 0, y = 0;
};

struct Velocity {
    float dx = 0, dy = 0;
};

struct Body {
    float radius = 5.0f;
    float mass = 1.0f;
};

struct Health {
    int hp = 100;
    int max_hp = 100;
};

struct Particle {
    float lifetime = 1.0f;
    float max_lifetime = 1.0f;
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

struct Emitter {
    float spawn_rate = 0.1f; // particles per frame
    float timer = 0;
};

struct Player {
    int score = 0;
};

// ------------------------------------------------------------------
// ComponentTraits for reflection / serialization
// ------------------------------------------------------------------

namespace campello::core {
template<> struct ComponentTraits<Position>  : ComponentTraitsBase<Position>  { static constexpr std::string_view name = "Position"; };
template<> struct ComponentTraits<Velocity>  : ComponentTraitsBase<Velocity>  { static constexpr std::string_view name = "Velocity"; };
template<> struct ComponentTraits<Body>      : ComponentTraitsBase<Body>      { static constexpr std::string_view name = "Body"; };
template<> struct ComponentTraits<Health>    : ComponentTraitsBase<Health>    { static constexpr std::string_view name = "Health"; };
template<> struct ComponentTraits<Particle>  : ComponentTraitsBase<Particle>  { static constexpr std::string_view name = "Particle"; };
template<> struct ComponentTraits<Color>     : ComponentTraitsBase<Color>     { static constexpr std::string_view name = "Color"; };
template<> struct ComponentTraits<Emitter>   : ComponentTraitsBase<Emitter>   { static constexpr std::string_view name = "Emitter"; };
template<> struct ComponentTraits<Player>    : ComponentTraitsBase<Player>    { static constexpr std::string_view name = "Player"; };
} // namespace campello::core

// ------------------------------------------------------------------
// Simulation constants
// ------------------------------------------------------------------

constexpr float WORLD_W = 1000.0f;
constexpr float WORLD_H = 800.0f;
constexpr int INITIAL_BODIES = 1000;
constexpr int INITIAL_EMITTERS = 50;
constexpr int FRAMES = 600;
constexpr int REPORT_INTERVAL = 60;

// ------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------

static std::mt19937 rng(42);

static float randf(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng);
}

static int randi(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

// ------------------------------------------------------------------
// Systems
// ------------------------------------------------------------------

// Physics: integrate velocity + boundary bounce
void physics_system(World& world) {
    world.query<Position, Velocity, Body>().each([](Position& pos, Velocity& vel, Body& body) {
        pos.x += vel.dx;
        pos.y += vel.dy;

        if (pos.x < body.radius) { pos.x = body.radius; vel.dx = std::abs(vel.dx); }
        if (pos.x > WORLD_W - body.radius) { pos.x = WORLD_W - body.radius; vel.dx = -std::abs(vel.dx); }
        if (pos.y < body.radius) { pos.y = body.radius; vel.dy = std::abs(vel.dy); }
        if (pos.y > WORLD_H - body.radius) { pos.y = WORLD_H - body.radius; vel.dy = -std::abs(vel.dy); }
    });
}

// Spatial-grid collision: O(n) average, O(n^2) worst-case
void collision_system(World& world) {
    struct BodyEntry {
        Position* pos;
        Body* body;
    };

    std::vector<BodyEntry> entries;
    entries.reserve(INITIAL_BODIES);
    world.query<Position, Body>().each([&](Position& p, Body& b) {
        entries.push_back({&p, &b});
    });

    const int n = static_cast<int>(entries.size());
    if (n < 2) return;

    constexpr float CELL_SIZE = 20.0f;
    constexpr int GRID_W = static_cast<int>(WORLD_W / CELL_SIZE) + 1;
    constexpr int GRID_H = static_cast<int>(WORLD_H / CELL_SIZE) + 1;

    std::vector<std::vector<int>> grid(GRID_W * GRID_H);
    for (int i = 0; i < n; ++i) {
        int gx = static_cast<int>(entries[i].pos->x / CELL_SIZE);
        int gy = static_cast<int>(entries[i].pos->y / CELL_SIZE);
        gx = std::clamp(gx, 0, GRID_W - 1);
        gy = std::clamp(gy, 0, GRID_H - 1);
        grid[gy * GRID_W + gx].push_back(i);
    }

    const int NEIGHBORS[4][2] = { {1, 0}, {1, 1}, {0, 1}, {-1, 1} };

    for (int gy = 0; gy < GRID_H; ++gy) {
        for (int gx = 0; gx < GRID_W; ++gx) {
            const auto& cell = grid[gy * GRID_W + gx];

            for (size_t a = 0; a < cell.size(); ++a) {
                for (size_t b = a + 1; b < cell.size(); ++b) {
                    int i = cell[a];
                    int j = cell[b];
                    float dx = entries[i].pos->x - entries[j].pos->x;
                    float dy = entries[i].pos->y - entries[j].pos->y;
                    float dist_sq = dx * dx + dy * dy;
                    float min_dist = entries[i].body->radius + entries[j].body->radius;
                    if (dist_sq < min_dist * min_dist && dist_sq > 0.01f) {
                        float dist = std::sqrt(dist_sq);
                        float nx = dx / dist;
                        float ny = dy / dist;
                        float overlap = min_dist - dist;
                        entries[i].pos->x += nx * overlap * 0.5f;
                        entries[i].pos->y += ny * overlap * 0.5f;
                        entries[j].pos->x -= nx * overlap * 0.5f;
                        entries[j].pos->y -= ny * overlap * 0.5f;
                    }
                }
            }

            for (int nidx = 0; nidx < 4; ++nidx) {
                int ngx = gx + NEIGHBORS[nidx][0];
                int ngy = gy + NEIGHBORS[nidx][1];
                if (ngx < 0 || ngx >= GRID_W || ngy < 0 || ngy >= GRID_H) continue;
                const auto& neighbor = grid[ngy * GRID_W + ngx];

                for (int i : cell) {
                    for (int j : neighbor) {
                        float dx = entries[i].pos->x - entries[j].pos->x;
                        float dy = entries[i].pos->y - entries[j].pos->y;
                        float dist_sq = dx * dx + dy * dy;
                        float min_dist = entries[i].body->radius + entries[j].body->radius;
                        if (dist_sq < min_dist * min_dist && dist_sq > 0.01f) {
                            float dist = std::sqrt(dist_sq);
                            float nx = dx / dist;
                            float ny = dy / dist;
                            float overlap = min_dist - dist;
                            entries[i].pos->x += nx * overlap * 0.5f;
                            entries[i].pos->y += ny * overlap * 0.5f;
                            entries[j].pos->x -= nx * overlap * 0.5f;
                            entries[j].pos->y -= ny * overlap * 0.5f;
                        }
                    }
                }
            }
        }
    }
}

// Aging: bodies slowly lose health, die when hp <= 0
void aging_system(World& world) {
    std::vector<Entity> dead;
    std::vector<Entity> explosions;

    world.query<Position, Health, Body>().each_with_entity([&](Entity e, Position& pos, Health& h, Body&) {
        if (randi(0, 99) == 0) {
            h.hp -= randi(10, 30);
            world.mark_changed<Health>(e);
        }

        if (h.hp <= 0) {
            for (int i = 0; i < 8; ++i) {
                explosions.push_back(world.spawn_with(
                    Position{pos.x + randf(-5, 5), pos.y + randf(-5, 5)},
                    Velocity{randf(-3, 3), randf(-3, 3)},
                    Particle{randf(0.3f, 0.8f), randf(0.3f, 0.8f)},
                    Color{randf(0.5f, 1.0f), randf(0.2f, 0.6f), 0.1f, 1.0f}
                ));
            }
            dead.push_back(e);
        }
    });

    world.despawn_many(dead);
}

// Update particle lifetimes, despawn expired
void particle_system(World& world) {
    std::vector<Entity> expired;

    world.query<Position, Velocity, Particle, Color>().each_with_entity(
        [&](Entity e, Position& pos, Velocity& vel, Particle& p, Color& c) {
            p.lifetime -= 0.016f;
            pos.x += vel.dx * 0.5f;
            pos.y += vel.dy * 0.5f;
            c.a = std::max(0.0f, p.lifetime / p.max_lifetime);
            if (p.lifetime <= 0) {
                expired.push_back(e);
            }
        }
    );

    world.despawn_many(expired);
}

// Emitter system: spawn particles from emitters
void emitter_system(World& world) {
    world.query<Position, Emitter>().each_with_entity(
        [&](Entity, Position& pos, Emitter& em) {
            em.timer += 0.016f;
            while (em.timer >= em.spawn_rate) {
                em.timer -= em.spawn_rate;
                world.spawn_with(
                    Position{pos.x + randf(-2, 2), pos.y + randf(-2, 2)},
                    Velocity{randf(-1, 1), randf(-2, 0)},
                    Particle{randf(0.5f, 1.5f), randf(0.5f, 1.5f)},
                    Color{0.8f, 0.9f, 1.0f, 0.6f}
                );
            }
        }
    );
}

// Hierarchy: child entities follow their parent with an offset
void hierarchy_system(World& world) {
    world.query<Position, Parent>().each([&](Position& child_pos, Parent& parent) {
        if (world.is_alive(parent.entity)) {
            auto* parent_pos = world.get<Position>(parent.entity);
            if (parent_pos) {
                child_pos.x = parent_pos->x + 15.0f;
                child_pos.y = parent_pos->y;
            }
        }
    });
}

// ------------------------------------------------------------------
// Main
// ------------------------------------------------------------------

int main() {
    World world;

    // Register all components
    world.register_component<Position>();
    world.register_component<Velocity>();
    world.register_component<Body>();
    world.register_component<Health>();
    world.register_component<Particle>();
    world.register_component<Color>();
    world.register_component<Emitter>();
    world.register_component<Player>();

    // --- Spawn initial entities ---

    // Physics bodies
    for (int i = 0; i < INITIAL_BODIES; ++i) {
        world.spawn_with(
            Position{randf(50, WORLD_W - 50), randf(50, WORLD_H - 50)},
            Velocity{randf(-2, 2), randf(-2, 2)},
            Body{randf(3, 8), randf(0.5f, 2.0f)},
            Health{100, 100},
            Color{randf(0.3f, 0.9f), randf(0.3f, 0.9f), randf(0.3f, 0.9f), 1.0f}
        );
    }

    // Player entity
    Entity player = world.spawn_with(
        Position{WORLD_W / 2, WORLD_H / 2},
        Velocity{0, 0},
        Body{10.0f, 5.0f},
        Health{500, 500},
        Player{0},
        Color{1.0f, 0.2f, 0.2f, 1.0f}
    );
    (void)player;

    // Emitters with hierarchical children (visual markers)
    for (int i = 0; i < INITIAL_EMITTERS; ++i) {
        Entity emitter = world.spawn_with(
            Position{randf(100, WORLD_W - 100), randf(100, WORLD_H - 100)},
            Velocity{randf(-0.5f, 0.5f), randf(-0.5f, 0.5f)},
            Emitter{randf(0.05f, 0.2f), 0},
            Color{1.0f, 1.0f, 0.2f, 1.0f}
        );
        (void)emitter;

        // Child marker that follows parent via hierarchy_system
        Entity marker = world.spawn_with(
            Position{0, 0},
            Color{1.0f, 0.5f, 0.0f, 0.8f}
        );
        world.set_parent(marker, emitter);
    }

    std::cout << "=== campello_core Demo Game ===\n";
    std::cout << "World: " << WORLD_W << "x" << WORLD_H << "\n";
    std::cout << "Initial bodies: " << INITIAL_BODIES << "\n";
    std::cout << "Initial emitters: " << INITIAL_EMITTERS << "\n";
    std::cout << "Simulating " << FRAMES << " frames...\n\n";

    auto stats_start = world.memory_stats();
    std::cout << "Initial memory: " << stats_start.total_bytes() / 1024 << " KB"
              << " (" << stats_start.bytes_per_entity() << " bytes/entity)\n";
    std::cout << "Chunks: " << stats_start.chunk_count
              << "  Archetypes: " << stats_start.archetype_count << "\n\n";

    // --- Build schedule ---
    Schedule schedule;
    schedule.add_system([&](World& w) { physics_system(w); },    "physics");
    schedule.add_system([&](World& w) { collision_system(w); },  "collision");
    schedule.add_system([&](World& w) { hierarchy_system(w); },  "hierarchy");

    // --- Run simulation ---
    std::vector<float> frame_times;
    frame_times.reserve(FRAMES);

    auto sim_start = steady_clock::now();

    for (int frame = 0; frame < FRAMES; ++frame) {
        auto frame_start = steady_clock::now();

        schedule.run(world);
        aging_system(world);
        particle_system(world);
        emitter_system(world);

        auto frame_end = steady_clock::now();
        float ms = duration<float, std::milli>(frame_end - frame_start).count();
        frame_times.push_back(ms);

        if ((frame + 1) % REPORT_INTERVAL == 0) {
            float min_ms = frame_times[0];
            float max_ms = frame_times[0];
            float sum_ms = 0;
            for (float t : frame_times) {
                min_ms = std::min(min_ms, t);
                max_ms = std::max(max_ms, t);
                sum_ms += t;
            }
            float avg_ms = sum_ms / frame_times.size();

            auto stats = world.memory_stats();
            std::cout << "Frame " << (frame + 1)
                      << " | bodies: " << world.query<Body>().count()
                      << " | particles: " << world.query<Particle>().count()
                      << " | total alive: " << stats.alive_entities
                      << " | mem: " << stats.total_bytes() / 1024 << " KB"
                      << " | min/avg/max ms: " << std::fixed << std::setprecision(2)
                      << min_ms << "/" << avg_ms << "/" << max_ms
                      << " | fps: " << int(1000.0f / avg_ms) << "\n";
            frame_times.clear();
        }
    }

    auto sim_end = steady_clock::now();
    float total_sec = duration<float>(sim_end - sim_start).count();

    auto stats_end = world.memory_stats();
    std::cout << "\n=== Simulation Complete ===\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << total_sec << " s\n";
    std::cout << "Avg frame time: " << (total_sec * 1000.0f / FRAMES) << " ms\n";
    std::cout << "Avg FPS: " << int(FRAMES / total_sec) << "\n";
    std::cout << "Final entities: " << stats_end.alive_entities << "\n";
    std::cout << "Final memory: " << stats_end.total_bytes() / 1024 << " KB\n";
    std::cout << "Peak archetypes: " << stats_end.archetype_count << "\n";

    // Target: 60fps = 16.67ms per frame
    float avg_ms = total_sec * 1000.0f / FRAMES;
    if (avg_ms < 16.67f) {
        std::cout << "\n✅ PASSED: Maintained 60fps budget (" << avg_ms << " ms/frame)\n";
    } else {
        std::cout << "\n⚠️  MISSED: Exceeded 60fps budget (" << avg_ms << " ms/frame)\n";
    }

    return 0;
}
