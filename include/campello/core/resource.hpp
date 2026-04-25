#pragma once

#include "detail/type_info.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace campello::core {

// ------------------------------------------------------------------
// ResourceStorage: type-erased singleton storage with borrow tracking
// ------------------------------------------------------------------
class ResourceStorage {
public:
    ~ResourceStorage() {
        for (auto& [id, box] : resources_) {
            if (box.destruct) {
                box.destruct(box.ptr.get());
            }
        }
    }

    template<typename T, typename... Args>
    T& emplace(Args&&... args) {
        detail::TypeId id = detail::type_id<T>();
        auto it = resources_.find(id);
        if (it != resources_.end()) {
            return *reinterpret_cast<T*>(it->second.ptr.get());
        }

        ResourceBox box;
        box.ptr = allocate_aligned<T>();
        T* obj = new (box.ptr.get()) T(std::forward<Args>(args)...);
        box.destruct = [](void* p) { static_cast<T*>(p)->~T(); };
        box.borrow = std::make_unique<BorrowState>();
        resources_.emplace(id, std::move(box));
        return *obj;
    }

    template<typename T>
    T* get() {
        detail::TypeId id = detail::type_id<T>();
        auto it = resources_.find(id);
        if (it == resources_.end()) return nullptr;
        return reinterpret_cast<T*>(it->second.ptr.get());
    }

    template<typename T>
    const T* get() const {
        detail::TypeId id = detail::type_id<T>();
        auto it = resources_.find(id);
        if (it == resources_.end()) return nullptr;
        return reinterpret_cast<const T*>(it->second.ptr.get());
    }

    template<typename T>
    bool contains() const {
        return resources_.contains(detail::type_id<T>());
    }

    // Borrow tracking (debug / safety) — thread-safe via atomics
    template<typename T>
    void begin_mutable_borrow() {
        auto* state = find_borrow_state<T>();
        if (!state) return;
        [[maybe_unused]] bool expected = false;
        assert(state->mutable_borrowed.compare_exchange_strong(expected, true) &&
               "Double mutable borrow detected!");
        assert(state->immutable_borrows.load() == 0 &&
               "Mutable borrow while immutable borrows active!");
    }

    template<typename T>
    void end_mutable_borrow() {
        auto* state = find_borrow_state<T>();
        if (!state) return;
        state->mutable_borrowed.store(false);
    }

    template<typename T>
    void begin_immutable_borrow() {
        auto* state = find_borrow_state<T>();
        if (!state) return;
        assert(!state->mutable_borrowed.load() &&
               "Immutable borrow while mutable borrow active!");
        state->immutable_borrows.fetch_add(1);
    }

    template<typename T>
    void end_immutable_borrow() {
        auto* state = find_borrow_state<T>();
        if (!state) return;
        auto prev = state->immutable_borrows.fetch_sub(1);
        (void)prev;
    }

private:
    struct BorrowState {
        std::atomic<bool> mutable_borrowed{false};
        std::atomic<std::uint32_t> immutable_borrows{0};
    };

    struct ResourceBox {
        std::unique_ptr<std::byte, void(*)(std::byte*)> ptr{nullptr, [](std::byte*){}};
        void (*destruct)(void*) = nullptr;
        std::unique_ptr<BorrowState> borrow;
    };

    template<typename T>
    static std::unique_ptr<std::byte, void(*)(std::byte*)> allocate_aligned() {
        void* raw = nullptr;
        std::size_t alignment = alignof(T);
        if (alignment < sizeof(void*)) alignment = sizeof(void*);
#ifdef _WIN32
        raw = _aligned_malloc(sizeof(T), alignment);
        if (!raw) throw std::bad_alloc();
        auto deleter = [](std::byte* p) { _aligned_free(p); };
#else
        if (posix_memalign(&raw, alignment, sizeof(T)) != 0) {
            throw std::bad_alloc();
        }
        auto deleter = [](std::byte* p) { free(p); };
#endif
        return std::unique_ptr<std::byte, void(*)(std::byte*)>(
            static_cast<std::byte*>(raw), deleter);
    }

    template<typename T>
    ResourceBox* find_box() {
        auto it = resources_.find(detail::type_id<T>());
        return it != resources_.end() ? &it->second : nullptr;
    }

    template<typename T>
    const ResourceBox* find_box() const {
        auto it = resources_.find(detail::type_id<T>());
        return it != resources_.end() ? &it->second : nullptr;
    }

    template<typename T>
    BorrowState* find_borrow_state() {
        auto it = resources_.find(detail::type_id<T>());
        return (it != resources_.end() && it->second.borrow) ? it->second.borrow.get() : nullptr;
    }

    template<typename T>
    const BorrowState* find_borrow_state() const {
        auto it = resources_.find(detail::type_id<T>());
        return (it != resources_.end() && it->second.borrow) ? it->second.borrow.get() : nullptr;
    }

    std::unordered_map<detail::TypeId, ResourceBox> resources_;
};

// ------------------------------------------------------------------
// Res<T> / ResMut<T>
// ------------------------------------------------------------------
template<typename T>
class ResMut {
public:
    ResMut() = default;
    explicit ResMut(T* ptr, ResourceStorage* storage = nullptr)
        : ptr_(ptr), storage_(storage) {
        if (storage_) {
            storage_->begin_mutable_borrow<T>();
        }
    }

    ~ResMut() {
        if (storage_) {
            storage_->end_mutable_borrow<T>();
        }
    }

    ResMut(ResMut&& other) noexcept
        : ptr_(other.ptr_), storage_(other.storage_) {
        other.ptr_ = nullptr;
        other.storage_ = nullptr;
    }

    ResMut& operator=(ResMut&& other) noexcept {
        if (this != &other) {
            if (storage_) storage_->end_mutable_borrow<T>();
            ptr_ = other.ptr_;
            storage_ = other.storage_;
            other.ptr_ = nullptr;
            other.storage_ = nullptr;
        }
        return *this;
    }

    ResMut(const ResMut&) = delete;
    ResMut& operator=(const ResMut&) = delete;

    T& operator*() const {
        assert(ptr_ && "Dereferencing ResMut<T> that holds a null resource");
        return *ptr_;
    }
    T* operator->() const {
        assert(ptr_ && "Dereferencing ResMut<T> that holds a null resource");
        return ptr_;
    }
    T* get() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
    ResourceStorage* storage_ = nullptr;
};

template<typename T>
class Res {
public:
    Res() = default;
    explicit Res(const T* ptr, ResourceStorage* storage = nullptr)
        : ptr_(ptr), storage_(storage) {
        if (storage_) {
            storage_->begin_immutable_borrow<T>();
        }
    }

    ~Res() {
        if (storage_) {
            storage_->end_immutable_borrow<T>();
        }
    }

    Res(Res&& other) noexcept
        : ptr_(other.ptr_), storage_(other.storage_) {
        other.ptr_ = nullptr;
        other.storage_ = nullptr;
    }

    Res& operator=(Res&& other) noexcept {
        if (this != &other) {
            if (storage_) storage_->end_immutable_borrow<T>();
            ptr_ = other.ptr_;
            storage_ = other.storage_;
            other.ptr_ = nullptr;
            other.storage_ = nullptr;
        }
        return *this;
    }

    Res(const Res&) = delete;
    Res& operator=(const Res&) = delete;

    const T& operator*() const {
        assert(ptr_ && "Dereferencing Res<T> that holds a null resource");
        return *ptr_;
    }
    const T* operator->() const {
        assert(ptr_ && "Dereferencing Res<T> that holds a null resource");
        return ptr_;
    }
    const T* get() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    const T* ptr_ = nullptr;
    ResourceStorage* storage_ = nullptr;
};

} // namespace campello::core
