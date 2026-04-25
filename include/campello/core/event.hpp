#pragma once

#include "detail/type_info.hpp"
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace campello::core {

// ------------------------------------------------------------------
// EventStorage: type-erased per-type event buffers
// ------------------------------------------------------------------
class EventStorage {
public:
    template<typename E>
    void send(E&& event) {
        static_assert(std::is_move_constructible_v<E>, "Event type must be move constructible");
        auto& vec = get_vector<E>();
        vec.push_back(std::forward<E>(event));
    }

    template<typename E>
    const std::vector<E>& read() const {
        return get_vector<E>();
    }

    template<typename E>
    void clear() {
        get_vector<E>().clear();
    }

    void clear_all() {
        vectors_.clear();
    }

    template<typename E>
    bool empty() const noexcept {
        return get_vector<E>().empty();
    }

private:
    template<typename E>
    std::vector<E>& get_vector() {
        detail::TypeId id = detail::type_id<E>();
        auto it = vectors_.find(id);
        if (it != vectors_.end()) {
            return *reinterpret_cast<std::vector<E>*>(it->second.get());
        }
        auto ptr = std::make_shared<std::vector<E>>();
        std::vector<E>* raw = ptr.get();
        vectors_[id] = std::static_pointer_cast<void>(ptr);
        return *raw;
    }

    template<typename E>
    const std::vector<E>& get_vector() const {
        detail::TypeId id = detail::type_id<E>();
        auto it = vectors_.find(id);
        if (it != vectors_.end()) {
            return *reinterpret_cast<const std::vector<E>*>(it->second.get());
        }
        static const std::vector<E> empty;
        return empty;
    }

    std::unordered_map<detail::TypeId, std::shared_ptr<void>> vectors_;

    template<typename E>
    std::shared_ptr<void> make_vector_ptr() {
        return std::static_pointer_cast<void>(std::make_shared<std::vector<E>>());
    }
};

// ------------------------------------------------------------------
// EventWriter / EventReader
// ------------------------------------------------------------------
template<typename E>
class EventWriter {
public:
    explicit EventWriter(EventStorage& storage) : storage_(&storage) {}

    void send(E&& event) {
        storage_->send<E>(std::forward<E>(event));
    }

    template<typename... Args>
    void emit(Args&&... args) {
        storage_->send<E>(E{std::forward<Args>(args)...});
    }

private:
    EventStorage* storage_;
};

template<typename E>
class EventReader {
public:
    explicit EventReader(const EventStorage& storage)
        : events_(&storage.read<E>()) {}

    const E* begin() const { return events_->data(); }
    const E* end() const { return events_->data() + events_->size(); }
    std::size_t size() const { return events_->size(); }
    bool empty() const { return events_->empty(); }

private:
    const std::vector<E>* events_;
};

} // namespace campello::core
