#pragma once

#include "detail/type_info.hpp"
#include <string_view>
#include <type_traits>

namespace campello::core {

class ComponentBuilder;

// Base ComponentTraits — provides defaults for any type.
// Users can specialize ComponentTraits<T> (deriving from ComponentTraitsBase<T>)
// to provide editor-friendly names and reflection metadata.
class ComponentBuilder;

template<typename T>
struct ComponentTraitsBase {
    static constexpr std::string_view name = "unknown";
    static constexpr bool trivially_relocatable = std::is_trivially_copyable_v<T>;
    static constexpr std::size_t alignment = alignof(T);
    static void reflect(ComponentBuilder&) {}
};

template<typename T>
struct ComponentTraits : ComponentTraitsBase<T> {};  // user-specializable

// Concept: a valid component type
template<typename T>
concept component = std::is_move_constructible_v<T> && std::is_destructible_v<T>;

// Helpers to get component id and info inline
template<typename T>
detail::TypeId component_type_id() {
    return detail::type_id<T>();
}

template<typename T>
detail::TypeInfo component_type_info() {
    auto info = detail::make_type_info<T>(ComponentTraits<T>::name);
    info.trivially_relocatable = ComponentTraits<T>::trivially_relocatable;
    return info;
}

} // namespace campello::core
