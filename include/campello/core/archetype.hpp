#pragma once

#include "types.hpp"
#include "component.hpp"
#include "detail/allocator.hpp"
#include "detail/type_info.hpp"
#include "detail/component_registry.hpp"
#include <algorithm>
#include <cstring>
#include <deque>
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

    Chunk() = default;
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    Chunk(Chunk&& other) noexcept
        : data(other.data)
        , count(other.count)
        , capacity(other.capacity)
        , total_size(other.total_size) {
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
    Archetype() = default;
    Archetype(const Archetype&) = delete;
    Archetype& operator=(const Archetype&) = delete;
    Archetype(Archetype&&) = default;
    Archetype& operator=(Archetype&&) = default;

    ArchetypeId id = 0;
    std::vector<ComponentId> components; // sorted
    std::vector<Chunk> chunks;
    std::vector<ChunkTicks> chunk_ticks;
    std::uint32_t entity_count = 0;

    // Layout shared by all chunks of this archetype.
    // offsets[i] is the byte offset of component (i-1)'s array within data.
    // offsets[0] is always the entity array.
    std::vector<std::size_t> offsets;
    std::uint32_t capacity = 0;
    std::size_t data_size = 0; // total bytes allocated per chunk

    // Cached component metadata (avoids registry lookups on hot paths).
    // Filled during compute_chunk_layout; indices match arch.components.
    std::vector<detail::TypeInfo> component_infos;

    // True if all components in this archetype are trivially relocatable.
    // Computed during chunk layout; defaults to false until layout is known.
    bool trivially_relocatable = false;

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

    bool is_trivially_relocatable() const noexcept {
        return trivially_relocatable;
    }
};

// ------------------------------------------------------------------
// ArchetypeStorage: owns all archetypes and the arena allocator
// ------------------------------------------------------------------
class ArchetypeStorage {
public:
    explicit ArchetypeStorage(std::size_t chunk_size = 64 * 1024)
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
                                                            Tick tick,
                                                            bool construct_components = true);

    std::size_t archetype_count() const noexcept { return archetypes_.size(); }

    // Return a chunk with free space in the given archetype, allocating a new chunk if necessary.
    // Returns (chunk, chunk_ticks, chunk_index). Returns (nullptr, nullptr, 0) if archetype is invalid.
    std::tuple<Chunk*, ChunkTicks*, std::uint32_t> ensure_chunk_with_space(
        ArchetypeId arch_id, const ComponentRegistry& registry);

    // Per-component archetype index: fast lookup of archetypes containing a component.
    // Returns nullptr if no archetype contains the component.
    const std::vector<ArchetypeId>* archetypes_with_component(ComponentId cid) const {
        auto it = component_to_archetypes_.find(cid);
        if (it != component_to_archetypes_.end()) return &it->second;
        return nullptr;
    }

private:
    void allocate_chunk(Archetype& arch, const ComponentRegistry& registry);
    void compute_chunk_layout(Archetype& arch, const ComponentRegistry& registry);
    void destruct_row(Archetype& arch, Chunk& chunk, std::uint32_t row,
                      const ComponentRegistry& registry);
    void copy_or_move_row(Archetype& src_arch, Chunk& src_chunk, std::uint32_t src_row,
                          ChunkTicks& src_ticks,
                          Archetype& dst_arch, Chunk& dst_chunk, std::uint32_t dst_row,
                          ChunkTicks& dst_ticks,
                          bool move, const ComponentRegistry& registry);

    std::size_t chunk_size_ = 16 * 1024;
    detail::ArenaAllocator allocator_;
    // deque (not vector): push_back must not invalidate existing Archetype*
    // pointers, since queries cache them across archetype-creating callbacks
    // (e.g. removing a component during iteration can create a new archetype).
    std::deque<Archetype> archetypes_;
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

inline void ArchetypeStorage::compute_chunk_layout(Archetype& arch,
                                                   const ComponentRegistry& registry) {
    std::size_t num_components = arch.components.size();
    arch.offsets.resize(num_components + 1);
    arch.component_infos.resize(num_components);

    // Gather component metadata
    std::vector<std::size_t> comp_sizes(num_components);
    std::vector<std::size_t> comp_aligns(num_components);
    std::size_t total_per_entity = sizeof(Entity);
    bool all_trivial = true;
    for (std::size_t i = 0; i < num_components; ++i) {
        const auto* info = registry.info(arch.components[i]);
        arch.component_infos[i] = info ? *info : detail::TypeInfo{};
        comp_sizes[i] = info ? info->size : 0;
        comp_aligns[i] = info ? info->alignment : 1;
        total_per_entity += comp_sizes[i];
        if (!info || !info->trivially_relocatable) {
            all_trivial = false;
        }
    }
    arch.trivially_relocatable = all_trivial;

    std::uint32_t capacity = static_cast<std::uint32_t>(chunk_size_ / total_per_entity);
    if (capacity == 0) capacity = 1;
    arch.capacity = capacity;

    // Compute exact offsets with alignment
    std::size_t offset = 0;
    arch.offsets[0] = 0; // entity array
    offset += sizeof(Entity) * capacity;

    for (std::size_t i = 0; i < num_components; ++i) {
        std::size_t align = comp_aligns[i];
        offset = (offset + align - 1) & ~(align - 1);
        arch.offsets[i + 1] = offset;
        offset += comp_sizes[i] * capacity;
    }

    arch.data_size = offset;
}

inline void ArchetypeStorage::allocate_chunk(Archetype& arch, const ComponentRegistry& registry) {
    if (arch.offsets.empty()) {
        compute_chunk_layout(arch, registry);
    }

    Chunk chunk;
    chunk.capacity = arch.capacity;
    chunk.total_size = arch.data_size;

    // Ensure chunk base is aligned to the largest component alignment so that
    // every component array is absolutely aligned (not just relative to chunk.data).
    std::size_t max_align = alignof(std::max_align_t);
    for (const auto& info : arch.component_infos) {
        if (info.alignment > max_align) max_align = info.alignment;
    }

    // Over-allocate by max_align so we can manually align the base pointer.
    void* raw = allocator_.allocate(chunk.total_size + max_align, alignof(std::max_align_t));
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(raw);
    std::size_t padding = (max_align - (addr % max_align)) % max_align;
    chunk.data = static_cast<std::byte*>(raw) + padding;
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
                                           const ComponentRegistry& /*registry*/) {
    for (std::size_t i = 0; i < arch.components.size(); ++i) {
        const detail::TypeInfo& info = arch.component_infos[i];
        if (info.trivially_destructible) continue;
        void* ptr = chunk.data + arch.offsets[i + 1] + row * info.size;
        if (info.destruct) info.destruct(ptr);
    }
}

inline void ArchetypeStorage::copy_or_move_row(Archetype& src_arch, Chunk& src_chunk, std::uint32_t src_row,
                                               ChunkTicks& src_ticks,
                                               Archetype& dst_arch, Chunk& dst_chunk, std::uint32_t dst_row,
                                               ChunkTicks& dst_ticks,
                                               bool move, const ComponentRegistry& /*registry*/) {
    // Copy entity id
    Entity* src_entities = reinterpret_cast<Entity*>(src_chunk.data + src_arch.offsets[0]);
    Entity* dst_entities = reinterpret_cast<Entity*>(dst_chunk.data + dst_arch.offsets[0]);
    dst_entities[dst_row] = src_entities[src_row];

    // Iterate over destination components using cached metadata.
    std::size_t src_i = 0;
    std::size_t dst_i = 0;
    while (dst_i < dst_arch.components.size()) {
        const detail::TypeInfo& dst_info = dst_arch.component_infos[dst_i];
        std::size_t dst_offset = dst_arch.offsets[dst_i + 1];
        void* dst_ptr = dst_chunk.data + dst_offset + dst_row * dst_info.size;

        if (src_i < src_arch.components.size() && src_arch.components[src_i] == dst_arch.components[dst_i]) {
            // Component exists in both source and destination
            const detail::TypeInfo& src_info = src_arch.component_infos[src_i];
            std::size_t src_offset = src_arch.offsets[src_i + 1];
            void* src_ptr = src_chunk.data + src_offset + src_row * src_info.size;
            if (src_info.trivially_relocatable && dst_info.trivially_relocatable) {
                std::memcpy(dst_ptr, src_ptr, src_info.size);
            } else if (move) {
                if (src_info.move) {
                    src_info.move(dst_ptr, src_ptr);
                }
            } else {
                if (src_info.copy) {
                    src_info.copy(dst_ptr, src_ptr);
                }
            }
            // Copy tick data
            std::memcpy(&dst_ticks.added[dst_i][dst_row], &src_ticks.added[src_i][src_row], sizeof(Tick));
            std::memcpy(&dst_ticks.changed[dst_i][dst_row], &src_ticks.changed[src_i][src_row], sizeof(Tick));
            ++src_i;
            ++dst_i;
        } else if (src_i < src_arch.components.size() && src_arch.components[src_i] < dst_arch.components[dst_i]) {
            // Source component not in destination — skip (will be destructed later if moving)
            ++src_i;
        } else {
            // Component only in destination — default construct
            if (dst_info.construct) {
                dst_info.construct(dst_ptr);
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
    auto [to_chunk, to_ticks, to_chunk_idx] = ensure_chunk_with_space(to_id, registry);
    if (!to_chunk) return {0, 0, null_entity};
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
    const bool from_trivial = from_arch->is_trivially_relocatable();
    Entity swapped = null_entity;
    if (from_row < src_chunk.count - 1) {
        // Swap with last
        std::uint32_t last_row = src_chunk.count - 1;
        Entity* entities = reinterpret_cast<Entity*>(src_chunk.data + from_arch->offsets[0]);
        if (from_trivial) {
            // Fast path: move entity id, components, and ticks with memcpy.
            entities[from_row] = entities[last_row];
            for (std::size_t i = 0; i < from_arch->components.size(); ++i) {
                const auto* info = registry.info(from_arch->components[i]);
                if (!info) continue;
                std::byte* base = src_chunk.data + from_arch->offsets[i + 1];
                std::memcpy(base + from_row * info->size, base + last_row * info->size, info->size);
                src_ticks_ref.added[i][from_row] = src_ticks_ref.added[i][last_row];
                src_ticks_ref.changed[i][from_row] = src_ticks_ref.changed[i][last_row];
            }
            swapped = entities[from_row];
        } else {
            // Destruct the row being removed before overwriting
            destruct_row(*from_arch, src_chunk, from_row, registry);
            // We need to copy the last row to the removed row
            copy_or_move_row(*from_arch, src_chunk, last_row, src_ticks_ref,
                             *from_arch, src_chunk, from_row, src_ticks_ref,
                             true, registry);
            swapped = entities[from_row];
        }
    }
    // Destruct the last row only for non-trivial archetypes.
    std::uint32_t last_row = src_chunk.count - 1;
    if (!from_trivial) {
        destruct_row(*from_arch, src_chunk, last_row, registry);
    }

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
    Entity* entities = reinterpret_cast<Entity*>(chunk.data + arch->offsets[0]);

    const bool trivial = arch->is_trivially_relocatable();

    Entity swapped = null_entity;
    if (row < chunk.count - 1) {
        std::uint32_t last_row = chunk.count - 1;
        if (trivial) {
            // Fast path: move entity id and all component rows with memcpy,
            // plus their tick arrays.
            entities[row] = entities[last_row];
            for (std::size_t i = 0; i < arch->components.size(); ++i) {
                const detail::TypeInfo& info = arch->component_infos[i];
                if (info.size == 0) continue;
                std::byte* base = chunk.data + arch->offsets[i + 1];
                std::memcpy(base + row * info.size, base + last_row * info.size, info.size);
                std::memcpy(&ticks.added[i][row], &ticks.added[i][last_row], sizeof(Tick));
                std::memcpy(&ticks.changed[i][row], &ticks.changed[i][last_row], sizeof(Tick));
            }
            swapped = entities[row];
        } else {
            // Destruct the row being removed before overwriting
            destruct_row(*arch, chunk, row, registry);
            // Copy last row to removed row
            copy_or_move_row(*arch, chunk, last_row, ticks,
                             *arch, chunk, row, ticks,
                             true, registry);
            swapped = entities[row];
        }
    }

    // Destruct last row only for non-trivial archetypes.
    std::uint32_t last_row = chunk.count - 1;
    if (!trivial) {
        destruct_row(*arch, chunk, last_row, registry);
    }

    chunk.count--;
    arch->entity_count--;

    return swapped;
}

inline void* ArchetypeStorage::get_component(const Archetype& arch, std::uint32_t chunk_idx,
                                             std::uint32_t row, std::size_t comp_idx,
                                             const ComponentRegistry& /*registry*/) const {
    if (chunk_idx >= arch.chunks.size() || comp_idx >= arch.components.size()) return nullptr;
    const Chunk& chunk = arch.chunks[chunk_idx];
    if (row >= chunk.count) return nullptr;

    const detail::TypeInfo& info = arch.component_infos[comp_idx];
    return const_cast<std::byte*>(chunk.data) + arch.offsets[comp_idx + 1] + row * info.size;
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

inline std::tuple<Chunk*, ChunkTicks*, std::uint32_t> ArchetypeStorage::ensure_chunk_with_space(
    ArchetypeId arch_id, const ComponentRegistry& registry) {
    Archetype* arch = get_archetype(arch_id);
    if (!arch) return {nullptr, nullptr, 0};
    // Search from the back: newest chunks usually have free space, and this reuses
    // holes created by recent despawns before allocating a new chunk.
    for (std::uint32_t i = static_cast<std::uint32_t>(arch->chunks.size()); i-- > 0;) {
        if (arch->chunks[i].count < arch->chunks[i].capacity) {
            return {&arch->chunks[i], &arch->chunk_ticks[i], i};
        }
    }
    allocate_chunk(*arch, registry);
    std::uint32_t idx = static_cast<std::uint32_t>(arch->chunks.size() - 1);
    return {&arch->chunks.back(), &arch->chunk_ticks.back(), idx};
}

inline std::pair<std::uint32_t, std::uint32_t> ArchetypeStorage::allocate_entity(
    Entity entity, ArchetypeId arch_id, const ComponentRegistry& registry,
    Tick tick, bool construct_components) {

    Archetype* arch = get_archetype(arch_id);
    if (!arch) return {0, 0};

    auto [chunk, ticks, chunk_idx] = ensure_chunk_with_space(arch_id, registry);
    if (!chunk) return {0, 0};

    std::uint32_t row = chunk->count++;
    arch->entity_count++;

    Entity* entities = reinterpret_cast<Entity*>(chunk->data + arch->offsets[0]);
    entities[row] = entity;

    // Default-construct all components and set tick (unless caller will construct).
    if (construct_components) {
        for (std::size_t i = 0; i < arch->components.size(); ++i) {
            const detail::TypeInfo& info = arch->component_infos[i];
            if (!info.construct) continue;
            void* ptr = chunk->data + arch->offsets[i + 1] + row * info.size;
            info.construct(ptr);
            ticks->added[i][row] = tick;
            ticks->changed[i][row] = tick;
        }
    }

    return {chunk_idx, row};
}

} // namespace campello::core
