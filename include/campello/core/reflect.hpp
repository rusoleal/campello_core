#pragma once

#include "types.hpp"
#include "detail/type_info.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace campello::core {

// Forward declarations
class ReflectRegistry;

// Type name helper (non-constexpr fallback)
namespace detail {

template<typename T>
std::string_view type_id_name() {
    return typeid(T).name();
}

} // namespace detail

// ------------------------------------------------------------------
// Property: metadata for a single field within a component
// ------------------------------------------------------------------
struct Property {
    std::string name;
    std::size_t offset = 0;
    std::size_t size = 0;
    detail::TypeId type_id = 0;
    std::string_view type_name = "unknown";

    // Pointer to the property value within a component instance
    void* value_ptr(void* component) const {
        return static_cast<std::byte*>(component) + offset;
    }

    const void* value_ptr(const void* component) const {
        return static_cast<const std::byte*>(component) + offset;
    }
};

// ------------------------------------------------------------------
// ComponentInfo: runtime metadata for a component type
// ------------------------------------------------------------------
struct ComponentInfo {
    ComponentId id = 0;
    std::string name;
    std::size_t size = 0;
    std::size_t alignment = 0;
    bool trivially_relocatable = false;
    std::vector<Property> properties;

    const Property* find_property(std::string_view name) const {
        for (const auto& p : properties) {
            if (p.name == name) return &p;
        }
        return nullptr;
    }
};

// ------------------------------------------------------------------
// ComponentBuilder: fluent API for describing component properties
// ------------------------------------------------------------------
class ComponentBuilder {
public:
    explicit ComponentBuilder(std::string_view name)
        : info_{0, std::string(name), 0, 0, false, {}} {}

    template<typename T, typename M>
    ComponentBuilder& property(std::string_view prop_name, M T::*member) {
        Property p;
        p.name = std::string(prop_name);
        p.offset = member_offset(member);
        p.size = sizeof(M);
        p.type_id = detail::type_id<M>();
        p.type_name = detail::type_id_name<M>();
        info_.properties.push_back(std::move(p));
        return *this;
    }

    ComponentBuilder& size(std::size_t s) {
        info_.size = s;
        return *this;
    }

    ComponentBuilder& alignment(std::size_t a) {
        info_.alignment = a;
        return *this;
    }

    ComponentBuilder& relocatable(bool r) {
        info_.trivially_relocatable = r;
        return *this;
    }

    ComponentInfo build() { return std::move(info_); }

private:
    template<typename T, typename M>
    static std::size_t member_offset(M T::*member) {
        // Compute offset from member pointer.
        // This is well-defined for standard-layout types.
        alignas(alignof(T)) std::byte storage[sizeof(T)]{};
        T* obj = reinterpret_cast<T*>(storage);
        return reinterpret_cast<std::size_t>(&(obj->*member))
             - reinterpret_cast<std::size_t>(obj);
    }

    ComponentInfo info_;
};

// ------------------------------------------------------------------
// ReflectRegistry: stores runtime metadata for all component types
// ------------------------------------------------------------------
class ReflectRegistry {
public:
    void register_info(ComponentId id, ComponentInfo info) {
        info.id = id;
        infos_[id] = std::move(info);
    }

    const ComponentInfo* info(ComponentId id) const {
        auto it = infos_.find(id);
        return it != infos_.end() ? &it->second : nullptr;
    }

    const ComponentInfo* info(std::string_view name) const {
        for (const auto& [id, info] : infos_) {
            if (info.name == name) return &info;
        }
        return nullptr;
    }

    bool contains(ComponentId id) const {
        return infos_.contains(id);
    }

    std::size_t size() const { return infos_.size(); }

    auto begin() const { return infos_.begin(); }
    auto end() const { return infos_.end(); }

private:
    std::unordered_map<ComponentId, ComponentInfo> infos_;
};

// ------------------------------------------------------------------
} // namespace campello::core
