#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>
#include <typeindex>

namespace campello::core::detail {

using TypeId = std::uint64_t;

inline TypeId make_type_id(std::type_index ti) {
    return std::hash<std::type_index>{}(ti);
}

template<typename T>
TypeId type_id() {
    static const TypeId id = make_type_id(typeid(T));
    return id;
}

struct TypeInfo {
    TypeId id;
    std::size_t size;
    std::size_t alignment;
    std::string_view name;
    bool trivially_destructible;
    bool trivially_copyable;
    bool trivially_relocatable;

    void (*construct)(void* ptr);
    void (*destruct)(void* ptr);
    void (*move)(void* dst, void* src);
    void (*copy)(void* dst, const void* src);
};

template<typename T>
TypeInfo make_type_info(std::string_view name) {
    TypeInfo info{};
    info.id = type_id<T>();
    info.size = sizeof(T);
    info.alignment = alignof(T);
    info.name = name;
    info.trivially_destructible = std::is_trivially_destructible_v<T>;
    info.trivially_copyable = std::is_trivially_copyable_v<T>;
    info.trivially_relocatable = std::is_trivially_copyable_v<T>; // default, overridable

    info.construct = [](void* ptr) { new (ptr) T(); };
    info.destruct = [](void* ptr) { static_cast<T*>(ptr)->~T(); };
    info.move = [](void* dst, void* src) {
        new (dst) T(std::move(*static_cast<T*>(src)));
    };
    info.copy = [](void* dst, const void* src) {
        new (dst) T(*static_cast<const T*>(src));
    };

    return info;
}

} // namespace campello::core::detail
