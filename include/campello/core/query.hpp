#pragma once

#include "types.hpp"
#include "component.hpp"
#include "archetype.hpp"
#include "detail/component_registry.hpp"
#include "detail/thread_pool.hpp"
#include <future>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace campello::core {

// ------------------------------------------------------------------
// Query filter type markers
// ------------------------------------------------------------------
template<component T>
struct Added {};

template<component T>
struct Changed {};

// Removed components are tracked via a world-level event buffer.
template<component T>
struct Removed {};

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

    Query(ArchetypeStorage* storage, ComponentRegistry* registry, std::uint64_t* world_version,
          const std::vector<EntityMeta>* entity_meta = nullptr,
          const std::unordered_map<ComponentId, std::vector<std::pair<Entity, Tick>>>* removed_components = nullptr)
        : storage_(storage), registry_(registry), world_version_(world_version),
          entity_meta_(entity_meta), removed_components_(removed_components) {}

    // Pre-populate matching archetypes from a world-level cache.
    // `archetype_version` must be the current archetype_version_ of the world.
    void set_cached_archetypes(const std::vector<ArchetypeId>& archetypes, std::uint64_t archetype_version) {
        matching_archetypes_ = archetypes;
        cached_version_ = archetype_version;
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

    template<component T>
    Query removed() {
        filter_removed_.push_back(component_type_id<T>());
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
        if (!filter_removed_.empty()) {
            each_removed(std::forward<Func>(func));
            return;
        }
        ComponentId required[] = { detail::component_access<Cs>::id()... };
        const bool has_filters = !filter_added_.empty() || !filter_changed_.empty();

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
                std::byte* bases[sizeof...(Cs)] = {
                    component_base<Cs>(*arch, chunk, comp_indices[index_of<Cs>()])...
                };
                for (std::uint32_t row = 0; row < chunk.count; ++row) {
                    if (has_filters && !passes_filters(*arch, static_cast<std::uint32_t>(chunk_idx), row)) continue;
                    func(deref_at<Cs>(bases[index_of<Cs>()], row)...);
                }
            }
        }
    }

    // Iterate with entity handle: query.each_with_entity([](Entity e, Position& p, Velocity& v){ ... });
    template<typename Func>
    void each_with_entity(Func&& func) {
        update_cache();
        if (!filter_removed_.empty()) {
            each_with_entity_removed(std::forward<Func>(func));
            return;
        }
        ComponentId required[] = { detail::component_access<Cs>::id()... };
        const bool has_filters = !filter_added_.empty() || !filter_changed_.empty();

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
                Entity* entities = reinterpret_cast<Entity*>(chunk.data + arch->offsets[0]);
                std::byte* bases[sizeof...(Cs)] = {
                    component_base<Cs>(*arch, chunk, comp_indices[index_of<Cs>()])...
                };
                for (std::uint32_t row = 0; row < chunk.count; ++row) {
                    if (has_filters && !passes_filters(*arch, static_cast<std::uint32_t>(chunk_idx), row)) continue;
                    func(entities[row], deref_at<Cs>(bases[index_of<Cs>()], row)...);
                }
            }
        }
    }

    // Parallel chunk iteration: each chunk is processed by a different thread.
    // The caller must ensure `func` is thread-safe (no mutable shared state).
    template<typename Func>
    void each_par(detail::ThreadPool& pool, Func&& func) {
        update_cache();
        if (!filter_removed_.empty()) {
            // Removed filters iterate a small per-frame event list; serial dispatch avoids
            // synchronization complexity. The list is already small enough that parallel
            // overhead is unlikely to help.
            each_removed(std::forward<Func>(func));
            return;
        }
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
                Chunk& chunk = *task.chunk;
                Archetype& arch = *task.arch;
                std::byte* bases[sizeof...(Cs)] = {
                    this->component_base<Cs>(arch, chunk, task.comp_indices[this->index_of<Cs>()])...
                };
                const bool has_filters = !added.empty() || !changed.empty();
                for (std::uint32_t row = 0; row < chunk.count; ++row) {
                    if (has_filters && !this->passes_filters_task(arch, task.chunk_idx, row, last_run, added, changed)) continue;
                    func(this->deref_at<Cs>(bases[this->index_of<Cs>()], row)...);
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
                 const std::vector<EntityMeta>* entity_meta,
                 const std::unordered_map<ComponentId, std::vector<std::pair<Entity, Tick>>>* removed_components,
                 const std::vector<ComponentId>* filter_removed,
                 Tick last_run_tick,
                 const std::vector<ComponentId>* filter_added,
                 const std::vector<ComponentId>* filter_changed,
                 std::size_t arch_idx, std::uint32_t chunk_idx, std::uint32_t row)
            : storage_(storage), registry_(registry), archetypes_(archetypes),
              entity_meta_(entity_meta), removed_components_(removed_components),
              filter_removed_(filter_removed),
              last_run_tick_(last_run_tick),
              filter_added_(filter_added), filter_changed_(filter_changed),
              arch_idx_(arch_idx), chunk_idx_(chunk_idx), row_(row) {
            if (filter_removed_ && !filter_removed_->empty()) {
                removed_mode_ = true;
                removed_idx_ = arch_idx;
                find_primary_removed();
                advance_to_valid_removed();
            } else {
                advance_to_valid();
                cache_indices();
            }
        }

        item_type operator*() {
            if (removed_mode_) {
                Archetype* arch = storage_->get_archetype(removed_meta_.archetype);
                Chunk& chunk = arch->chunks[removed_meta_.chunk_index];
                return std::tuple<Cs&...>(get_component_ref<Cs>(*arch, chunk, removed_meta_.row_index, removed_comp_indices_[index_of<Cs>()])...);
            }
            Archetype* arch = current_archetype();
            Chunk& chunk = current_chunk();
            return std::tuple<Cs&...>(get_component_ref<Cs>(*arch, chunk, row_, comp_indices_[index_of<Cs>()])...);
        }

        Iterator& operator++() {
            if (removed_mode_) {
                ++removed_idx_;
                advance_to_valid_removed();
                return *this;
            }
            ++row_;
            advance_to_valid();
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            if (removed_mode_ != other.removed_mode_) return true;
            if (removed_mode_) {
                return removed_idx_ != other.removed_idx_;
            }
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

        static bool passes_filters_at(ArchetypeStorage* storage, const Archetype& arch,
                                       std::uint32_t chunk_idx, std::uint32_t row,
                                       Tick last_run_tick,
                                       const std::vector<ComponentId>& added,
                                       const std::vector<ComponentId>& changed) {
            for (ComponentId cid : added) {
                std::size_t comp_idx = arch.component_index(cid);
                if (comp_idx == static_cast<std::size_t>(-1)) return false;
                Tick t = storage->get_added_tick(arch, chunk_idx, comp_idx, row);
                if (t <= last_run_tick) return false;
            }
            for (ComponentId cid : changed) {
                std::size_t comp_idx = arch.component_index(cid);
                if (comp_idx == static_cast<std::size_t>(-1)) return false;
                Tick t = storage->get_changed_tick(arch, chunk_idx, comp_idx, row);
                if (t <= last_run_tick) return false;
            }
            return true;
        }

        bool passes_filters_current() const {
            Archetype* arch = storage_->get_archetype((*archetypes_)[arch_idx_]);
            if (!arch) return false;
            return passes_filters_at(storage_, *arch, chunk_idx_, row_, last_run_tick_, *filter_added_, *filter_changed_);
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

        void find_primary_removed() {
            primary_removed_ = nullptr;
            primary_cid_ = 0;
            if (!removed_components_ || !filter_removed_) return;
            for (ComponentId cid : *filter_removed_) {
                auto it = removed_components_->find(cid);
                if (it == removed_components_->end() || it->second.empty()) return;
                if (!primary_removed_ || it->second.size() < primary_removed_->size()) {
                    primary_removed_ = &it->second;
                    primary_cid_ = cid;
                }
            }
        }

        bool resolve_removed_entity(Entity entity) {
            if (!entity_meta_ || !storage_) return false;
            std::uint32_t idx = entity_index(entity);
            if (idx >= entity_meta_->size()) return false;
            const EntityMeta& meta = (*entity_meta_)[idx];
            if (meta.archetype == 0) return false;
            Archetype* arch = storage_->get_archetype(meta.archetype);
            if (!arch) return false;
            if (meta.chunk_index >= arch->chunks.size()) return false;
            Chunk& chunk = arch->chunks[meta.chunk_index];
            if (meta.row_index >= chunk.count) return false;
            Entity* entities = reinterpret_cast<Entity*>(chunk.data + arch->offsets[0]);
            if (entities[meta.row_index] != entity) return false;

            ComponentId required[] = { detail::component_access<Cs>::id()... };
            for (std::size_t i = 0; i < sizeof...(Cs); ++i) {
                removed_comp_indices_[i] = arch->component_index(required[i]);
                if (removed_comp_indices_[i] == static_cast<std::size_t>(-1)) return false;
            }
            removed_meta_ = meta;
            return true;
        }

        bool passes_removed_filters_current(Entity entity) const {
            if (!removed_components_) return false;
            for (ComponentId cid : *filter_removed_) {
                if (cid == primary_cid_) continue;
                auto it = removed_components_->find(cid);
                if (it == removed_components_->end()) return false;
                bool found = false;
                for (const auto& [e, t] : it->second) {
                    if (e == entity && t > last_run_tick_) {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            return true;
        }

        void advance_to_valid_removed() {
            if (!primary_removed_) {
                removed_idx_ = 0;
                return;
            }
            while (removed_idx_ < primary_removed_->size()) {
                const auto& [entity, tick] = (*primary_removed_)[removed_idx_];
                if (tick > last_run_tick_ &&
                    resolve_removed_entity(entity) &&
                    passes_removed_filters_current(entity)) {
                    Archetype* arch = storage_->get_archetype(removed_meta_.archetype);
                    if (passes_filters_at(storage_, *arch, removed_meta_.chunk_index, removed_meta_.row_index,
                                          last_run_tick_, *filter_added_, *filter_changed_)) {
                        return;
                    }
                }
                ++removed_idx_;
            }
        }

        Archetype* current_archetype() {
            return storage_->get_archetype((*archetypes_)[arch_idx_]);
        }

        Chunk& current_chunk() {
            Archetype* arch = current_archetype();
            return arch->chunks[chunk_idx_];
        }

        ArchetypeStorage* storage_ = nullptr;
        ComponentRegistry* registry_ = nullptr;
        const std::vector<ArchetypeId>* archetypes_ = nullptr;
        const std::vector<EntityMeta>* entity_meta_ = nullptr;
        const std::unordered_map<ComponentId, std::vector<std::pair<Entity, Tick>>>* removed_components_ = nullptr;
        const std::vector<ComponentId>* filter_removed_ = nullptr;
        Tick last_run_tick_ = 0;
        const std::vector<ComponentId>* filter_added_ = nullptr;
        const std::vector<ComponentId>* filter_changed_ = nullptr;
        std::size_t arch_idx_ = 0;
        std::uint32_t chunk_idx_ = 0;
        std::uint32_t row_ = 0;
        std::size_t comp_indices_[sizeof...(Cs)] = {};

        bool removed_mode_ = false;
        std::size_t removed_idx_ = 0;
        const std::vector<std::pair<Entity, Tick>>* primary_removed_ = nullptr;
        ComponentId primary_cid_ = 0;
        EntityMeta removed_meta_;
        std::size_t removed_comp_indices_[sizeof...(Cs)] = {};
    };

    Iterator begin() {
        update_cache();
        return Iterator(storage_, registry_, &matching_archetypes_,
                        entity_meta_, removed_components_, &filter_removed_,
                        last_run_tick_, &filter_added_, &filter_changed_,
                        0, 0, 0);
    }

    Iterator end() {
        update_cache();
        std::size_t end_idx = matching_archetypes_.size();
        if (!filter_removed_.empty() && removed_components_) {
            ComponentId primary_cid = 0;
            const auto* primary = primary_removed_list(primary_cid);
            if (primary) end_idx = primary->size();
        }
        return Iterator(storage_, registry_, &matching_archetypes_,
                        entity_meta_, removed_components_, &filter_removed_,
                        last_run_tick_, &filter_added_, &filter_changed_,
                        end_idx, 0, 0);
    }

    std::size_t count() const {
        update_cache();
        if (!filter_removed_.empty()) {
            return count_removed();
        }
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
    static std::byte* component_base(Archetype& arch, Chunk& chunk, std::size_t comp_idx) {
        return chunk.data + arch.offsets[comp_idx + 1];
    }

    template<typename C>
    static decltype(auto) deref_at(std::byte* base, std::uint32_t row) {
        using raw = typename detail::component_access<C>::raw_type;
        return reference_helper<C>(base + row * sizeof(raw));
    }

    template<typename C>
    static decltype(auto) get_component_ref(Archetype& arch, Chunk& chunk, std::uint32_t row, std::size_t comp_idx) {
        return deref_at<C>(component_base<C>(arch, chunk, comp_idx), row);
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

    // --- Removed-component filter implementation ---

    bool is_alive_entity(Entity e) const {
        if (!entity_meta_ || !storage_) return false;
        std::uint32_t idx = entity_index(e);
        if (idx >= entity_meta_->size()) return false;
        const EntityMeta& meta = (*entity_meta_)[idx];
        if (meta.archetype == 0) return false;
        Archetype* arch = storage_->get_archetype(meta.archetype);
        if (!arch) return false;
        if (meta.chunk_index >= arch->chunks.size()) return false;
        Chunk& chunk = arch->chunks[meta.chunk_index];
        if (meta.row_index >= chunk.count) return false;
        Entity* entities = reinterpret_cast<Entity*>(chunk.data + arch->offsets[0]);
        return entities[meta.row_index] == e;
    }

    const std::vector<std::pair<Entity, Tick>>* primary_removed_list(ComponentId& out_cid) const {
        const std::vector<std::pair<Entity, Tick>>* primary = nullptr;
        out_cid = 0;
        if (!removed_components_) return nullptr;
        for (ComponentId cid : filter_removed_) {
            auto it = removed_components_->find(cid);
            if (it == removed_components_->end() || it->second.empty()) return nullptr;
            if (!primary || it->second.size() < primary->size()) {
                primary = &it->second;
                out_cid = cid;
            }
        }
        return primary;
    }

    bool passes_removed_filters(Entity entity, ComponentId primary_cid) const {
        if (!removed_components_) return false;
        for (ComponentId cid : filter_removed_) {
            if (cid == primary_cid) continue;
            auto it = removed_components_->find(cid);
            if (it == removed_components_->end()) return false;
            bool found = false;
            for (const auto& [e, t] : it->second) {
                if (e == entity && t > last_run_tick_) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }

    bool resolve_entity_components(Entity entity, EntityMeta& out_meta, std::size_t* comp_indices) const {
        if (!entity_meta_ || !storage_) return false;
        std::uint32_t idx = entity_index(entity);
        if (idx >= entity_meta_->size()) return false;
        const EntityMeta& meta = (*entity_meta_)[idx];
        if (meta.archetype == 0) return false;
        Archetype* arch = storage_->get_archetype(meta.archetype);
        if (!arch) return false;
        if (meta.chunk_index >= arch->chunks.size()) return false;
        Chunk& chunk = arch->chunks[meta.chunk_index];
        if (meta.row_index >= chunk.count) return false;
        Entity* entities = reinterpret_cast<Entity*>(chunk.data + arch->offsets[0]);
        if (entities[meta.row_index] != entity) return false;

        ComponentId required[] = { detail::component_access<Cs>::id()... };
        for (std::size_t i = 0; i < sizeof...(Cs); ++i) {
            comp_indices[i] = arch->component_index(required[i]);
            if (comp_indices[i] == static_cast<std::size_t>(-1)) return false;
        }
        out_meta = meta;
        return true;
    }

    template<typename Func>
    void each_removed(Func&& func) const {
        ComponentId primary_cid = 0;
        const auto* primary = primary_removed_list(primary_cid);
        if (!primary) return;

        std::size_t comp_indices[sizeof...(Cs)];
        for (const auto& [entity, tick] : *primary) {
            if (tick <= last_run_tick_) continue;
            EntityMeta meta;
            if (!resolve_entity_components(entity, meta, comp_indices)) continue;
            if (!passes_removed_filters(entity, primary_cid)) continue;
            Archetype* arch = storage_->get_archetype(meta.archetype);
            Chunk& chunk = arch->chunks[meta.chunk_index];
            if (!passes_filters(*arch, meta.chunk_index, meta.row_index)) continue;
            std::byte* bases[sizeof...(Cs)] = {
                component_base<Cs>(*arch, chunk, comp_indices[index_of<Cs>()])...
            };
            func(deref_at<Cs>(bases[index_of<Cs>()], meta.row_index)...);
        }
    }

    template<typename Func>
    void each_with_entity_removed(Func&& func) const {
        ComponentId primary_cid = 0;
        const auto* primary = primary_removed_list(primary_cid);
        if (!primary) return;

        std::size_t comp_indices[sizeof...(Cs)];
        for (const auto& [entity, tick] : *primary) {
            if (tick <= last_run_tick_) continue;
            EntityMeta meta;
            if (!resolve_entity_components(entity, meta, comp_indices)) continue;
            if (!passes_removed_filters(entity, primary_cid)) continue;
            Archetype* arch = storage_->get_archetype(meta.archetype);
            Chunk& chunk = arch->chunks[meta.chunk_index];
            if (!passes_filters(*arch, meta.chunk_index, meta.row_index)) continue;
            std::byte* bases[sizeof...(Cs)] = {
                component_base<Cs>(*arch, chunk, comp_indices[index_of<Cs>()])...
            };
            func(entity, deref_at<Cs>(bases[index_of<Cs>()], meta.row_index)...);
        }
    }

    std::size_t count_removed() const {
        ComponentId primary_cid = 0;
        const auto* primary = primary_removed_list(primary_cid);
        if (!primary) return 0;

        std::size_t total = 0;
        std::size_t comp_indices[sizeof...(Cs)];
        for (const auto& [entity, tick] : *primary) {
            if (tick <= last_run_tick_) continue;
            EntityMeta meta;
            if (!resolve_entity_components(entity, meta, comp_indices)) continue;
            if (!passes_removed_filters(entity, primary_cid)) continue;
            Archetype* arch = storage_->get_archetype(meta.archetype);
            if (!passes_filters(*arch, meta.chunk_index, meta.row_index)) continue;
            ++total;
        }
        return total;
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

        if constexpr (req_count == 0) return;

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
        if constexpr (req_count == 1) {
            matching_archetypes_.assign(candidate_set->begin(), candidate_set->end());
        } else {
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
    }

    ArchetypeStorage* storage_ = nullptr;
    ComponentRegistry* registry_ = nullptr;
    std::uint64_t* world_version_ = nullptr;
    const std::vector<EntityMeta>* entity_meta_ = nullptr;
    const std::unordered_map<ComponentId, std::vector<std::pair<Entity, Tick>>>* removed_components_ = nullptr;
    mutable std::uint64_t cached_version_ = std::numeric_limits<std::uint64_t>::max();
    mutable std::vector<ArchetypeId> matching_archetypes_;

    Tick last_run_tick_ = 0;
    std::vector<ComponentId> filter_added_;
    std::vector<ComponentId> filter_changed_;
    std::vector<ComponentId> filter_removed_;
};

} // namespace campello::core
