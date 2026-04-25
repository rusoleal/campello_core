#pragma once

#include "types.hpp"
#include "component.hpp"
#include "detail/allocator.hpp"
#include "detail/type_info.hpp"
#include "detail/component_registry.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace campello::core {

// ------------------------------------------------------------------
// Chunk: contiguous block of entities with the same component set
// ------------------------------------------------------------------
struct Chunk {
    std::byte* data = nullptr;
    std::uint32_t count = 0;
    std::uint32_t capacity = 0;
    std::size_t total_size = 0; // allocated bytes

    // offsets[i] is the byte offset of component (i-1)'s array within data
    // offsets[0] is always the entity array
    std::vector<std::size_t> offsets;

    Chunk() = default;
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    Chunk(Chunk&& other) noexcept
        : data(other.data)
        , count(other.count)
        , capacity(other.capacity)
        , total_size(other.total_size)
        , offsets(std::move(other.offsets)) {
        other.data = nullptr;
        other.count = 0;
        other.capacity = 0;
    }

    Chunk& operator=(Chunk&& other) noexcept {
        if (this != &other) {
            data = other.data;
            count = other.count;
            capacity = other.capacity;
            total_size = other.total_size;
            offsets = std::move(other.offsets);
            other.data = nullptr;
            other.count = 0;
            other.capacity = 0;
        }
        return *this;
    }

    ~Chunk() {
        // data is owned by ArenaAllocator, not freed here
    }
};

// ------------------------------------------------------------------
// ChunkTicks: parallel tick storage for change detection
// ------------------------------------------------------------------
struct ChunkTicks {
    // added[component_index][row] = tick when component was added
    // changed[component_index][row] = tick when component was last changed
    std::vector<std::vector<Tick>> added;
    std::vector<std::vector<Tick>> changed;
};

// ------------------------------------------------------------------
// Archetype: defines a set of components and holds chunks
// ------------------------------------------------------------------
struct Archetype {
    ArchetypeId id = 0;
    std::vector<ComponentId> components; // sorted
    std::vector<Chunk> chunks;
    std::vector<ChunkTicks> chunk_ticks;
    std::uint32_t entity_count = 0;

    // Edge cache for fast add/remove transitions
    std::unordered_map<ComponentId, ArchetypeId> add_edges;
    std::unordered_map<ComponentId, ArchetypeId> remove_edges;

    bool has_component(ComponentId cid) const noexcept {
        return std::binary_search(components.begin(), components.end(), cid);
    }

    std::size_t component_index(ComponentId cid) const {
        auto it = std::lower_bound(components.begin(), components.end(), cid);
        if (it != components.end() && *it == cid) {
            return static_cast<std::size_t>(it - components.begin());
        }
        return static_cast<std::size_t>(-1);
    }
};

// ------------------------------------------------------------------
// ArchetypeStorage: owns all archetypes and the arena allocator
// ------------------------------------------------------------------
class ArchetypeStorage {
public:
    explicit ArchetypeStorage(std::size_t chunk_size = 16 * 1024)
        : chunk_size_(chunk_size) {}

    ArchetypeId find_or_create_archetype(const std::vector<ComponentId>& components);
    Archetype* get_archetype(ArchetypeId id);
    const Archetype* get_archetype(ArchetypeId id) const;

    // Move entity from one archetype to another (add/remove component).
    // Returns (chunk_index, row_index) in target archetype.
    // Returns (to_chunk_idx, to_row_idx, swapped_entity_in_source)
    std::tuple<std::uint32_t, std::uint32_t, Entity> move_entity(
        Entity entity,
        ArchetypeId from_id, std::uint32_t from_chunk, std::uint32_t from_row,
        ArchetypeId to_id,
        const ComponentRegistry& registry,
        Tick tick);

    // Remove entity from archetype (swap with last).
    // Returns the entity that was swapped into the removed slot (if any).
    Entity remove_entity(ArchetypeId id, std::uint32_t chunk_idx, std::uint32_t row,
                         const ComponentRegistry& registry);

    // Direct component access
    void* get_component(const Archetype& arch, std::uint32_t chunk_idx,
                        std::uint32_t row, std::size_t comp_idx,
                        const ComponentRegistry& registry) const;

    // Tick access
    Tick get_added_tick(const Archetype& arch, std::uint32_t chunk_idx,
                        std::size_t comp_idx, std::uint32_t row) const;
    Tick get_changed_tick(const Archetype& arch, std::uint32_t chunk_idx,
                          std::size_t comp_idx, std::uint32_t row) const;
    void set_added_tick(Archetype& arch, std::uint32_t chunk_idx,
                        std::size_t comp_idx, std::uint32_t row, Tick tick);
    void set_changed_tick(Archetype& arch, std::uint32_t chunk_idx,
                          std::size_t comp_idx, std::uint32_t row, Tick tick);

    // Allocate space for a brand-new entity in an archetype (no prior archetype)
    std::pair<std::uint32_t, std::uint32_t> allocate_entity(Entity entity, ArchetypeId arch_id,
                                                            const ComponentRegistry& registry,
                                                            Tick tick);

    std::size_t archetype_count() const noexcept { return archetypes_.size(); }

    // Per-component archetype index: fast lookup of archetypes containing a component.
    // Returns nullptr if no archetype contains the component.
    const std::vector<ArchetypeId>* archetypes_with_component(ComponentId cid) const {
        auto it = component_to_archetypes_.find(cid);
        if (it != component_to_archetypes_.end()) return &it->second;
        return nullptr;
    }

private:
    void allocate_chunk(Archetype& arch, const ComponentRegistry& registry);
    void compute_chunk_layout(const Archetype& arch, Chunk& chunk, const ComponentRegistry& registry);
    void destruct_row(Archetype& arch, Chunk& chunk, std::uint32_t row,
                      const ComponentRegistry& registry);
    void copy_or_move_row(Archetype& src_arch, Chunk& src_chunk, std::uint32_t src_row,
                          ChunkTicks& src_ticks,
                          Archetype& dst_arch, Chunk& dst_chunk, std::uint32_t dst_row,
                          ChunkTicks& dst_ticks,
                          bool move, const ComponentRegistry& registry);

    std::size_t chunk_size_ = 16 * 1024;
    detail::ArenaAllocator allocator_;
    std::vector<Archetype> archetypes_;
    std::map<std::vector<ComponentId>, ArchetypeId> archetype_map_;
    ArchetypeId next_archetype_id_ = 1;
    std::unordered_map<ComponentId, std::vector<ArchetypeId>> component_to_archetypes_;
};

// ------------------------------------------------------------------
// Inline implementations
// ------------------------------------------------------------------

inline ArchetypeId ArchetypeStorage::find_or_create_archetype(const std::vector<ComponentId>& components) {
    auto it = archetype_map_.find(components);
    if (it != archetype_map_.end()) {
        return it->second;
    }

    ArchetypeId id = next_archetype_id_++;
    Archetype arch;
    arch.id = id;
    arch.components = components; // already expected to be sorted
    archetypes_.push_back(std::move(arch));
    archetype_map_[components] = id;

    // Update per-component index
    for (ComponentId cid : components) {
        component_to_archetypes_[cid].push_back(id);
    }
    return id;
}

inline Archetype* ArchetypeStorage::get_archetype(ArchetypeId id) {
    if (id == 0 || id > archetypes_.size()) return nullptr;
    return &archetypes_[id - 1]; // ids are 1-based
}

inline const Archetype* ArchetypeStorage::get_archetype(ArchetypeId id) const {
    if (id == 0 || id > archetypes_.size()) return nullptr;
    return &archetypes_[id - 1];
}

inline void ArchetypeStorage::compute_chunk_layout(const Archetype& arch, Chunk& chunk,
                                                   const ComponentRegistry& registry) {
    std::size_t num_components = arch.components.size();
    chunk.offsets.resize(num_components + 1);

    // Gather component metadata
    std::vector<std::size_t> comp_sizes(num_components);
    std::vector<std::size_t> comp_aligns(num_components);
    std::size_t total_per_entity = sizeof(Entity);
    for (std::size_t i = 0; i < num_components; ++i) {
        const auto* info = registry.info(arch.components[i]);
        comp_sizes[i] = info ? info->size : 0;
        comp_aligns[i] = info ? info->alignment : 1;
        total_per_entity += comp_sizes[i];
    }

    std::uint32_t capacity = static_cast<std::uint32_t>(chunk_size_ / total_per_entity);
    if (capacity == 0) capacity = 1;

    // Compute exact offsets with alignment
    std::size_t offset = 0;
    chunk.offsets[0] = 0; // entity array
    offset += sizeof(Entity) * capacity;

    for (std::size_t i = 0; i < num_components; ++i) {
        std::size_t align = comp_aligns[i];
        offset = (offset + align - 1) & ~(align - 1);
        chunk.offsets[i + 1] = offset;
        offset += comp_sizes[i] * capacity;
    }

    chunk.capacity = capacity;
    chunk.total_size = offset;
}

inline void ArchetypeStorage::allocate_chunk(Archetype& arch, const ComponentRegistry& registry) {
    Chunk chunk;
    compute_chunk_layout(arch, chunk, registry);
    chunk.data = static_cast<std::byte*>(allocator_.allocate(chunk.total_size, alignof(std::max_align_t)));
    chunk.count = 0;

    // Allocate parallel tick arrays
    ChunkTicks ticks;
    std::size_t num_components = arch.components.size();
    ticks.added.resize(num_components);
    ticks.changed.resize(num_components);
    for (std::size_t i = 0; i < num_components; ++i) {
        ticks.added[i].resize(chunk.capacity, 0);
        ticks.changed[i].resize(chunk.capacity, 0);
    }

    arch.chunks.push_back(std::move(chunk));
    arch.chunk_ticks.push_back(std::move(ticks));
}

inline void ArchetypeStorage::destruct_row(Archetype& arch, Chunk& chunk, std::uint32_t row,
                                           const ComponentRegistry& registry) {
    for (std::size_t i = 0; i < arch.components.size(); ++i) {
        const auto* info = registry.info(arch.components[i]);
        if (!info || info->trivially_destructible) continue;
        void* ptr = chunk.data + chunk.offsets[i + 1] + row * info->size;
        info->destruct(ptr);
    }
}

inline void ArchetypeStorage::copy_or_move_row(Archetype& src_arch, Chunk& src_chunk, std::uint32_t src_row,
                                               ChunkTicks& src_ticks,
                                               Archetype& dst_arch, Chunk& dst_chunk, std::uint32_t dst_row,
                                               ChunkTicks& dst_ticks,
                                               bool move, const ComponentRegistry& registry) {
    // Copy entity id
    Entity* src_entities = reinterpret_cast<Entity*>(src_chunk.data + src_chunk.offsets[0]);
    Entity* dst_entities = reinterpret_cast<Entity*>(dst_chunk.data + dst_chunk.offsets[0]);
    dst_entities[dst_row] = src_entities[src_row];

    // Iterate over destination components
    std::size_t src_i = 0;
    std::size_t dst_i = 0;
    while (dst_i < dst_arch.components.size()) {
        ComponentId dst_cid = dst_arch.components[dst_i];
        std::size_t dst_offset = dst_chunk.offsets[dst_i + 1];
        const auto* dst_info = registry.info(dst_cid);
        void* dst_ptr = dst_chunk.data + dst_offset + dst_row * (dst_info ? dst_info->size : 0);

        if (src_i < src_arch.components.size() && src_arch.components[src_i] == dst_cid) {
            // Component exists in both source and destination
            std::size_t src_offset = src_chunk.offsets[src_i + 1];
            const auto* src_info = registry.info(dst_cid);
            void* src_ptr = src_chunk.data + src_offset + src_row * (src_info ? src_info->size : 0);
            if (move) {
                if (src_info && src_info->move) {
                    src_info->move(dst_ptr, src_ptr);
                }
            } else {
                if (src_info && src_info->copy) {
                    src_info->copy(dst_ptr, src_ptr);
                }
            }
            // Copy tick data
            dst_ticks.added[dst_i][dst_row] = src_ticks.added[src_i][src_row];
            dst_ticks.changed[dst_i][dst_row] = src_ticks.changed[src_i][src_row];
            ++src_i;
            ++dst_i;
        } else if (src_i < src_arch.components.size() && src_arch.components[src_i] < dst_cid) {
            // Source component not in destination — skip (will be destructed later if moving)
            ++src_i;
        } else {
            // Component only in destination — default construct
            if (dst_info && dst_info->construct) {
                dst_info->construct(dst_ptr);
            }
            // New component: ticks will be set by caller (allocate_entity/move_entity)
            ++dst_i;
        }
    }
}

inline std::tuple<std::uint32_t, std::uint32_t, Entity> ArchetypeStorage::move_entity(
    Entity entity,
    ArchetypeId from_id, std::uint32_t from_chunk, std::uint32_t from_row,
    ArchetypeId to_id,
    const ComponentRegistry& registry,
    Tick tick) {

    (void)entity;
    Archetype* from_arch = get_archetype(from_id);
    Archetype* to_arch = get_archetype(to_id);
    if (!from_arch || !to_arch) return {0, 0, null_entity};

    // Find or allocate space in target
    Chunk* to_chunk = nullptr;
    ChunkTicks* to_ticks = nullptr;
    std::uint32_t to_chunk_idx = 0;
    for (std::uint32_t i = 0; i < to_arch->chunks.size(); ++i) {
        if (to_arch->chunks[i].count < to_arch->chunks[i].capacity) {
            to_chunk = &to_arch->chunks[i];
            to_ticks = &to_arch->chunk_ticks[i];
            to_chunk_idx = i;
            break;
        }
    }
    if (!to_chunk) {
        allocate_chunk(*to_arch, registry);
        to_chunk_idx = static_cast<std::uint32_t>(to_arch->chunks.size() - 1);
        to_chunk = &to_arch->chunks.back();
        to_ticks = &to_arch->chunk_ticks.back();
    }
    std::uint32_t to_row = to_chunk->count++;
    to_arch->entity_count++;

    // Move data
    ChunkTicks& src_ticks = from_arch->chunk_ticks[from_chunk];
    copy_or_move_row(*from_arch, from_arch->chunks[from_chunk], from_row, src_ticks,
                     *to_arch, *to_chunk, to_row, *to_ticks,
                     true, registry);

    // Set added/changed tick for NEW components (present in dst but not src)
    {
        std::size_t src_i = 0;
        std::size_t dst_i = 0;
        while (dst_i < to_arch->components.size()) {
            ComponentId dst_cid = to_arch->components[dst_i];
            if (src_i < from_arch->components.size() && from_arch->components[src_i] == dst_cid) {
                ++src_i;
                ++dst_i;
            } else if (src_i < from_arch->components.size() && from_arch->components[src_i] < dst_cid) {
                ++src_i;
            } else {
                // New component
                to_ticks->added[dst_i][to_row] = tick;
                to_ticks->changed[dst_i][to_row] = tick;
                ++dst_i;
            }
        }
    }

    // Remove from source
    Chunk& src_chunk = from_arch->chunks[from_chunk];
    ChunkTicks& src_ticks_ref = from_arch->chunk_ticks[from_chunk];
    Entity swapped = null_entity;
    if (from_row < src_chunk.count - 1) {
        // Swap with last
        std::uint32_t last_row = src_chunk.count - 1;
        // Destruct the row being removed before overwriting
        destruct_row(*from_arch, src_chunk, from_row, registry);
        // We need to copy the last row to the removed row
        copy_or_move_row(*from_arch, src_chunk, last_row, src_ticks_ref,
                         *from_arch, src_chunk, from_row, src_ticks_ref,
                         true, registry);
        Entity* entities = reinterpret_cast<Entity*>(src_chunk.data + src_chunk.offsets[0]);
        swapped = entities[from_row];
    }
    // Destruct the last row (which was either moved away or is the row being removed)
    std::uint32_t last_row = src_chunk.count - 1;
    destruct_row(*from_arch, src_chunk, last_row, registry);

    src_chunk.count--;
    from_arch->entity_count--;

    return {to_chunk_idx, to_row, swapped};
}

inline Entity ArchetypeStorage::remove_entity(ArchetypeId id, std::uint32_t chunk_idx, std::uint32_t row,
                                              const ComponentRegistry& registry) {
    Archetype* arch = get_archetype(id);
    if (!arch || chunk_idx >= arch->chunks.size() || row >= arch->chunks[chunk_idx].count) {
        return null_entity;
    }

    Chunk& chunk = arch->chunks[chunk_idx];
    ChunkTicks& ticks = arch->chunk_ticks[chunk_idx];
    Entity* entities = reinterpret_cast<Entity*>(chunk.data + chunk.offsets[0]);

    Entity swapped = null_entity;
    if (row < chunk.count - 1) {
        std::uint32_t last_row = chunk.count - 1;
        // Destruct the row being removed before overwriting
        destruct_row(*arch, chunk, row, registry);
        // Copy last row to removed row
        copy_or_move_row(*arch, chunk, last_row, ticks,
                         *arch, chunk, row, ticks,
                         true, registry);
        swapped = entities[row];
    }

    // Destruct last row
    std::uint32_t last_row = chunk.count - 1;
    destruct_row(*arch, chunk, last_row, registry);

    chunk.count--;
    arch->entity_count--;

    return swapped;
}

inline void* ArchetypeStorage::get_component(const Archetype& arch, std::uint32_t chunk_idx,
                                             std::uint32_t row, std::size_t comp_idx,
                                             const ComponentRegistry& registry) const {
    if (chunk_idx >= arch.chunks.size() || comp_idx >= arch.components.size()) return nullptr;
    const Chunk& chunk = arch.chunks[chunk_idx];
    if (row >= chunk.count) return nullptr;

    const auto* info = registry.info(arch.components[comp_idx]);
    std::size_t size = info ? info->size : 0;
    return const_cast<std::byte*>(chunk.data) + chunk.offsets[comp_idx + 1] + row * size;
}

inline Tick ArchetypeStorage::get_added_tick(const Archetype& arch, std::uint32_t chunk_idx,
                                             std::size_t comp_idx, std::uint32_t row) const {
    if (chunk_idx >= arch.chunk_ticks.size() || comp_idx >= arch.components.size()) return 0;
    const ChunkTicks& ticks = arch.chunk_ticks[chunk_idx];
    if (row >= ticks.added[comp_idx].size()) return 0;
    return ticks.added[comp_idx][row];
}

inline Tick ArchetypeStorage::get_changed_tick(const Archetype& arch, std::uint32_t chunk_idx,
                                               std::size_t comp_idx, std::uint32_t row) const {
    if (chunk_idx >= arch.chunk_ticks.size() || comp_idx >= arch.components.size()) return 0;
    const ChunkTicks& ticks = arch.chunk_ticks[chunk_idx];
    if (row >= ticks.changed[comp_idx].size()) return 0;
    return ticks.changed[comp_idx][row];
}

inline void ArchetypeStorage::set_added_tick(Archetype& arch, std::uint32_t chunk_idx,
                                             std::size_t comp_idx, std::uint32_t row, Tick tick) {
    if (chunk_idx >= arch.chunk_ticks.size() || comp_idx >= arch.components.size()) return;
    ChunkTicks& ticks = arch.chunk_ticks[chunk_idx];
    if (row >= ticks.added[comp_idx].size()) return;
    ticks.added[comp_idx][row] = tick;
}

inline void ArchetypeStorage::set_changed_tick(Archetype& arch, std::uint32_t chunk_idx,
                                               std::size_t comp_idx, std::uint32_t row, Tick tick) {
    if (chunk_idx >= arch.chunk_ticks.size() || comp_idx >= arch.components.size()) return;
    ChunkTicks& ticks = arch.chunk_ticks[chunk_idx];
    if (row >= ticks.changed[comp_idx].size()) return;
    ticks.changed[comp_idx][row] = tick;
}

inline std::pair<std::uint32_t, std::uint32_t> ArchetypeStorage::allocate_entity(
    Entity entity, ArchetypeId arch_id, const ComponentRegistry& registry,
    Tick tick) {

    Archetype* arch = get_archetype(arch_id);
    if (!arch) return {0, 0};

    Chunk* chunk = nullptr;
    ChunkTicks* ticks = nullptr;
    std::uint32_t chunk_idx = 0;
    for (std::uint32_t i = 0; i < arch->chunks.size(); ++i) {
        if (arch->chunks[i].count < arch->chunks[i].capacity) {
            chunk = &arch->chunks[i];
            ticks = &arch->chunk_ticks[i];
            chunk_idx = i;
            break;
        }
    }
    if (!chunk) {
        allocate_chunk(*arch, registry);
        chunk_idx = static_cast<std::uint32_t>(arch->chunks.size() - 1);
        chunk = &arch->chunks.back();
        ticks = &arch->chunk_ticks.back();
    }

    std::uint32_t row = chunk->count++;
    arch->entity_count++;

    Entity* entities = reinterpret_cast<Entity*>(chunk->data + chunk->offsets[0]);
    entities[row] = entity;

    // Default-construct all components and set tick
    for (std::size_t i = 0; i < arch->components.size(); ++i) {
        const auto* info = registry.info(arch->components[i]);
        if (!info || !info->construct) continue;
        void* ptr = chunk->data + chunk->offsets[i + 1] + row * info->size;
        info->construct(ptr);
        ticks->added[i][row] = tick;
        ticks->changed[i][row] = tick;
    }

    return {chunk_idx, row};
}

} // namespace campello::core
