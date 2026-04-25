#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>

namespace campello::core {

using Entity = std::uint64_t;
using ComponentId = std::uint64_t;
using ArchetypeId = std::uint64_t;
using TypeId = std::uint64_t;
using Tick = std::uint32_t;

constexpr Entity null_entity = 0;

inline Entity make_entity(std::uint32_t index, std::uint32_t generation) noexcept {
    return (static_cast<Entity>(generation) << 32) | index;
}

inline std::uint32_t entity_index(Entity e) noexcept {
    return static_cast<std::uint32_t>(e);
}

inline std::uint32_t entity_generation(Entity e) noexcept {
    return static_cast<std::uint32_t>(e >> 32);
}

struct EntityMeta {
    ArchetypeId archetype = 0;
    std::uint32_t chunk_index = 0;
    std::uint32_t row_index = 0;
};

} // namespace campello::core

// std::hash<std::uint64_t> is already defined, so std::hash<Entity> works automatically
// since Entity is a type alias for std::uint64_t.
