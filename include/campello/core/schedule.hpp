#pragma once

#include "world.hpp"
#include "thread_pool.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace campello::core {

// ------------------------------------------------------------------
// Stage: execution ordering
// ------------------------------------------------------------------
enum class Stage {
    Startup,
    PreUpdate,
    Update,
    PostUpdate,
    PreRender,
    Last
};

// ------------------------------------------------------------------
// SystemDescriptor: declare data access for parallel scheduling
// ------------------------------------------------------------------
struct SystemDescriptor {
    std::vector<ComponentId> reads;
    std::vector<ComponentId> writes;
    std::vector<detail::TypeId> read_resources;
    std::vector<detail::TypeId> write_resources;
    Stage stage = Stage::Update;
    std::string name;
    std::vector<std::string> after;
    std::vector<std::string> before;

    template<component... Cs>
    SystemDescriptor& reads_components() {
        (reads.push_back(component_type_id<Cs>()), ...);
        return *this;
    }

    template<component... Cs>
    SystemDescriptor& writes_components() {
        (writes.push_back(component_type_id<Cs>()), ...);
        return *this;
    }

    template<typename... Rs>
    SystemDescriptor& reads_resources() {
        (read_resources.push_back(detail::type_id<Rs>()), ...);
        return *this;
    }

    template<typename... Rs>
    SystemDescriptor& writes_resources() {
        (write_resources.push_back(detail::type_id<Rs>()), ...);
        return *this;
    }

    SystemDescriptor& in_stage(Stage s) {
        stage = s;
        return *this;
    }

    SystemDescriptor& with_name(std::string n) {
        name = std::move(n);
        return *this;
    }

    SystemDescriptor& after_system(std::string n) {
        after.push_back(std::move(n));
        return *this;
    }

    SystemDescriptor& before_system(std::string n) {
        before.push_back(std::move(n));
        return *this;
    }
};

// ------------------------------------------------------------------
// Schedule: dependency-aware system executor
// ------------------------------------------------------------------
class Schedule {
public:
    // Defaults to hardware_concurrency() worker threads, matching ThreadPool's
    // own default. Pass 0 explicitly to force fully sequential execution
    // (deterministic ordering, no worker threads spun up at all).
    //
    // This Schedule owns the pool it creates. If your application already
    // has a shared ThreadPool (e.g. one owned by the engine and also used
    // for each_par() elsewhere, or shared across multiple Schedule
    // instances), prefer the Schedule(ThreadPool&) constructor below instead
    // of letting every Schedule spin up its own hardware_concurrency()
    // threads — those add up fast and compete with your render/input/audio
    // threads for cores.
    explicit Schedule(std::size_t num_threads = std::thread::hardware_concurrency())
        : owned_pool_(num_threads > 0 ? std::make_unique<ThreadPool>(num_threads) : nullptr),
          pool_(owned_pool_.get()) {}

    // Uses an externally-owned pool instead of creating one. `pool` must
    // outlive this Schedule (and any run() call on it). Multiple Schedule
    // instances — and each_par() calls — can safely share the same pool.
    explicit Schedule(ThreadPool& pool) : pool_(&pool) {}

    template<typename F>
    SystemDescriptor& add_system(F&& f) {
        systems_.push_back(SystemNode{std::forward<F>(f), SystemDescriptor{}});
        return systems_.back().desc;
    }

    template<typename F>
    SystemDescriptor& add_system(F&& f, std::string name) {
        systems_.push_back(SystemNode{std::forward<F>(f), SystemDescriptor{}});
        systems_.back().desc.name = std::move(name);
        return systems_.back().desc;
    }

    template<typename F>
    SystemDescriptor& add_system(F&& f, SystemDescriptor desc) {
        systems_.push_back(SystemNode{std::forward<F>(f), std::move(desc)});
        return systems_.back().desc;
    }

    void run(World& world);

    std::size_t system_count() const { return systems_.size(); }

    // Minimum estimated total duration a batch of independent systems must
    // have (summed from each system's last measured run) before it's worth
    // paying thread-pool dispatch overhead. Below this, the batch runs
    // inline even if the systems are independent and threads are available.
    // Default (20us) is calibrated from measuring this pool's dispatch cost
    // for a 3-task batch (~14-15us, roughly constant regardless of workload
    // size) — see docs/PERFORMANCE.md.
    void set_min_parallel_duration(std::chrono::nanoseconds d) { min_parallel_duration_ = d; }
    std::chrono::nanoseconds min_parallel_duration() const { return min_parallel_duration_; }

private:
    struct SystemNode {
        std::function<void(World&)> func;
        SystemDescriptor desc;
    };

    struct ExecNode {
        std::size_t index = 0;
        std::vector<std::size_t> dependents;
        std::size_t in_degree = 0;
        bool executed = false;
    };

    std::vector<SystemNode> systems_;
    // Non-null only when this Schedule created its own pool (the
    // Schedule(num_threads) constructor); null when sharing an external one.
    std::unique_ptr<ThreadPool> owned_pool_;
    // The pool actually used for dispatch: either owned_pool_.get() or an
    // externally-provided pool. Never owning; always points somewhere valid
    // (or is null, meaning "run everything inline").
    ThreadPool* pool_ = nullptr;
    // last_duration_[i] is how long systems_[i] took on its most recent run
    // (sequential or parallel). Starts at zero, so a system is assumed cheap
    // (and runs inline) until it's actually been measured once.
    std::vector<std::chrono::nanoseconds> last_duration_;
    std::chrono::nanoseconds min_parallel_duration_ = std::chrono::microseconds(20);

    bool conflicts(const SystemDescriptor& a, const SystemDescriptor& b) const;
    void run_sequential(World& world);
    void run_parallel(World& world);
};

// ------------------------------------------------------------------
// Inline implementations
// ------------------------------------------------------------------

inline bool Schedule::conflicts(const SystemDescriptor& a, const SystemDescriptor& b) const {
    // Two systems conflict if:
    // 1. Any write-write overlap
    // 2. Any read-write overlap
    auto any_overlap = [](const auto& vec_a, const auto& vec_b) {
        for (const auto& x : vec_a) {
            for (const auto& y : vec_b) {
                if (x == y) return true;
            }
        }
        return false;
    };

    if (any_overlap(a.writes, b.writes)) return true;
    if (any_overlap(a.writes, b.reads)) return true;
    if (any_overlap(a.reads, b.writes)) return true;
    if (any_overlap(a.write_resources, b.write_resources)) return true;
    if (any_overlap(a.write_resources, b.read_resources)) return true;
    if (any_overlap(a.read_resources, b.write_resources)) return true;
    return false;
}

inline void Schedule::run_sequential(World& world) {
    for (auto& sys : systems_) {
        sys.func(world);
    }
}

inline void Schedule::run_parallel(World& world) {
    if (last_duration_.size() != systems_.size()) {
        last_duration_.resize(systems_.size(), std::chrono::nanoseconds{0});
    }

    // Group systems by stage
    std::vector<std::vector<std::size_t>> stage_indices(static_cast<std::size_t>(Stage::Last));
    for (std::size_t i = 0; i < systems_.size(); ++i) {
        stage_indices[static_cast<std::size_t>(systems_[i].desc.stage)].push_back(i);
    }

    // Execute each stage in order, parallelizing within stage
    for (auto& indices : stage_indices) {
        if (indices.empty()) continue;

        // Build DAG for this stage
        std::vector<ExecNode> nodes(indices.size());
        for (std::size_t i = 0; i < indices.size(); ++i) {
            nodes[i].index = indices[i];
        }

        // Build name -> node index map for explicit dependencies
        std::unordered_map<std::string, std::size_t> name_to_node;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            if (!systems_[indices[i]].desc.name.empty()) {
                name_to_node[systems_[indices[i]].desc.name] = i;
            }
        }

        for (std::size_t i = 0; i < indices.size(); ++i) {
            for (std::size_t j = i + 1; j < indices.size(); ++j) {
                if (conflicts(systems_[indices[i]].desc, systems_[indices[j]].desc)) {
                    // i must run before j
                    nodes[i].dependents.push_back(j);
                    nodes[j].in_degree++;
                }
            }
            // Add explicit dependency edges
            for (const auto& dep_name : systems_[indices[i]].desc.after) {
                auto it = name_to_node.find(dep_name);
                if (it != name_to_node.end() && it->second != i) {
                    std::size_t dep_idx = it->second;
                    // dep_idx must run before i
                    nodes[dep_idx].dependents.push_back(i);
                    nodes[i].in_degree++;
                }
            }
            for (const auto& dep_name : systems_[indices[i]].desc.before) {
                auto it = name_to_node.find(dep_name);
                if (it != name_to_node.end() && it->second != i) {
                    std::size_t dep_idx = it->second;
                    // i must run before dep_idx
                    nodes[i].dependents.push_back(dep_idx);
                    nodes[dep_idx].in_degree++;
                }
            }
        }

        // Batch-parallel execution
        std::size_t executed = 0;
        while (executed < indices.size()) {
            std::vector<std::size_t> batch;
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                if (!nodes[i].executed && nodes[i].in_degree == 0) {
                    batch.push_back(i);
                }
            }

            if (batch.empty()) {
                // Cycle or bug — fall back to sequential
                for (std::size_t i = 0; i < nodes.size(); ++i) {
                    if (!nodes[i].executed) {
                        systems_[nodes[i].index].func(world);
                        nodes[i].executed = true;
                        executed++;
                    }
                }
                break;
            }

            std::chrono::nanoseconds batch_estimate{0};
            for (std::size_t node_idx : batch) {
                batch_estimate += last_duration_[nodes[node_idx].index];
            }

            if (batch.size() == 1 || !pool_ || batch_estimate < min_parallel_duration_) {
                // Sequential: single item, no thread pool, or the batch's
                // last-measured cost doesn't clear the dispatch-overhead bar.
                for (std::size_t node_idx : batch) {
                    std::size_t idx = nodes[node_idx].index;
                    auto t0 = std::chrono::steady_clock::now();
                    systems_[idx].func(world);
                    last_duration_[idx] = std::chrono::steady_clock::now() - t0;
                    nodes[node_idx].executed = true;
                    executed++;
                    for (std::size_t dep : nodes[node_idx].dependents) {
                        nodes[dep].in_degree--;
                    }
                }
            } else {
                // Parallel batch. Each task only ever writes its own
                // last_duration_[idx] slot, so no synchronization needed
                // between concurrently-running systems.
                std::vector<std::future<void>> futures;
                futures.reserve(batch.size());
                for (std::size_t node_idx : batch) {
                    futures.push_back(pool_->enqueue([this, &world, &nodes, node_idx]() {
                        std::size_t idx = nodes[node_idx].index;
                        auto t0 = std::chrono::steady_clock::now();
                        systems_[idx].func(world);
                        last_duration_[idx] = std::chrono::steady_clock::now() - t0;
                    }));
                }
                for (auto& f : futures) {
                    f.wait();
                }
                for (std::size_t node_idx : batch) {
                    nodes[node_idx].executed = true;
                    executed++;
                    for (std::size_t dep : nodes[node_idx].dependents) {
                        nodes[dep].in_degree--;
                    }
                }
            }
        }
    }
}

inline void Schedule::run(World& world) {
    if (systems_.size() < 2) {
        run_sequential(world);
    } else {
        run_parallel(world);
    }
}

} // namespace campello::core
