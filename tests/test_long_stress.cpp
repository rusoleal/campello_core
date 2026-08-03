// 1-hour continuous stress loop for campello_core.
//
// Builds as `campello_core_long_stress`. Run without arguments for the
// default 60-minute production validation, or pass `--minutes N` for a
// shorter sanity run.

#include <campello/core/core.hpp>
#include <campello/core/schedule.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace campello::core;

// ------------------------------------------------------------------
// Stress components
// ------------------------------------------------------------------

struct StressPos {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct StressVel {
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
};

struct StressHealth {
    int hp = 0;
};

struct StressEvent {
    int value = 0;
};

struct StressCounter {
    int frame_count = 0;
};

namespace campello::core {

template<> struct ComponentTraits<StressPos>    : ComponentTraitsBase<StressPos>    { static constexpr std::string_view name = "StressPos"; };
template<> struct ComponentTraits<StressVel>    : ComponentTraitsBase<StressVel>    { static constexpr std::string_view name = "StressVel"; };
template<> struct ComponentTraits<StressHealth> : ComponentTraitsBase<StressHealth> { static constexpr std::string_view name = "StressHealth"; };
template<> struct ComponentTraits<StressEvent>  : ComponentTraitsBase<StressEvent>  { static constexpr std::string_view name = "StressEvent"; };

} // namespace campello::core

// ------------------------------------------------------------------
// Deterministic RNG
// ------------------------------------------------------------------

class FastLCG {
    std::uint64_t state_;
public:
    explicit FastLCG(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return state_;
    }

    std::uint32_t u32() { return static_cast<std::uint32_t>(next()); }
    std::uint32_t range(std::uint32_t max) { return max == 0 ? 0 : u32() % max; }
    bool coin() { return (u32() & 1) != 0; }
    float unit_float() { return static_cast<float>(u32()) / static_cast<float>(std::numeric_limits<std::uint32_t>::max()); }
};

// ------------------------------------------------------------------
// Oracle: expected world state
// ------------------------------------------------------------------

struct EntityState {
    bool alive = false;
    bool has_pos = false;
    bool has_vel = false;
    bool has_health = false;
    StressPos pos{};
    StressVel vel{};
    StressHealth health{0};

    bool has_any_component() const {
        return has_pos || has_vel || has_health;
    }
};

using Oracle = std::unordered_map<Entity, EntityState>;

static void apply_move_system(Oracle& oracle) {
    for (auto& [_, st] : oracle) {
        if (!st.alive || !st.has_pos || !st.has_vel) continue;
        st.pos.x += st.vel.dx;
        st.pos.y += st.vel.dy;
        st.pos.z += st.vel.dz;
    }
}

static void apply_health_system(Oracle& oracle) {
    for (auto& [_, st] : oracle) {
        if (!st.alive || !st.has_health) continue;
        st.health.hp = std::max(0, st.health.hp - 1);
    }
}

// ------------------------------------------------------------------
// Verification
// ------------------------------------------------------------------

static bool verify_against_oracle(World& world, const Oracle& oracle, std::uint64_t iteration, const char* phase) {
    std::size_t oracle_alive = 0;

    for (const auto& [e, expected] : oracle) {
        bool world_alive = world.is_alive(e);
        if (world_alive != expected.alive) {
            std::cerr << "[" << phase << " iter " << iteration << "] invariant fail: entity " << e
                      << " alive mismatch (world=" << world_alive
                      << " oracle=" << expected.alive << ")\n";
            return false;
        }

        if (!expected.alive) continue;
        ++oracle_alive;

        auto* pos = world.get<StressPos>(e);
        if ((pos != nullptr) != expected.has_pos) {
            std::cerr << "[" << phase << " iter " << iteration << "] invariant fail: entity " << e
                      << " StressPos presence mismatch\n";
            return false;
        }
        if (pos && (pos->x != expected.pos.x || pos->y != expected.pos.y || pos->z != expected.pos.z)) {
            std::cerr << "[" << phase << " iter " << iteration << "] invariant fail: entity " << e
                      << " StressPos value mismatch\n";
            return false;
        }

        auto* vel = world.get<StressVel>(e);
        if ((vel != nullptr) != expected.has_vel) {
            std::cerr << "[" << phase << " iter " << iteration << "] invariant fail: entity " << e
                      << " StressVel presence mismatch\n";
            return false;
        }
        if (vel && (vel->dx != expected.vel.dx || vel->dy != expected.vel.dy || vel->dz != expected.vel.dz)) {
            std::cerr << "[" << phase << " iter " << iteration << "] invariant fail: entity " << e
                      << " StressVel value mismatch\n";
            return false;
        }

        auto* hp = world.get<StressHealth>(e);
        if ((hp != nullptr) != expected.has_health) {
            std::cerr << "[" << phase << " iter " << iteration << "] invariant fail: entity " << e
                      << " StressHealth presence mismatch\n";
            return false;
        }
        if (hp && hp->hp != expected.health.hp) {
            std::cerr << "[" << phase << " iter " << iteration << "] invariant fail: entity " << e
                      << " StressHealth value mismatch (world=" << hp->hp
                      << " oracle=" << expected.health.hp << ")\n";
            return false;
        }
    }

    std::size_t query_alive = 0;
    world.query<StressPos>().each_with_entity([&](Entity e, StressPos&) {
        if (!world.is_alive(e)) {
            std::cerr << "[" << phase << " iter " << iteration << "] invariant fail: dead entity " << e
                      << " returned by query<StressPos>\n";
            return;
        }
        ++query_alive;
    });

    std::size_t oracle_in_archetypes = 0;
    for (const auto& [_, st] : oracle) {
        if (st.alive && st.has_any_component()) ++oracle_in_archetypes;
    }

    auto stats = world.memory_stats();
    if (stats.alive_entities != oracle_in_archetypes) {
        std::cerr << "[" << phase << " iter " << iteration << "] invariant fail: memory_stats alive="
                  << stats.alive_entities << " oracle_in_archetypes=" << oracle_in_archetypes
                  << " oracle_alive=" << oracle_alive << "\n";
        return false;
    }

    return true;
}

// ------------------------------------------------------------------
// Workload helpers
// ------------------------------------------------------------------

static Entity spawn_random_entity(World& world, Oracle& oracle, FastLCG& rng, int spawn_id) {
    Entity e = world.spawn();
    EntityState st;
    st.alive = true;

    if (rng.coin()) {
        st.has_pos = true;
        st.pos = {rng.unit_float(), rng.unit_float(), rng.unit_float()};
        world.insert<StressPos>(e, st.pos);
    }
    if (rng.coin()) {
        st.has_vel = true;
        st.vel = {rng.unit_float() * 0.1f, rng.unit_float() * 0.1f, rng.unit_float() * 0.1f};
        world.insert<StressVel>(e, st.vel);
    }
    if (rng.coin()) {
        st.has_health = true;
        st.health = {10 + static_cast<int>(rng.range(90))};
        world.insert<StressHealth>(e, st.health);
    }

    oracle[e] = st;
    (void)spawn_id;
    return e;
}

static void mutate_random_entity(World& world, Oracle& oracle, FastLCG& rng,
                                 const std::vector<Entity>& alive) {
    if (alive.empty()) return;

    Entity e = alive[rng.range(static_cast<std::uint32_t>(alive.size()))];
    if (!world.is_alive(e)) return;

    EntityState& st = oracle[e];
    int op = rng.range(6);
    switch (op) {
        case 0:
            if (!st.has_pos) {
                st.has_pos = true;
                st.pos = {rng.unit_float(), rng.unit_float(), rng.unit_float()};
                world.insert<StressPos>(e, st.pos);
            }
            break;
        case 1:
            if (st.has_pos) {
                world.remove<StressPos>(e);
                st.has_pos = false;
            }
            break;
        case 2:
            if (!st.has_vel) {
                st.has_vel = true;
                st.vel = {rng.unit_float() * 0.1f, rng.unit_float() * 0.1f, rng.unit_float() * 0.1f};
                world.insert<StressVel>(e, st.vel);
            }
            break;
        case 3:
            if (st.has_vel) {
                world.remove<StressVel>(e);
                st.has_vel = false;
            }
            break;
        case 4:
            if (!st.has_health) {
                st.has_health = true;
                st.health = {10 + static_cast<int>(rng.range(90))};
                world.insert<StressHealth>(e, st.health);
            }
            break;
        case 5:
            if (st.has_health) {
                world.remove<StressHealth>(e);
                st.has_health = false;
            }
            break;
    }
}

static void collect_alive(const Oracle& oracle, std::vector<Entity>& out) {
    out.clear();
    for (const auto& [e, st] : oracle) {
        if (st.alive) out.push_back(e);
    }
}

static void cleanup_dead_oracle(Oracle& oracle) {
    for (auto it = oracle.begin(); it != oracle.end();) {
        if (!it->second.alive) {
            it = oracle.erase(it);
        } else {
            ++it;
        }
    }
}

// ------------------------------------------------------------------
// Main stress loop
// ------------------------------------------------------------------

struct Args {
    int minutes = 60;
    bool seed_provided = false;
    std::uint64_t seed = 0;
};

static Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--minutes") {
            args.minutes = std::max(0, std::atoi(argv[i + 1]));
        } else if (std::string(argv[i]) == "--seed") {
            args.seed_provided = true;
            args.seed = static_cast<std::uint64_t>(std::stoull(argv[i + 1]));
        }
    }
    return args;
}

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    if (args.minutes <= 0) {
        std::cerr << "Invalid duration. Usage: " << argv[0] << " [--minutes N] [--seed S]\n";
        return 1;
    }

    std::random_device rd;
    const std::uint64_t seed = args.seed_provided ? args.seed : rd();
    FastLCG rng(seed);

    std::cout << "campello_core long stress loop\n";
    std::cout << "duration: " << args.minutes << " minute(s)\n";
    std::cout << "seed: " << seed << "\n";
    std::cout << "---\n";

    try {
        World world;
        world.init_resource<StressCounter>();

        Schedule schedule;

        schedule.add_system([](World& w) {
            for (auto [pos, vel] : w.query<StressPos, StressVel>()) {
                pos.x += vel.dx;
                pos.y += vel.dy;
                pos.z += vel.dz;
            }
        }, "move")
            .writes_components<StressPos>()
            .reads_components<StressVel>();

        schedule.add_system([](World& w) {
            for (auto [hp] : w.query<StressHealth>()) {
                hp.hp = std::max(0, hp.hp - 1);
            }
        }, "health")
            .writes_components<StressHealth>();

        schedule.add_system([](World& w) {
            w.resource<StressCounter>().frame_count++;
            int count = 0;
            for (auto [pos] : w.query<StressPos>()) {
                (void)pos;
                ++count;
            }
            w.send<StressEvent>(StressEvent{count});
        }, "counter")
            .reads_components<StressPos>()
            .writes_resources<StressCounter>();

        Oracle oracle;
        std::vector<Entity> alive;
        alive.reserve(8192);

        const auto start = std::chrono::steady_clock::now();
        const auto duration = std::chrono::minutes(args.minutes);
        auto next_report = start + std::chrono::minutes(1);

        std::uint64_t iteration = 0;
        std::uint64_t last_report_iteration = 0;

        while (true) {
            const auto now = std::chrono::steady_clock::now();
            if (now - start >= duration) break;

            // ---- spawn batch ----
            std::uint32_t spawn_count = 10 + rng.range(16);
            for (std::uint32_t i = 0; i < spawn_count; ++i) {
                if (alive.size() >= 5000) break; // cap alive entities
                spawn_random_entity(world, oracle, rng, static_cast<int>(iteration * 100 + i));
            }
            if (!verify_against_oracle(world, oracle, iteration, "spawn")) {
                return 2;
            }

            // ---- mutate ----
            collect_alive(oracle, alive);
            std::uint32_t mutate_count = 10 + rng.range(21);
            for (std::uint32_t i = 0; i < mutate_count; ++i) {
                mutate_random_entity(world, oracle, rng, alive);
            }
            if (!verify_against_oracle(world, oracle, iteration, "mutate")) {
                return 2;
            }

            // ---- despawn ----
            collect_alive(oracle, alive);
            std::uint32_t despawn_count = alive.empty() ? 0 : 2 + rng.range(static_cast<std::uint32_t>(std::min<std::size_t>(alive.size(), 8)));
            for (std::uint32_t i = 0; i < despawn_count && !alive.empty(); ++i) {
                std::uint32_t idx = rng.range(static_cast<std::uint32_t>(alive.size()));
                Entity e = alive[idx];
                alive[idx] = alive.back();
                alive.pop_back();
                if (world.is_alive(e)) {
                    world.despawn(e);
                    oracle[e].alive = false;
                }
            }
            if (!verify_against_oracle(world, oracle, iteration, "despawn")) {
                return 2;
            }

            // ---- update oracle with expected system effects ----
            apply_move_system(oracle);
            apply_health_system(oracle);

            // ---- run schedule on world ----
            schedule.run(world);

            // ---- verify ----
            if (!verify_against_oracle(world, oracle, iteration, "schedule")) {
                return 2;
            }

            // ---- verify counter resource ----
            if (world.resource<StressCounter>().frame_count != static_cast<int>(iteration + 1)) {
                std::cerr << "[verify iter " << iteration << "] invariant fail: frame_count="
                          << world.resource<StressCounter>().frame_count
                          << " expected " << (iteration + 1) << "\n";
                return 2;
            }

            // ---- verify events ----
            {
                std::size_t pos_count = 0;
                world.query<StressPos>().each([&](StressPos&) { ++pos_count; });

                auto reader = world.event_reader<StressEvent>();
                std::size_t event_count = 0;
                int event_value = -1;
                for (const auto& ev : reader) {
                    event_value = ev.value;
                    ++event_count;
                }
                if (event_count != 1) {
                    std::cerr << "[verify iter " << iteration << "] invariant fail: expected 1 StressEvent, got "
                              << event_count << "\n";
                    return 2;
                }
                if (event_value != static_cast<int>(pos_count)) {
                    std::cerr << "[verify iter " << iteration << "] invariant fail: event value="
                              << event_value << " expected " << pos_count << "\n";
                    return 2;
                }
            }

            // ---- clear events ----
            world.clear_events();

            // ---- periodic cleanup and reporting ----
            if (now >= next_report) {
                cleanup_dead_oracle(oracle);
                collect_alive(oracle, alive);
                auto stats = world.memory_stats();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
                std::cout << "[" << elapsed / 60 << "m] iterations=" << iteration
                          << " (" << (iteration - last_report_iteration) << " this minute)"
                          << " alive=" << alive.size()
                          << " archetypes=" << stats.archetype_count
                          << " chunks=" << stats.chunk_count
                          << " bytes/entity=" << stats.bytes_per_entity()
                          << "\n";
                last_report_iteration = iteration;
                next_report += std::chrono::minutes(1);
            }

            ++iteration;
        }

        cleanup_dead_oracle(oracle);
        collect_alive(oracle, alive);

        const auto end = std::chrono::steady_clock::now();
        const auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        auto stats = world.memory_stats();

        std::cout << "---\n";
        std::cout << "stress loop completed successfully\n";
        std::cout << "total iterations: " << iteration << "\n";
        std::cout << "elapsed: " << elapsed_sec << "s\n";
        std::cout << "final alive entities: " << alive.size() << "\n";
        std::cout << "final archetypes: " << stats.archetype_count << "\n";
        std::cout << "final chunks: " << stats.chunk_count << "\n";
        std::cout << "final bytes/entity: " << stats.bytes_per_entity() << "\n";

        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "stress loop aborted with exception: " << ex.what() << "\n";
        return 3;
    } catch (...) {
        std::cerr << "stress loop aborted with unknown exception\n";
        return 4;
    }
}
