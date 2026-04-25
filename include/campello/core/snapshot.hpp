#pragma once

#include "world.hpp"
#include "serialize.hpp"
#include <string_view>

namespace campello::core {

namespace detail {

// Skip whitespace in a string view
inline void skip_ws(std::string_view s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

// Extract a JSON object starting at position i (which must point to '{')
// Returns the object including braces and advances i past it.
inline std::string_view extract_json_object(std::string_view s, std::size_t& i) {
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '{') return {};
    std::size_t start = i;
    int depth = 0;
    do {
        if (s[i] == '{') ++depth;
        else if (s[i] == '}') --depth;
        else if (s[i] == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) i += 2;
                else ++i;
            }
        }
        ++i;
    } while (i < s.size() && depth > 0);
    return s.substr(start, i - start);
}

// Parse a flat JSON object, extracting only top-level key -> value mappings.
// Values that are nested objects are returned as raw object strings.
inline std::unordered_map<std::string, std::string> parse_flat_json_with_objects(std::string_view json) {
    std::unordered_map<std::string, std::string> result;
    std::size_t i = 0;
    const std::size_t n = json.size();

    auto parse_string = [&](bool unescape) -> std::string {
        std::string key;
        ++i; // skip opening quote
        while (i < n && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < n) {
                ++i;
                if (unescape) {
                    switch (json[i]) {
                        case '"': key.push_back('"'); break;
                        case '\\': key.push_back('\\'); break;
                        case 'b': key.push_back('\b'); break;
                        case 'f': key.push_back('\f'); break;
                        case 'n': key.push_back('\n'); break;
                        case 'r': key.push_back('\r'); break;
                        case 't': key.push_back('\t'); break;
                        default: key.push_back(json[i]); break;
                    }
                } else {
                    key.push_back('\\');
                    key.push_back(json[i]);
                }
            } else {
                key.push_back(json[i]);
            }
            ++i;
        }
        if (i < n && json[i] == '"') ++i; // skip closing quote
        return key;
    };

    skip_ws(json, i);
    if (i < n && json[i] == '{') ++i;
    while (i < n) {
        skip_ws(json, i);
        if (i < n && json[i] == '}') break;
        if (i < n && json[i] == '"') {
            std::string key = parse_string(true);
            skip_ws(json, i);
            if (i < n && json[i] == ':') ++i;
            skip_ws(json, i);
            std::string val;
            if (i < n && json[i] == '{') {
                // Nested object — extract raw
                std::size_t obj_start = i;
                extract_json_object(json, i);
                val = std::string(json.substr(obj_start, i - obj_start));
            } else if (i < n && json[i] == '"') {
                // String value — include quotes
                std::size_t str_start = i;
                parse_string(false); // advances i, keeps escapes
                val = std::string(json.substr(str_start, i - str_start));
            } else {
                // Primitive
                std::size_t start = i;
                while (i < n && json[i] != ',' && json[i] != '}') {
                    if (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r') break;
                    ++i;
                }
                val = std::string(json.substr(start, i - start));
                while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\n' || val.back() == '\r'))
                    val.pop_back();
            }
            result[std::move(key)] = std::move(val);
        }
        skip_ws(json, i);
        if (i < n && json[i] == ',') ++i;
    }
    return result;
}

} // namespace detail

// ------------------------------------------------------------------
// WorldSnapshot: serialize / deserialize world state
// ------------------------------------------------------------------

inline std::string World::snapshot() const {
    TypeSerializerRegistry reg;
    register_primitive_serializers(reg);

    std::string result = "{\"entities\":[";
    bool first_entity = true;

    // Iterate all archetypes and their chunks
    for (std::size_t arch_i = 0; arch_i < archetype_storage_.archetype_count(); ++arch_i) {
        ArchetypeId arch_id = static_cast<ArchetypeId>(arch_i + 1);
        const Archetype* arch = archetype_storage_.get_archetype(arch_id);
        if (!arch || arch->entity_count == 0) continue;

        for (std::size_t chunk_idx = 0; chunk_idx < arch->chunks.size(); ++chunk_idx) {
            const Chunk& chunk = arch->chunks[chunk_idx];
            if (chunk.count == 0) continue;

            const Entity* entities = reinterpret_cast<const Entity*>(chunk.data + chunk.offsets[0]);
            for (std::uint32_t row = 0; row < chunk.count; ++row) {
                Entity e = entities[row];
                if (!is_alive(e)) continue;

                if (!first_entity) result += ',';
                first_entity = false;

                result += "{\"id\":";
                result += std::to_string(e);
                result += ',';

                bool first_comp = true;
                for (std::size_t ci = 0; ci < arch->components.size(); ++ci) {
                    ComponentId cid = arch->components[ci];
                    const ComponentInfo* info = reflect_registry_.info(cid);
                    if (!info) continue;

                    const void* ptr = chunk.data + chunk.offsets[ci + 1] + row * info->size;
                    std::string comp_json = serialize_component(*info, ptr, reg);

                    if (!first_comp) result += ',';
                    first_comp = false;

                    result += '"';
                    result += info->name;
                    result += "\":";
                    result += comp_json;
                }
                result += '}';
            }
        }
    }

    result += "]}";
    return result;
}

inline void World::restore(std::string_view json) {
    TypeSerializerRegistry reg;
    register_primitive_serializers(reg);

    clear();

    // Find the "entities" array
    std::size_t i = 0;
    auto array_start = json.find('[');
    if (array_start == std::string::npos) return;
    i = array_start + 1;

    while (i < json.size()) {
        detail::skip_ws(json, i);
        if (i >= json.size() || json[i] == ']') break;
        if (json[i] == ',') { ++i; continue; }
        if (json[i] != '{') break;

        std::string_view entity_obj = detail::extract_json_object(json, i);
        auto fields = detail::parse_flat_json_with_objects(entity_obj);

        // Parse entity ID (we don't reuse it, but we could in future)
        // auto id_it = fields.find("id");
        // Entity old_id = 0;
        // if (id_it != fields.end()) {
        //     old_id = static_cast<Entity>(std::stoull(id_it->second));
        // }

        Entity e = spawn();

        for (auto& [key, val] : fields) {
            if (key == "id") continue;

            const ComponentInfo* info = reflect_registry_.info(key);
            if (!info) continue;

            ComponentId cid = info->id;
            if (cid == 0) continue;

            // Allocate temp buffer for component
            std::vector<std::byte> buffer(info->size);
            void* ptr = buffer.data();
            if (info->alignment > alignof(std::max_align_t)) {
                // Over-aligned type — use aligned allocation
                void* aligned = nullptr;
                if (posix_memalign(&aligned, info->alignment, info->size) != 0) continue;
                ptr = aligned;
            }

            // Default-construct
            const auto* type_info = component_registry_.info(cid);
            if (type_info && type_info->construct) {
                type_info->construct(ptr);
            }

            // Deserialize
            bool ok = deserialize_component(*info, ptr, val, reg);
            if (ok) {
                type_erased_insert(e, cid, ptr);
            }

            // Destruct temp
            if (type_info && type_info->destruct) {
                type_info->destruct(ptr);
            }

            if (ptr != buffer.data()) {
                free(ptr);
            }
        }
    }
}

inline void World::clear() {
    // Call on_remove hooks for all alive entities (before clearing state)
    for (std::size_t arch_i = 0; arch_i < archetype_storage_.archetype_count(); ++arch_i) {
        ArchetypeId arch_id = static_cast<ArchetypeId>(arch_i + 1);
        Archetype* arch = archetype_storage_.get_archetype(arch_id);
        if (!arch || arch->entity_count == 0) continue;

        for (std::size_t chunk_idx = 0; chunk_idx < arch->chunks.size(); ++chunk_idx) {
            Chunk& chunk = arch->chunks[chunk_idx];
            if (chunk.count == 0) continue;
            Entity* entities = reinterpret_cast<Entity*>(chunk.data + chunk.offsets[0]);
            for (std::uint32_t row = 0; row < chunk.count; ++row) {
                Entity e = entities[row];
                if (is_alive(e)) {
                    call_on_remove_hooks(e);
                }
            }
        }
    }

    // Call destructors for all components
    for (std::size_t arch_i = 0; arch_i < archetype_storage_.archetype_count(); ++arch_i) {
        ArchetypeId arch_id = static_cast<ArchetypeId>(arch_i + 1);
        Archetype* arch = archetype_storage_.get_archetype(arch_id);
        if (!arch) continue;

        for (std::size_t chunk_idx = 0; chunk_idx < arch->chunks.size(); ++chunk_idx) {
            Chunk& chunk = arch->chunks[chunk_idx];
            for (std::uint32_t row = 0; row < chunk.count; ++row) {
                for (std::size_t ci = 0; ci < arch->components.size(); ++ci) {
                    ComponentId cid = arch->components[ci];
                    const auto* info = component_registry_.info(cid);
                    if (!info || info->trivially_destructible) continue;
                    void* ptr = chunk.data + chunk.offsets[ci + 1] + row * info->size;
                    info->destruct(ptr);
                }
            }
        }
    }

    // Reset state
    entity_allocator_ = EntityAllocator();
    entity_meta_.clear();
    archetype_storage_ = ArchetypeStorage();
    archetype_version_ = 0;
    change_tick_ = 1;
    removed_components_.clear();
    // Keep: component_registry_, reflect_registry_, event_storage_, resource_storage_, hooks
}

inline void World::type_erased_insert(Entity entity, ComponentId cid, const void* data) {
    if (!is_alive(entity)) return;

    std::uint32_t idx = entity_index(entity);
    EntityMeta& meta = entity_meta_[idx];

    // If entity already has this component, just overwrite
    if (meta.archetype != 0) {
        Archetype* arch = archetype_storage_.get_archetype(meta.archetype);
        if (arch && arch->has_component(cid)) {
            std::size_t comp_idx = arch->component_index(cid);
            void* ptr = archetype_storage_.get_component(*arch, meta.chunk_index, meta.row_index,
                                                         comp_idx, component_registry_);
            const auto* info = component_registry_.info(cid);
            if (info && info->destruct) info->destruct(ptr);
            if (info && info->copy) info->copy(ptr, data);
            return;
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
        auto [new_chunk, new_row] = archetype_storage_.allocate_entity(
            entity, new_arch, component_registry_, change_tick_);
        meta.archetype = new_arch;
        meta.chunk_index = new_chunk;
        meta.row_index = new_row;
    } else {
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

    // Copy component data into place
    Archetype* arch = archetype_storage_.get_archetype(new_arch);
    std::size_t comp_idx = arch->component_index(cid);
    void* ptr = archetype_storage_.get_component(*arch, meta.chunk_index, meta.row_index,
                                                 comp_idx, component_registry_);
    const auto* info = component_registry_.info(cid);
    if (info && info->destruct) info->destruct(ptr);
    if (info && info->copy) info->copy(ptr, data);

    // Call on_add hooks
    auto it = on_add_hooks_.find(cid);
    if (it != on_add_hooks_.end()) {
        for (auto& hook : it->second) {
            hook(entity, ptr);
        }
    }
}

} // namespace campello::core
