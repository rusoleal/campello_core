#pragma once

#include "types.hpp"
#include "component.hpp"
#include "archetype.hpp"
#include "commands.hpp"
#include "event.hpp"
#include "resource.hpp"
#include "query.hpp"
#include "reflect.hpp"
#include "detail/component_registry.hpp"
#include "detail/sparse_set.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace campello::core {

// ------------------------------------------------------------------
// Hierarchy components (built-in)
// ------------------------------------------------------------------
struct Parent {
    Entity entity = null_entity;
};

struct Children {
    std::vector<Entity> entities;
};

// ------------------------------------------------------------------
// EntityAllocator: manages entity indices and generations
// ------------------------------------------------------------------
class EntityAllocator {
public:
    Entity spawn() {
        if (!free_indices_.empty()) {
            std::uint32_t idx = free_indices_.back();
            free_indices_.pop_back();
            std::uint32_t gen = generations_[idx];
            return make_entity(idx, gen);
        }
        std::uint32_t idx = next_index_++;
        if (idx >= generations_.size()) {
            generations_.resize(idx + 1, 0);
        }
        return make_entity(idx, generations_[idx]);
    }

    bool is_alive(Entity e) const noexcept {
        std::uint32_t idx = entity_index(e);
        std::uint32_t gen = entity_generation(e);
        return idx < generations_.size() && generations_[idx] == gen;
    }

    void despawn(Entity e) {
        std::uint32_t idx = entity_index(e);
        if (idx < generations_.size()) {
            generations_[idx]++;
            free_indices_.push_back(idx);
        }
    }

private:
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint32_t> free_indices_;
    std::uint32_t next_index_ = 0;
};

// ------------------------------------------------------------------
// World: top-level ECS container
// ------------------------------------------------------------------
class World {
public:
    World() {
        // Register built-in hierarchy components
        register_component<Parent>();
        register_component<Children>();
    }

    ~World() { clear(); }

    // --- Entity lifecycle ---

    Entity spawn() {
        Entity e = entity_allocator_.spawn();
        std::uint32_t idx = entity_index(e);
        if (idx >= entity_meta_.size()) {
            entity_meta_.resize(idx + 1);
        }
        entity_meta_[idx] = {0, 0, 0}; // no archetype yet
        return e;
    }

    template<typename... Cs>
    Entity spawn_with(Cs&&... components) {
        Entity e = spawn();
        (insert<std::remove_cv_t<std::remove_reference_t<Cs>>>(e, std::forward<Cs>(components)), ...);
        return e;
    }

    template<typename... Cs>
    std::vector<Entity> spawn_many(std::size_t count, const Cs&... components) {
        (register_component<std::remove_cv_t<Cs>>(), ...);

        std::vector<ComponentId> cids = {component_type_id<std::remove_cv_t<Cs>>()...};
        std::sort(cids.begin(), cids.end());
        cids.erase(std::unique(cids.begin(), cids.end()), cids.end());

        ArchetypeId arch = archetype_storage_.find_or_create_archetype(cids);
        archetype_version_++;
        Archetype* arch_ptr = archetype_storage_.get_archetype(arch);

        std::vector<Entity> entities;
        entities.reserve(count);

        Tick tick = change_tick_;
        for (std::size_t i = 0; i < count; ++i) {
            Entity e = entity_allocator_.spawn();
            std::uint32_t idx = entity_index(e);
            if (idx >= entity_meta_.size()) entity_meta_.resize(idx + 1);

            auto [chunk_idx, row] = archetype_storage_.allocate_entity(e, arch, component_registry_, tick);
            entity_meta_[idx] = {arch, chunk_idx, row};

            // Overwrite default-constructed components with provided values
            ([&]<typename C>(const C& comp) {
                std::size_t ci = arch_ptr->component_index(component_type_id<std::remove_cv_t<C>>());
                void* ptr = archetype_storage_.get_component(*arch_ptr, chunk_idx, row, ci, component_registry_);
                const auto* info = component_registry_.info(component_type_id<std::remove_cv_t<C>>());
                if (info && info->destruct) info->destruct(ptr);
                new (ptr) std::remove_cv_t<C>(comp);
            }(components), ...);

            entities.push_back(e);
        }

        return entities;
    }

    void despawn_many(const std::vector<Entity>& entities) {
        for (Entity e : entities) {
            despawn(e);
        }
    }

    template<component T>
    void insert_many(const std::vector<Entity>& entities, const T& value) {
        for (Entity e : entities) {
            insert<T>(e, value);
        }
    }

    void despawn(Entity entity) {
        if (!is_alive(entity)) return;

        // Cascade despawn children
        if (auto* children = get<Children>(entity)) {
            auto child_list = children->entities; // copy before mutation
            for (Entity child : child_list) {
                despawn(child);
            }
        }

        // Remove from parent's children list
        if (auto* parent = get<Parent>(entity)) {
            if (is_alive(parent->entity)) {
                if (auto* parent_children = get<Children>(parent->entity)) {
                    auto it = std::find(parent_children->entities.begin(),
                                        parent_children->entities.end(), entity);
                    if (it != parent_children->entities.end()) {
                        parent_children->entities.erase(it);
                    }
                }
            }
        }

        // Call on_remove hooks for all components before removal
        call_on_remove_hooks(entity);

        // Remove from archetype
        std::uint32_t idx = entity_index(entity);
        EntityMeta& meta = entity_meta_[idx];
        if (meta.archetype != 0) {
            Entity swapped = archetype_storage_.remove_entity(meta.archetype, meta.chunk_index,
                                                              meta.row_index, component_registry_);
            if (swapped != null_entity) {
                std::uint32_t swapped_idx = entity_index(swapped);
                entity_meta_[swapped_idx].chunk_index = meta.chunk_index;
                entity_meta_[swapped_idx].row_index = meta.row_index;
            }
        }

        entity_allocator_.despawn(entity);
        meta = {0, 0, 0};
    }

    bool is_alive(Entity entity) const noexcept {
        return entity_allocator_.is_alive(entity);
    }

    // --- Component mutation ---

    template<component T, typename... Args>
    T& insert(Entity entity, Args&&... args) {
        assert(is_alive(entity) && "insert<T> called on dead or invalid entity");
        if (!is_alive(entity)) {
            static T dummy{};
            return dummy;
        }

        register_component<T>();

        std::uint32_t idx = entity_index(entity);
        EntityMeta& meta = entity_meta_[idx];
        ComponentId cid = component_type_id<T>();

        if (meta.archetype != 0) {
            Archetype* arch = archetype_storage_.get_archetype(meta.archetype);
            if (arch && arch->has_component(cid)) {
                // Already has component — return existing
                void* ptr = archetype_storage_.get_component(*arch, meta.chunk_index,
                                                             meta.row_index,
                                                             arch->component_index(cid),
                                                             component_registry_);
                return *static_cast<T*>(ptr);
            }
        }

        // Build new archetype component list
        std::vector<ComponentId> new_components;
        if (meta.archetype != 0) {
            Archetype* arch = archetype_storage_.get_archetype(meta.archetype);
            if (arch) new_components = arch->components;
        }
        new_components.push_back(cid);
        std::sort(new_components.begin(), new_components.end());
        new_components.erase(std::unique(new_components.begin(), new_components.end()),
                             new_components.end());

        ArchetypeId new_arch = archetype_storage_.find_or_create_archetype(new_components);
        archetype_version_++;

        if (meta.archetype == 0) {
            // First component — allocate directly
            auto [new_chunk, new_row] = archetype_storage_.allocate_entity(
                entity, new_arch, component_registry_, change_tick_);
            meta.archetype = new_arch;
            meta.chunk_index = new_chunk;
            meta.row_index = new_row;
        } else {
            // Move to new archetype
            auto [new_chunk, new_row, swapped] = archetype_storage_.move_entity(
                entity, meta.archetype, meta.chunk_index, meta.row_index,
                new_arch, component_registry_, change_tick_);
            if (swapped != null_entity) {
                std::uint32_t swapped_idx = entity_index(swapped);
                entity_meta_[swapped_idx].chunk_index = meta.chunk_index;
                entity_meta_[swapped_idx].row_index = meta.row_index;
            }
            meta.archetype = new_arch;
            meta.chunk_index = new_chunk;
            meta.row_index = new_row;
        }

        // Initialize the new component
        Archetype* arch = archetype_storage_.get_archetype(new_arch);
        std::size_t comp_idx = arch->component_index(cid);
        void* ptr = archetype_storage_.get_component(*arch, meta.chunk_index, meta.row_index,
                                                     comp_idx, component_registry_);
        // If the component was default-constructed during allocate_entity/move_entity,
        // we need to assign over it. For simplicity, destruct then construct.
        const auto* info = component_registry_.info(cid);
        if (info && info->destruct) {
            info->destruct(ptr);
        }
        new (ptr) T(std::forward<Args>(args)...);

        // Call on_add hooks
        auto it = on_add_hooks_.find(cid);
        if (it != on_add_hooks_.end()) {
            for (auto& hook : it->second) {
                hook(entity, ptr);
            }
        }

        return *static_cast<T*>(ptr);
    }

    template<component T>
    void remove(Entity entity) {
        if (!is_alive(entity)) return;

        std::uint32_t idx = entity_index(entity);
        EntityMeta& meta = entity_meta_[idx];
        if (meta.archetype == 0) return;

        ComponentId cid = component_type_id<T>();
        Archetype* arch = archetype_storage_.get_archetype(meta.archetype);
        if (!arch || !arch->has_component(cid)) return;

        // Call on_remove hooks before the component is destroyed
        std::size_t comp_idx = arch->component_index(cid);
        if (comp_idx != static_cast<std::size_t>(-1)) {
            void* ptr = archetype_storage_.get_component(*arch, meta.chunk_index, meta.row_index,
                                                         comp_idx, component_registry_);
            auto it = on_remove_hooks_.find(cid);
            if (it != on_remove_hooks_.end()) {
                for (auto& hook : it->second) {
                    hook(entity, ptr);
                }
            }
        }

        record_component_removal(entity, cid);

        std::vector<ComponentId> new_components = arch->components;
        auto it = std::lower_bound(new_components.begin(), new_components.end(), cid);
        if (it != new_components.end() && *it == cid) {
            new_components.erase(it);
        }

        if (new_components.empty()) {
            // No components left — remove from archetype entirely
            Entity swapped = archetype_storage_.remove_entity(meta.archetype, meta.chunk_index,
                                                              meta.row_index, component_registry_);
            if (swapped != null_entity) {
                std::uint32_t swapped_idx = entity_index(swapped);
                entity_meta_[swapped_idx].chunk_index = meta.chunk_index;
                entity_meta_[swapped_idx].row_index = meta.row_index;
            }
            meta.archetype = 0;
            meta.chunk_index = 0;
            meta.row_index = 0;
            return;
        }

        ArchetypeId new_arch = archetype_storage_.find_or_create_archetype(new_components);
        archetype_version_++;

        auto [new_chunk, new_row, swapped] = archetype_storage_.move_entity(
            entity, meta.archetype, meta.chunk_index, meta.row_index,
            new_arch, component_registry_, change_tick_);
        if (swapped != null_entity) {
            std::uint32_t swapped_idx = entity_index(swapped);
            entity_meta_[swapped_idx].chunk_index = meta.chunk_index;
            entity_meta_[swapped_idx].row_index = meta.row_index;
        }

        meta.archetype = new_arch;
        meta.chunk_index = new_chunk;
        meta.row_index = new_row;
    }

    template<component T>
    T* get(Entity entity) {
        if (!is_alive(entity)) return nullptr;
        std::uint32_t idx = entity_index(entity);
        EntityMeta& meta = entity_meta_[idx];
        if (meta.archetype == 0) return nullptr;

        ComponentId cid = component_type_id<T>();
        Archetype* arch = archetype_storage_.get_archetype(meta.archetype);
        if (!arch || !arch->has_component(cid)) return nullptr;

        std::size_t comp_idx = arch->component_index(cid);
        void* ptr = archetype_storage_.get_component(*arch, meta.chunk_index, meta.row_index,
                                                     comp_idx, component_registry_);
        return static_cast<T*>(ptr);
    }

    template<component T>
    const T* get(Entity entity) const {
        return const_cast<World*>(this)->get<T>(entity);
    }

    template<component T>
    bool has(Entity entity) const noexcept {
        if (!is_alive(entity)) return false;
        std::uint32_t idx = entity_index(entity);
        const EntityMeta& meta = entity_meta_[idx];
        if (meta.archetype == 0) return false;

        ComponentId cid = component_type_id<T>();
        const Archetype* arch = archetype_storage_.get_archetype(meta.archetype);
        return arch && arch->has_component(cid);
    }

    // --- Query ---

    template<typename... Cs>
    Query<Cs...> query() {
        auto q = Query<Cs...>(&archetype_storage_, &component_registry_, &archetype_version_);
        q.with_last_run_tick(change_tick_);
        return q;
    }

    // --- Prefabs / Cloning ---

    // Shallow clone: create a new entity with copies of all components from `source`.
    // on_add hooks fire after all components are copied.
    // Hierarchy components (Parent/Children) are copied verbatim — caller must
    // reparent if a true subtree clone is desired.
    Entity clone(Entity source) {
        if (!is_alive(source)) return null_entity;

        std::uint32_t src_idx = entity_index(source);
        EntityMeta src_meta = entity_meta_[src_idx]; // copy — spawn() may reallocate vector

        Entity clone_e = spawn();
        if (src_meta.archetype == 0) {
            return clone_e; // source has no components
        }

        // Allocate clone directly into the source archetype
        auto [chunk_idx, row] = archetype_storage_.allocate_entity(
            clone_e, src_meta.archetype, component_registry_, change_tick_);
        entity_meta_[entity_index(clone_e)] = {src_meta.archetype, chunk_idx, row};

        // Copy components and collect hooks
        Archetype* arch = archetype_storage_.get_archetype(src_meta.archetype);
        if (arch) {
            for (std::size_t i = 0; i < arch->components.size(); ++i) {
                ComponentId cid = arch->components[i];
                const auto* info = component_registry_.info(cid);

                void* src_ptr = archetype_storage_.get_component(
                    *arch, src_meta.chunk_index, src_meta.row_index, i, component_registry_);
                void* dst_ptr = archetype_storage_.get_component(
                    *arch, chunk_idx, row, i, component_registry_);

                // allocate_entity default-constructed the component.
                // Destruct it before copying the source value.
                if (info && info->destruct) info->destruct(dst_ptr);
                if (info && info->copy) info->copy(dst_ptr, src_ptr);
            }

            // Fire on_add hooks after all components are present
            for (std::size_t i = 0; i < arch->components.size(); ++i) {
                ComponentId cid = arch->components[i];
                void* dst_ptr = archetype_storage_.get_component(
                    *arch, chunk_idx, row, i, component_registry_);
                auto it = on_add_hooks_.find(cid);
                if (it != on_add_hooks_.end()) {
                    for (auto& hook : it->second) {
                        hook(clone_e, dst_ptr);
                    }
                }
            }
        }

        return clone_e;
    }

    // Bulk clone: spawn `count` copies of `source` efficiently.
    // All clones share the same archetype, so allocation is batched at the chunk level.
    std::vector<Entity> clone_many(Entity source, std::size_t count) {
        std::vector<Entity> clones;
        clones.reserve(count);
        if (!is_alive(source) || count == 0) return clones;

        std::uint32_t src_idx = entity_index(source);
        EntityMeta src_meta = entity_meta_[src_idx]; // copy — spawn() may reallocate vector

        if (src_meta.archetype == 0) {
            // Source has no components — just spawn empty entities
            for (std::size_t i = 0; i < count; ++i) {
                clones.push_back(spawn());
            }
            return clones;
        }

        Archetype* arch = archetype_storage_.get_archetype(src_meta.archetype);
        if (!arch) return clones;

        // Batch allocate all clones into the same archetype
        for (std::size_t i = 0; i < count; ++i) {
            Entity clone_e = spawn();
            auto [chunk_idx, row] = archetype_storage_.allocate_entity(
                clone_e, src_meta.archetype, component_registry_, change_tick_);
            entity_meta_[entity_index(clone_e)] = {src_meta.archetype, chunk_idx, row};
            clones.push_back(clone_e);
        }

        // Copy components for all clones
        for (Entity clone_e : clones) {
            std::uint32_t dst_idx = entity_index(clone_e);
            const EntityMeta& dst_meta = entity_meta_[dst_idx];

            for (std::size_t i = 0; i < arch->components.size(); ++i) {
                ComponentId cid = arch->components[i];
                const auto* info = component_registry_.info(cid);

                void* src_ptr = archetype_storage_.get_component(
                    *arch, src_meta.chunk_index, src_meta.row_index, i, component_registry_);
                void* dst_ptr = archetype_storage_.get_component(
                    *arch, dst_meta.chunk_index, dst_meta.row_index, i, component_registry_);

                if (info && info->destruct) info->destruct(dst_ptr);
                if (info && info->copy) info->copy(dst_ptr, src_ptr);
            }
        }

        // Fire on_add hooks for all clones (after all components are copied)
        for (Entity clone_e : clones) {
            std::uint32_t dst_idx = entity_index(clone_e);
            const EntityMeta& dst_meta = entity_meta_[dst_idx];

            for (std::size_t i = 0; i < arch->components.size(); ++i) {
                ComponentId cid = arch->components[i];
                void* dst_ptr = archetype_storage_.get_component(
                    *arch, dst_meta.chunk_index, dst_meta.row_index, i, component_registry_);
                auto it = on_add_hooks_.find(cid);
                if (it != on_add_hooks_.end()) {
                    for (auto& hook : it->second) {
                        hook(clone_e, dst_ptr);
                    }
                }
            }
        }

        return clones;
    }

    // --- Events ---

    template<typename E>
    void send(E&& event) {
        event_storage_.send<E>(std::forward<E>(event));
    }

    template<typename E, typename... Args>
    void emit(Args&&... args) {
        event_storage_.send<E>(E{std::forward<Args>(args)...});
    }

    template<typename E>
    EventReader<E> event_reader() const {
        return EventReader<E>(event_storage_);
    }

    template<typename E>
    EventWriter<E> event_writer() {
        return EventWriter<E>(event_storage_);
    }

    void clear_events() {
        event_storage_.clear_all();
    }

    // --- Resources ---

    template<typename T, typename... Args>
    T& init_resource(Args&&... args) {
        return resource_storage_.emplace<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    T& resource() {
        T* ptr = resource_storage_.get<T>();
        if (!ptr) {
            ptr = &resource_storage_.emplace<T>();
        }
        return *ptr;
    }

    template<typename T>
    const T& resource() const {
        const T* ptr = resource_storage_.get<T>();
        assert(ptr && "resource<T>() const called before resource was initialized");
        return *ptr;
    }

    template<typename T>
    Res<T> res() {
        return Res<T>(resource_storage_.get<T>(), &resource_storage_);
    }

    template<typename T>
    ResMut<T> res_mut() {
        return ResMut<T>(resource_storage_.get<T>(), &resource_storage_);
    }

    // --- Commands ---

    CommandBuffer& commands() {
        return command_buffer_;
    }

    void apply_commands() {
        command_buffer_.apply(*this);
    }

    // --- Component registration ---

    template<component T>
    void register_component() {
        component_registry_.register_component<T>();

        ComponentBuilder builder(ComponentTraits<T>::name);
        builder.size(sizeof(T)).alignment(alignof(T))
                .relocatable(ComponentTraits<T>::trivially_relocatable);
        ComponentTraits<T>::reflect(builder);
        reflect_registry_.register_info(component_type_id<T>(), builder.build());
    }

    const ComponentRegistry& components() const {
        return component_registry_;
    }

    const ReflectRegistry& reflect_registry() const {
        return reflect_registry_;
    }

    // Visit all components of an entity
    template<typename Func>
    void visit(Entity entity, Func&& func) {
        if (!is_alive(entity)) return;
        std::uint32_t idx = entity_index(entity);
        EntityMeta& meta = entity_meta_[idx];
        if (meta.archetype == 0) return;

        Archetype* arch = archetype_storage_.get_archetype(meta.archetype);
        if (!arch) return;

        for (std::size_t i = 0; i < arch->components.size(); ++i) {
            ComponentId cid = arch->components[i];
            void* ptr = archetype_storage_.get_component(*arch, meta.chunk_index,
                                                         meta.row_index, i,
                                                         component_registry_);
            const ComponentInfo* info = reflect_registry_.info(cid);
            func(cid, ptr, info);
        }
    }

    // --- Component hooks ---

    template<component T>
    void on_add(std::function<void(Entity, T&)> callback) {
        ComponentId cid = component_type_id<T>();
        on_add_hooks_[cid].push_back([cb = std::move(callback)](Entity e, void* ptr) {
            cb(e, *static_cast<T*>(ptr));
        });
    }

    template<component T>
    void on_remove(std::function<void(Entity, T&)> callback) {
        ComponentId cid = component_type_id<T>();
        on_remove_hooks_[cid].push_back([cb = std::move(callback)](Entity e, void* ptr) {
            cb(e, *static_cast<T*>(ptr));
        });
    }

    // --- Change detection ---

    Tick change_tick() const { return change_tick_; }

    void increment_change_tick() {
        ++change_tick_;
        clear_removed();
    }

    void clear_removed() {
        removed_components_.clear();
    }

    template<component T>
    const std::vector<Entity>& removed() const {
        auto it = removed_components_.find(component_type_id<T>());
        if (it == removed_components_.end()) {
            static const std::vector<Entity> empty;
            return empty;
        }
        return it->second;
    }

    // --- Snapshots ---

    // Serialize all entities and components to JSON.
    // Entity IDs are not preserved across restore (new IDs are assigned).
    // Entity references inside components (e.g. Parent) become stale and
    // must be remapped by the caller.
    std::string snapshot() const;

    // Clear the world and restore from a JSON snapshot.
    void restore(std::string_view json);

    // Remove all entities and reset world state.
    // Calls component destructors before freeing memory.
    void clear();

    // --- Memory diagnostics ---

    struct MemoryStats {
        std::size_t chunk_data_bytes = 0;    // SoA chunk allocations
        std::size_t chunk_ticks_bytes = 0;   // Change-detection tick arrays
        std::size_t entity_meta_bytes = 0;   // entity_meta_ vector
        std::size_t archetype_count = 0;
        std::size_t chunk_count = 0;
        std::size_t alive_entities = 0;
        std::size_t total_capacity = 0;      // sum of chunk capacities

        std::size_t total_bytes() const noexcept {
            return chunk_data_bytes + chunk_ticks_bytes + entity_meta_bytes;
        }
        double bytes_per_entity() const noexcept {
            return alive_entities ? static_cast<double>(total_bytes()) / alive_entities : 0.0;
        }
        double chunk_utilization() const noexcept {
            return total_capacity ? static_cast<double>(alive_entities) / total_capacity : 0.0;
        }
    };

    MemoryStats memory_stats() const;

    template<component T>
    void mark_changed(Entity entity) {
        if (!is_alive(entity)) return;
        std::uint32_t idx = entity_index(entity);
        EntityMeta& meta = entity_meta_[idx];
        if (meta.archetype == 0) return;

        ComponentId cid = component_type_id<T>();
        Archetype* arch = archetype_storage_.get_archetype(meta.archetype);
        if (!arch || !arch->has_component(cid)) return;

        std::size_t comp_idx = arch->component_index(cid);
        archetype_storage_.set_changed_tick(*arch, meta.chunk_index, comp_idx, meta.row_index, change_tick_);
    }

    // --- Hierarchy helpers ---

    bool is_descendant_of(Entity possible_descendant, Entity possible_ancestor) const {
        Entity current = possible_descendant;
        while (current != null_entity) {
            auto* p = get<Parent>(current);
            if (!p) break;
            current = p->entity;
            if (current == possible_ancestor) return true;
            // Safety limit to prevent infinite loops in corrupted hierarchies
            if (current == possible_descendant) break;
        }
        return false;
    }

    void set_parent(Entity child, Entity parent) {
        if (!is_alive(child) || !is_alive(parent)) return;
        if (child == parent) return;
        if (is_descendant_of(parent, child)) return; // would create cycle

        // Remove from old parent
        if (auto* old_parent = get<Parent>(child)) {
            if (is_alive(old_parent->entity) && old_parent->entity != parent) {
                if (auto* old_children = get<Children>(old_parent->entity)) {
                    auto it = std::find(old_children->entities.begin(),
                                        old_children->entities.end(), child);
                    if (it != old_children->entities.end()) {
                        old_children->entities.erase(it);
                    }
                }
            }
        }

        // Update or insert Parent component
        if (auto* p = get<Parent>(child)) {
            p->entity = parent;
        } else {
            insert<Parent>(child, Parent{parent});
        }

        auto* children = get<Children>(parent);
        if (!children) {
            insert<Children>(parent, Children{});
            children = get<Children>(parent);
        }
        if (std::find(children->entities.begin(), children->entities.end(), child)
            == children->entities.end()) {
            children->entities.push_back(child);
        }
    }

    void remove_parent(Entity child) {
        if (!is_alive(child)) return;
        auto* parent = get<Parent>(child);
        if (!parent) return;

        if (is_alive(parent->entity)) {
            if (auto* children = get<Children>(parent->entity)) {
                auto it = std::find(children->entities.begin(),
                                    children->entities.end(), child);
                if (it != children->entities.end()) {
                    children->entities.erase(it);
                }
            }
        }
        remove<Parent>(child);
    }

    // --- Internal ---
    ArchetypeStorage& archetype_storage() { return archetype_storage_; }
    const ArchetypeStorage& archetype_storage() const { return archetype_storage_; }
    ComponentRegistry& component_registry() { return component_registry_; }

private:
    EntityAllocator entity_allocator_;
    std::vector<EntityMeta> entity_meta_;
    ArchetypeStorage archetype_storage_;
    ComponentRegistry component_registry_;
    EventStorage event_storage_;
    ResourceStorage resource_storage_;
    CommandBuffer command_buffer_;
    ReflectRegistry reflect_registry_;
    std::uint64_t archetype_version_ = 0;
    Tick change_tick_ = 1;

    using HookCallback = std::function<void(Entity, void*)>;
    std::unordered_map<ComponentId, std::vector<HookCallback>> on_add_hooks_;
    std::unordered_map<ComponentId, std::vector<HookCallback>> on_remove_hooks_;
    std::unordered_map<ComponentId, std::vector<Entity>> removed_components_;

    void record_component_removal(Entity entity, ComponentId cid) {
        removed_components_[cid].push_back(entity);
    }

    void type_erased_insert(Entity entity, ComponentId cid, const void* data);

    void call_on_remove_hooks(Entity entity) {
        if (!is_alive(entity)) return;
        std::uint32_t idx = entity_index(entity);
        const EntityMeta& meta = entity_meta_[idx];
        if (meta.archetype == 0) return;

        Archetype* arch = archetype_storage_.get_archetype(meta.archetype);
        if (!arch) return;

        for (std::size_t i = 0; i < arch->components.size(); ++i) {
            ComponentId cid = arch->components[i];
            record_component_removal(entity, cid);
            auto it = on_remove_hooks_.find(cid);
            if (it != on_remove_hooks_.end()) {
                void* ptr = archetype_storage_.get_component(*arch, meta.chunk_index, meta.row_index,
                                                             i, component_registry_);
                for (auto& hook : it->second) {
                    hook(entity, ptr);
                }
            }
        }
    }
};

// ------------------------------------------------------------------
// World inline implementations
// ------------------------------------------------------------------

inline World::MemoryStats World::memory_stats() const {
    MemoryStats stats;
    stats.entity_meta_bytes = entity_meta_.size() * sizeof(EntityMeta);
    stats.alive_entities = 0;

    for (std::size_t arch_i = 0; arch_i < archetype_storage_.archetype_count(); ++arch_i) {
        ArchetypeId arch_id = static_cast<ArchetypeId>(arch_i + 1);
        const Archetype* arch = archetype_storage_.get_archetype(arch_id);
        if (!arch) continue;

        ++stats.archetype_count;
        stats.alive_entities += arch->entity_count;

        for (std::size_t chunk_idx = 0; chunk_idx < arch->chunks.size(); ++chunk_idx) {
            const Chunk& chunk = arch->chunks[chunk_idx];
            ++stats.chunk_count;
            stats.chunk_data_bytes += chunk.total_size;
            stats.total_capacity += chunk.capacity;

            if (chunk_idx < arch->chunk_ticks.size()) {
                const ChunkTicks& ticks = arch->chunk_ticks[chunk_idx];
                for (const auto& vec : ticks.added) {
                    stats.chunk_ticks_bytes += vec.capacity() * sizeof(Tick);
                }
                for (const auto& vec : ticks.changed) {
                    stats.chunk_ticks_bytes += vec.capacity() * sizeof(Tick);
                }
            }
        }
    }

    return stats;
}

// ------------------------------------------------------------------
// CommandBuffer template definitions (require World to be complete)
// ------------------------------------------------------------------

inline void CommandBuffer::spawn() {
    push([](World& world) {
        world.spawn();
    });
}

inline void CommandBuffer::despawn(Entity entity) {
    push([entity](World& world) {
        world.despawn(entity);
    });
}

template<typename... Cs>
void CommandBuffer::spawn_with(Cs&&... components) {
    push([...cs = std::forward<Cs>(components)](World& world) mutable {
        Entity e = world.spawn();
        (world.insert<std::remove_cv_t<std::remove_reference_t<Cs>>>(e, std::move(cs)), ...);
    });
}

template<component T, typename... Args>
void CommandBuffer::insert(Entity entity, Args&&... args) {
    push([entity, ...args = std::forward<Args>(args)](World& world) mutable {
        world.insert<T>(entity, std::forward<Args>(args)...);
    });
}

template<component T>
void CommandBuffer::remove(Entity entity) {
    push([entity](World& world) {
        world.remove<T>(entity);
    });
}

inline void CommandBuffer::apply(World& world) {
    for (auto& cmd : commands_) {
        cmd->execute(world);
    }
    commands_.clear();
}

} // namespace campello::core
