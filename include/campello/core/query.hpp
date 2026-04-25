#pragma once

#include "types.hpp"
#include "component.hpp"
#include "archetype.hpp"
#include "detail/component_registry.hpp"
#include "detail/thread_pool.hpp"
#include <future>
#include <tuple>
#include <type_traits>
#include <vector>

namespace campello::core {

// ------------------------------------------------------------------
// Query filter type markers
// ------------------------------------------------------------------
template<component T>
struct Added {};

template<component T>
struct Changed {};

// Legacy filters (not yet implemented as runtime filters)
template<component T>
struct With {};

template<component T>
struct Without {};

namespace detail {

template<typename T>
struct component_access {
    using raw_type = std::remove_cv_t<std::remove_reference_t<T>>;
    static constexpr bool is_const = std::is_const_v<std::remove_reference_t<T>>;
    static ComponentId id() { return component_type_id<raw_type>(); }
};

// Type index helper
template<typename C, typename... Ts>
struct type_index_impl;

template<typename C, typename T, typename... Rest>
struct type_index_impl<C, T, Rest...> {
    static constexpr std::size_t value = std::is_same_v<C, T> ? 0 : 1 + type_index_impl<C, Rest...>::value;
};

template<typename C>
struct type_index_impl<C> {
    static constexpr std::size_t value = 0;
};

} // namespace detail

// ------------------------------------------------------------------
// Query: archetype-matched component iteration
// ------------------------------------------------------------------
template<typename... Cs>
class Query {
public:
    using item_type = std::tuple<Cs&...>;

    Query(ArchetypeStorage* storage, ComponentRegistry* registry, std::uint64_t* version)
        : storage_(storage), registry_(registry), world_version_(version) {}

    // Pre-populate matching archetypes from a world-level cache.
    // `version` must be the current archetype_version_ of the world.
    void set_cached_archetypes(const std::vector<ArchetypeId>& archetypes, std::uint64_t version) {
        matching_archetypes_ = archetypes;
        cached_version_ = version;
    }

    // Access the cached matching archetypes (for world-level query caching).
    const std::vector<ArchetypeId>& matching_archetypes() const {
        return matching_archetypes_;
    }

    // --- Change detection filters ---

    template<component T>
    Query added() {
        filter_added_.push_back(component_type_id<T>());
        return *this;
    }

    template<component T>
    Query changed() {
        filter_changed_.push_back(component_type_id<T>());
        return *this;
    }

    Query with_last_run_tick(Tick tick) {
        last_run_tick_ = tick;
        return *this;
    }

    // --- Iteration ---

    // Iterate with lambda: query.each([](Position& p, const Velocity& v){ ... });
    template<typename Func>
    void each(Func&& func) {
        update_cache();
        ComponentId required[] = { detail::component_access<Cs>::id()... };
        for (ArchetypeId arch_id : matching_archetypes_) {
            Archetype* arch = storage_->get_archetype(arch_id);
            if (!arch) continue;

            std::size_t comp_indices[sizeof...(Cs)];
            for (std::size_t i = 0; i < sizeof...(Cs); ++i) {
                comp_indices[i] = arch->component_index(required[i]);
            }

            for (std::size_t chunk_idx = 0; chunk_idx < arch->chunks.size(); ++chunk_idx) {
                Chunk& chunk = arch->chunks[chunk_idx];
                if (chunk.count == 0) continue;
                for (std::uint32_t row = 0; row < chunk.count; ++row) {
                    if (!passes_filters(*arch, static_cast<std::uint32_t>(chunk_idx), row)) continue;
                    func(get_component_ref<Cs>(chunk, row, comp_indices[index_of<Cs>()])...);
                }
            }
        }
    }

    // Iterate with entity handle: query.each_with_entity([](Entity e, Position& p, Velocity& v){ ... });
    template<typename Func>
    void each_with_entity(Func&& func) {
        update_cache();
        ComponentId required[] = { detail::component_access<Cs>::id()... };
        for (ArchetypeId arch_id : matching_archetypes_) {
            Archetype* arch = storage_->get_archetype(arch_id);
            if (!arch) continue;

            std::size_t comp_indices[sizeof...(Cs)];
            for (std::size_t i = 0; i < sizeof...(Cs); ++i) {
                comp_indices[i] = arch->component_index(required[i]);
            }

            for (std::size_t chunk_idx = 0; chunk_idx < arch->chunks.size(); ++chunk_idx) {
                Chunk& chunk = arch->chunks[chunk_idx];
                if (chunk.count == 0) continue;
                Entity* entities = reinterpret_cast<Entity*>(chunk.data + chunk.offsets[0]);
                for (std::uint32_t row = 0; row < chunk.count; ++row) {
                    if (!passes_filters(*arch, static_cast<std::uint32_t>(chunk_idx), row)) continue;
                    func(entities[row], get_component_ref<Cs>(chunk, row, comp_indices[index_of<Cs>()])...);
                }
            }
        }
    }

    // Parallel chunk iteration: each chunk is processed by a different thread.
    // The caller must ensure `func` is thread-safe (no mutable shared state).
    template<typename Func>
    void each_par(detail::ThreadPool& pool, Func&& func) {
        update_cache();
        ComponentId required[] = { detail::component_access<Cs>::id()... };

        struct ChunkTask {
            Chunk* chunk;
            std::size_t comp_indices[sizeof...(Cs)];
            Archetype* arch;
            std::uint32_t chunk_idx;
        };
        std::vector<ChunkTask> tasks;

        for (ArchetypeId arch_id : matching_archetypes_) {
            Archetype* arch = storage_->get_archetype(arch_id);
            if (!arch) continue;

            std::size_t comp_indices[sizeof...(Cs)];
            for (std::size_t i = 0; i < sizeof...(Cs); ++i) {
                comp_indices[i] = arch->component_index(required[i]);
            }

            for (std::size_t chunk_idx = 0; chunk_idx < arch->chunks.size(); ++chunk_idx) {
                Chunk& chunk = arch->chunks[chunk_idx];
                if (chunk.count == 0) continue;
                tasks.push_back({&chunk, {}, arch, static_cast<std::uint32_t>(chunk_idx)});
                for (std::size_t i = 0; i < sizeof...(Cs); ++i) {
                    tasks.back().comp_indices[i] = comp_indices[i];
                }
            }
        }

        std::vector<std::future<void>> futures;
        futures.reserve(tasks.size());
        Tick last_run = last_run_tick_;
        std::vector<ComponentId> added = filter_added_;
        std::vector<ComponentId> changed = filter_changed_;
        for (auto& task : tasks) {
            futures.push_back(pool.enqueue([this, &func, &task, last_run, added, changed]() {
                for (std::uint32_t row = 0; row < task.chunk->count; ++row) {
                    if (!this->passes_filters_task(*task.arch, task.chunk_idx, row, last_run, added, changed)) continue;
                    func(this->get_component_ref<Cs>(*task.chunk, row, task.comp_indices[this->index_of<Cs>()])...);
                }
            }));
        }
        for (auto& f : futures) {
            f.wait();
        }
    }

    // Standard iteration support
    class Iterator {
    public:
        Iterator() = default;
        Iterator(ArchetypeStorage* storage, ComponentRegistry* registry,
                 const std::vector<ArchetypeId>* archetypes,
                 Tick last_run_tick,
                 const std::vector<ComponentId>* filter_added,
                 const std::vector<ComponentId>* filter_changed,
                 std::size_t arch_idx, std::uint32_t chunk_idx, std::uint32_t row)
            : storage_(storage), registry_(registry), archetypes_(archetypes),
              last_run_tick_(last_run_tick),
              filter_added_(filter_added), filter_changed_(filter_changed),
              arch_idx_(arch_idx), chunk_idx_(chunk_idx), row_(row) {
            advance_to_valid();
            cache_indices();
        }

        item_type operator*() {
            Chunk& chunk = current_chunk();
            return std::tuple<Cs&...>(get_component_ref<Cs>(chunk, row_, comp_indices_[index_of<Cs>()])...);
        }

        Iterator& operator++() {
            ++row_;
            advance_to_valid();
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return arch_idx_ != other.arch_idx_ ||
                   chunk_idx_ != other.chunk_idx_ ||
                   row_ != other.row_;
        }

    private:
        static constexpr std::size_t index_of() { return 0; }
        template<typename T>
        static constexpr std::size_t index_of() {
            return detail::type_index_impl<std::remove_cv_t<T>, std::remove_cv_t<Cs>...>::value;
        }

        bool passes_filters_current() const {
            Archetype* arch = storage_->get_archetype((*archetypes_)[arch_idx_]);
            if (!arch) return false;
            for (ComponentId cid : *filter_added_) {
                std::size_t comp_idx = arch->component_index(cid);
                if (comp_idx == static_cast<std::size_t>(-1)) return false;
                Tick t = storage_->get_added_tick(*arch, chunk_idx_, comp_idx, row_);
                if (t <= last_run_tick_) return false;
            }
            for (ComponentId cid : *filter_changed_) {
                std::size_t comp_idx = arch->component_index(cid);
                if (comp_idx == static_cast<std::size_t>(-1)) return false;
                Tick t = storage_->get_changed_tick(*arch, chunk_idx_, comp_idx, row_);
                if (t <= last_run_tick_) return false;
            }
            return true;
        }

        void advance_to_valid() {
            while (arch_idx_ < archetypes_->size()) {
                Archetype* arch = storage_->get_archetype((*archetypes_)[arch_idx_]);
                if (!arch || chunk_idx_ >= arch->chunks.size()) {
                    ++arch_idx_;
                    chunk_idx_ = 0;
                    row_ = 0;
                    cache_indices();
                    continue;
                }
                Chunk& chunk = arch->chunks[chunk_idx_];
                if (row_ >= chunk.count) {
                    ++chunk_idx_;
                    row_ = 0;
                    cache_indices();
                    continue;
                }
                if (passes_filters_current()) {
                    return;
                }
                ++row_;
            }
        }

        void cache_indices() {
            if (arch_idx_ >= archetypes_->size()) return;
            Archetype* arch = storage_->get_archetype((*archetypes_)[arch_idx_]);
            if (!arch) return;
            ComponentId required[] = { detail::component_access<Cs>::id()... };
            for (std::size_t i = 0; i < sizeof...(Cs); ++i) {
                comp_indices_[i] = arch->component_index(required[i]);
            }
        }

        Chunk& current_chunk() {
            Archetype* arch = storage_->get_archetype((*archetypes_)[arch_idx_]);
            return arch->chunks[chunk_idx_];
        }

        ArchetypeStorage* storage_ = nullptr;
        ComponentRegistry* registry_ = nullptr;
        const std::vector<ArchetypeId>* archetypes_ = nullptr;
        Tick last_run_tick_ = 0;
        const std::vector<ComponentId>* filter_added_ = nullptr;
        const std::vector<ComponentId>* filter_changed_ = nullptr;
        std::size_t arch_idx_ = 0;
        std::uint32_t chunk_idx_ = 0;
        std::uint32_t row_ = 0;
        std::size_t comp_indices_[sizeof...(Cs)] = {};
    };

    Iterator begin() {
        update_cache();
        return Iterator(storage_, registry_, &matching_archetypes_,
                        last_run_tick_, &filter_added_, &filter_changed_,
                        0, 0, 0);
    }

    Iterator end() {
        update_cache();
        return Iterator(storage_, registry_, &matching_archetypes_,
                        last_run_tick_, &filter_added_, &filter_changed_,
                        matching_archetypes_.size(), 0, 0);
    }

    std::size_t count() const {
        update_cache();
        std::size_t total = 0;
        for (ArchetypeId id : matching_archetypes_) {
            Archetype* arch = storage_->get_archetype(id);
            if (!arch) continue;
            for (std::size_t chunk_idx = 0; chunk_idx < arch->chunks.size(); ++chunk_idx) {
                Chunk& chunk = arch->chunks[chunk_idx];
                for (std::uint32_t row = 0; row < chunk.count; ++row) {
                    if (passes_filters(*arch, static_cast<std::uint32_t>(chunk_idx), row)) {
                        ++total;
                    }
                }
            }
        }
        return total;
    }

    bool empty() const { return count() == 0; }

private:
    template<typename T>
    static constexpr std::size_t index_of() {
        return detail::type_index_impl<std::remove_cv_t<T>, std::remove_cv_t<Cs>...>::value;
    }

    template<typename C>
    static decltype(auto) get_component_ref(Chunk& chunk, std::uint32_t row, std::size_t comp_idx) {
        using raw = typename detail::component_access<C>::raw_type;
        void* ptr = chunk.data + chunk.offsets[comp_idx + 1];
        // We need to know the component size. Use sizeof(raw) as fallback.
        return reference_helper<C>(static_cast<std::byte*>(ptr) + row * sizeof(raw));
    }

    template<typename C>
    static decltype(auto) reference_helper(std::byte* ptr) {
        using raw = typename detail::component_access<C>::raw_type;
        if constexpr (detail::component_access<C>::is_const) {
            return static_cast<const raw&>(*reinterpret_cast<raw*>(ptr));
        } else {
            return static_cast<raw&>(*reinterpret_cast<raw*>(ptr));
        }
    }

    bool passes_filters(const Archetype& arch, std::uint32_t chunk_idx, std::uint32_t row) const {
        for (ComponentId cid : filter_added_) {
            std::size_t comp_idx = arch.component_index(cid);
            if (comp_idx == static_cast<std::size_t>(-1)) return false;
            Tick t = storage_->get_added_tick(arch, chunk_idx, comp_idx, row);
            if (t <= last_run_tick_) return false;
        }
        for (ComponentId cid : filter_changed_) {
            std::size_t comp_idx = arch.component_index(cid);
            if (comp_idx == static_cast<std::size_t>(-1)) return false;
            Tick t = storage_->get_changed_tick(arch, chunk_idx, comp_idx, row);
            if (t <= last_run_tick_) return false;
        }
        return true;
    }

    bool passes_filters_task(const Archetype& arch, std::uint32_t chunk_idx, std::uint32_t row,
                             Tick last_run_tick,
                             const std::vector<ComponentId>& added,
                             const std::vector<ComponentId>& changed) const {
        for (ComponentId cid : added) {
            std::size_t comp_idx = arch.component_index(cid);
            if (comp_idx == static_cast<std::size_t>(-1)) return false;
            Tick t = storage_->get_added_tick(arch, chunk_idx, comp_idx, row);
            if (t <= last_run_tick) return false;
        }
        for (ComponentId cid : changed) {
            std::size_t comp_idx = arch.component_index(cid);
            if (comp_idx == static_cast<std::size_t>(-1)) return false;
            Tick t = storage_->get_changed_tick(arch, chunk_idx, comp_idx, row);
            if (t <= last_run_tick) return false;
        }
        return true;
    }

public:
    // Build or refresh the archetype match cache.
    // Called automatically by each(), count(), begin(), end().
    void update_cache() const {
        if (!world_version_ || !storage_) return;
        std::uint64_t current = *world_version_;
        if (cached_version_ == current) return;
        cached_version_ = current;
        matching_archetypes_.clear();

        ComponentId required[] = { detail::component_access<Cs>::id()... };
        constexpr std::size_t req_count = sizeof...(Cs);

        if (req_count == 0) return;

        // Use the per-component archetype index to find candidates.
        // Pick the component with the fewest archetypes as the starting set,
        // then verify the remaining components.
        const std::vector<ArchetypeId>* candidate_set = nullptr;
        std::size_t candidate_idx = 0;
        for (std::size_t r = 0; r < req_count; ++r) {
            const auto* set = storage_->archetypes_with_component(required[r]);
            if (!set) return; // component never registered → no matches
            if (!candidate_set || set->size() < candidate_set->size()) {
                candidate_set = set;
                candidate_idx = r;
            }
        }
        if (!candidate_set) return;

        // For single-component queries, skip the redundant has_component check
        if (req_count == 1) {
            matching_archetypes_.assign(candidate_set->begin(), candidate_set->end());
            return;
        }

        for (ArchetypeId id : *candidate_set) {
            Archetype* arch = storage_->get_archetype(id);
            if (!arch) continue;

            bool matches = true;
            for (std::size_t r = 0; r < req_count; ++r) {
                if (r == candidate_idx) continue; // already know this one matches
                if (!arch->has_component(required[r])) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                matching_archetypes_.push_back(id);
            }
        }
    }

    ArchetypeStorage* storage_ = nullptr;
    ComponentRegistry* registry_ = nullptr;
    std::uint64_t* world_version_ = nullptr;
    mutable std::uint64_t cached_version_ = std::numeric_limits<std::uint64_t>::max();
    mutable std::vector<ArchetypeId> matching_archetypes_;

    Tick last_run_tick_ = 0;
    std::vector<ComponentId> filter_added_;
    std::vector<ComponentId> filter_changed_;
};

} // namespace campello::core
