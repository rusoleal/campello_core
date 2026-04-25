#pragma once

#include "world.hpp"
#include "detail/thread_pool.hpp"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <future>
#include <string>
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
    Schedule() = default;
    explicit Schedule(std::size_t num_threads)
        : pool_(num_threads > 0 ? std::make_unique<detail::ThreadPool>(num_threads) : nullptr) {}

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
    std::unique_ptr<detail::ThreadPool> pool_;

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

            if (batch.size() == 1 || !pool_) {
                // Sequential for single item or no thread pool
                for (std::size_t node_idx : batch) {
                    systems_[nodes[node_idx].index].func(world);
                    nodes[node_idx].executed = true;
                    executed++;
                    for (std::size_t dep : nodes[node_idx].dependents) {
                        nodes[dep].in_degree--;
                    }
                }
            } else {
                // Parallel batch
                std::vector<std::future<void>> futures;
                futures.reserve(batch.size());
                for (std::size_t node_idx : batch) {
                    futures.push_back(pool_->enqueue([this, &world, &nodes, node_idx]() {
                        systems_[nodes[node_idx].index].func(world);
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
