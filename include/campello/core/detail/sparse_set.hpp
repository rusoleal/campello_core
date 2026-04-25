#pragma once

#include <vector>
#include <cstdint>
#include <type_traits>
#include <limits>

namespace campello::core::detail {

// Generic sparse set for O(1) add/remove/contains and dense iteration.
// Key is expected to be an integer type (e.g. Entity).
template<typename Key, typename Value = void>
class SparseSet {
public:
    static_assert(std::is_unsigned_v<Key>, "SparseSet Key must be unsigned integer");

    using key_type = Key;
    using value_type = Value;
    static constexpr key_type invalid_key = std::numeric_limits<key_type>::max();

    bool contains(key_type key) const {
        std::size_t page = key / page_size;
        std::size_t offset = key % page_size;
        if (page >= sparse_.size()) return false;
        if (sparse_[page] == nullptr) return false;
        return sparse_[page][offset] != invalid_key;
    }

    // Returns pointer to value, or nullptr if not present.
    Value* get(key_type key) {
        std::size_t page = key / page_size;
        std::size_t offset = key % page_size;
        if (page >= sparse_.size()) return nullptr;
        if (sparse_[page] == nullptr) return nullptr;
        key_type dense_idx = sparse_[page][offset];
        if (dense_idx == invalid_key) return nullptr;
        if constexpr (!std::is_void_v<Value>) {
            return &dense_values_[dense_idx];
        } else {
            return reinterpret_cast<Value*>(this); // non-void fallback, shouldn't happen
        }
    }

    const Value* get(key_type key) const {
        return const_cast<SparseSet*>(this)->get(key);
    }

    template<typename... Args>
    Value& emplace(key_type key, Args&&... args) {
        if (contains(key)) {
            return *get(key);
        }
        std::size_t page = key / page_size;
        std::size_t offset = key % page_size;
        if (page >= sparse_.size()) {
            sparse_.resize(page + 1, nullptr);
        }
        if (sparse_[page] == nullptr) {
            sparse_[page] = new key_type[page_size];
            for (std::size_t i = 0; i < page_size; ++i) {
                sparse_[page][i] = invalid_key;
            }
        }

        key_type dense_idx = static_cast<key_type>(dense_keys_.size());
        sparse_[page][offset] = dense_idx;
        dense_keys_.push_back(key);
        if constexpr (!std::is_void_v<Value>) {
            dense_values_.emplace_back(std::forward<Args>(args)...);
            return dense_values_.back();
        }
    }

    void remove(key_type key) {
        if (!contains(key)) return;
        std::size_t page = key / page_size;
        std::size_t offset = key % page_size;
        key_type dense_idx = sparse_[page][offset];
        key_type last_dense = static_cast<key_type>(dense_keys_.size() - 1);
        key_type last_key = dense_keys_[last_dense];

        // Swap with last
        dense_keys_[dense_idx] = last_key;
        std::size_t last_page = last_key / page_size;
        std::size_t last_off = last_key % page_size;
        sparse_[last_page][last_off] = dense_idx;

        if constexpr (!std::is_void_v<Value>) {
            dense_values_[dense_idx] = std::move(dense_values_[last_dense]);
            dense_values_.pop_back();
        }

        dense_keys_.pop_back();
        sparse_[page][offset] = invalid_key;
    }

    void clear() {
        for (auto* page : sparse_) {
            delete[] page;
        }
        sparse_.clear();
        dense_keys_.clear();
        if constexpr (!std::is_void_v<Value>) {
            dense_values_.clear();
        }
    }

    std::size_t size() const { return dense_keys_.size(); }
    bool empty() const { return dense_keys_.empty(); }

    // Iteration over dense set
    const key_type* keys_begin() const { return dense_keys_.data(); }
    const key_type* keys_end() const { return dense_keys_.data() + dense_keys_.size(); }

    key_type* keys_begin() { return dense_keys_.data(); }
    key_type* keys_end() { return dense_keys_.data() + dense_keys_.size(); }

    Value* values_begin() { return dense_values_.data(); }
    Value* values_end() { return dense_values_.data() + dense_values_.size(); }

    const Value* values_begin() const { return dense_values_.data(); }
    const Value* values_end() const { return dense_values_.data() + dense_values_.size(); }

    ~SparseSet() {
        clear();
    }

private:
    static constexpr std::size_t page_size = 4096;
    std::vector<key_type*> sparse_;
    std::vector<key_type> dense_keys_;
    std::vector<Value> dense_values_;
};

// Specialization for tag-only (no value) sparse set
template<typename Key>
class SparseSet<Key, void> {
public:
    static_assert(std::is_unsigned_v<Key>, "SparseSet Key must be unsigned integer");

    using key_type = Key;
    static constexpr key_type invalid_key = std::numeric_limits<key_type>::max();

    bool contains(key_type key) const {
        std::size_t page = key / page_size;
        std::size_t offset = key % page_size;
        if (page >= sparse_.size()) return false;
        if (sparse_[page] == nullptr) return false;
        return sparse_[page][offset] != invalid_key;
    }

    void insert(key_type key) {
        if (contains(key)) return;
        std::size_t page = key / page_size;
        std::size_t offset = key % page_size;
        if (page >= sparse_.size()) {
            sparse_.resize(page + 1, nullptr);
        }
        if (sparse_[page] == nullptr) {
            sparse_[page] = new key_type[page_size];
            for (std::size_t i = 0; i < page_size; ++i) {
                sparse_[page][i] = invalid_key;
            }
        }
        sparse_[page][offset] = static_cast<key_type>(dense_.size());
        dense_.push_back(key);
    }

    void remove(key_type key) {
        if (!contains(key)) return;
        std::size_t page = key / page_size;
        std::size_t offset = key % page_size;
        key_type dense_idx = sparse_[page][offset];
        key_type last_key = dense_.back();
        dense_[dense_idx] = last_key;
        std::size_t last_page = last_key / page_size;
        std::size_t last_off = last_key % page_size;
        sparse_[last_page][last_off] = dense_idx;
        dense_.pop_back();
        sparse_[page][offset] = invalid_key;
    }

    void clear() {
        for (auto* page : sparse_) {
            delete[] page;
        }
        sparse_.clear();
        dense_.clear();
    }

    std::size_t size() const { return dense_.size(); }
    bool empty() const { return dense_.empty(); }

    const key_type* begin() const { return dense_.data(); }
    const key_type* end() const { return dense_.data() + dense_.size(); }

    key_type* begin() { return dense_.data(); }
    key_type* end() { return dense_.data() + dense_.size(); }

    ~SparseSet() { clear(); }

private:
    static constexpr std::size_t page_size = 4096;
    std::vector<key_type*> sparse_;
    std::vector<key_type> dense_;
};

} // namespace campello::core::detail
