#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <limits>

namespace campello::core::detail {

// Arena allocator for archetype chunks.
// Allocates blocks from a pre-reserved pool. Individual chunks
// are not freed until the entire arena is destroyed or reset.
class ArenaAllocator {
public:
    explicit ArenaAllocator(std::size_t block_size = 64 * 1024)
        : block_size_(block_size) {}

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    ArenaAllocator(ArenaAllocator&& other) noexcept
        : blocks_(std::move(other.blocks_))
        , current_block_(other.current_block_)
        , offset_(other.offset_)
        , block_size_(other.block_size_)
        , allocated_bytes_(other.allocated_bytes_) {
        other.current_block_ = nullptr;
        other.offset_ = 0;
        other.allocated_bytes_ = 0;
    }

    ArenaAllocator& operator=(ArenaAllocator&& other) noexcept {
        if (this != &other) {
            reset();
            blocks_ = std::move(other.blocks_);
            current_block_ = other.current_block_;
            offset_ = other.offset_;
            block_size_ = other.block_size_;
            allocated_bytes_ = other.allocated_bytes_;
            other.current_block_ = nullptr;
            other.offset_ = 0;
            other.allocated_bytes_ = 0;
        }
        return *this;
    }

    ~ArenaAllocator() {
        reset();
    }

    // Allocate `size` bytes aligned to `alignment`.
    void* allocate(std::size_t size, std::size_t alignment) {
        std::size_t padding = (alignment - (offset_ % alignment)) % alignment;
        if (current_block_ == nullptr || offset_ + padding + size > block_size_) {
            allocate_block(std::max(block_size_, size + alignment));
            padding = (alignment - (offset_ % alignment)) % alignment;
        }

        std::byte* ptr = current_block_ + offset_ + padding;
        offset_ += padding + size;
        allocated_bytes_ += padding + size;
        return ptr;
    }

    template<typename T, typename... Args>
    T* construct(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    void reset() {
        for (auto* block : blocks_) {
            ::operator delete[](block, std::align_val_t{alignof(std::max_align_t)});
        }
        blocks_.clear();
        current_block_ = nullptr;
        offset_ = 0;
        allocated_bytes_ = 0;
    }

    std::size_t allocated_bytes() const { return allocated_bytes_; }

private:
    void allocate_block(std::size_t size) {
        auto* block = static_cast<std::byte*>(::operator new[](size, std::align_val_t{alignof(std::max_align_t)}));
        blocks_.push_back(block);
        current_block_ = block;
        offset_ = 0;
    }

    std::vector<std::byte*> blocks_;
    std::byte* current_block_ = nullptr;
    std::size_t offset_ = 0;
    std::size_t block_size_ = 64 * 1024;
    std::size_t allocated_bytes_ = 0;
};

} // namespace campello::core::detail
