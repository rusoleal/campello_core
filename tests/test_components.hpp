#pragma once

#include <campello/core/world.hpp>

// Common test components with proper ComponentTraits specializations.
// In a real project these would live in a shared header.

struct Position {
    float x, y, z;
};

struct Velocity {
    float dx, dy, dz;
};

struct Health {
    int value;
};

namespace campello::core {

template<>
struct ComponentTraits<Position> : ComponentTraitsBase<Position> {
    static constexpr std::string_view name = "Position";
    static void reflect(ComponentBuilder& b) {
        b.property("x", &Position::x)
         .property("y", &Position::y)
         .property("z", &Position::z);
    }
};

template<>
struct ComponentTraits<Velocity> : ComponentTraitsBase<Velocity> {
    static constexpr std::string_view name = "Velocity";
};

template<>
struct ComponentTraits<Health> : ComponentTraitsBase<Health> {
    static constexpr std::string_view name = "Health";
};

} // namespace campello::core
