#pragma once

#include "type_info.hpp"
#include "../component.hpp"
#include <unordered_map>

namespace campello::core {

class ComponentRegistry {
public:
    template<component T>
    void register_component() {
        detail::TypeId id = component_type_id<T>();
        if (infos_.contains(id)) return;
        infos_.emplace(id, component_type_info<T>());
    }

    const detail::TypeInfo* info(ComponentId id) const {
        auto it = infos_.find(id);
        return it != infos_.end() ? &it->second : nullptr;
    }

    detail::TypeInfo* info(ComponentId id) {
        auto it = infos_.find(id);
        return it != infos_.end() ? &it->second : nullptr;
    }

    bool contains(ComponentId id) const {
        return infos_.contains(id);
    }

    std::size_t size() const { return infos_.size(); }

private:
    std::unordered_map<ComponentId, detail::TypeInfo> infos_;
};

} // namespace campello::core
