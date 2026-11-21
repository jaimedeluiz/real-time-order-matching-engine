#pragma once

#include <cstddef>
#include <vector>
#include <new>
#include <utility>
#include <cassert>

namespace matching {

// High-performance, zero-allocation-on-hot-path object pool.
// Pre-allocates blocks of memory in contiguous slabs.
// Acquires and releases objects in O(1) time without calling system malloc/free.
// Ensures spatial cache locality and eliminates heap lock contention.
template <typename T, size_t BlockSize = 4096>
class MemoryPool {
private:
    union Node {
        alignas(alignof(T)) char storage[sizeof(T)];
        Node* next;
    };

    struct Block {
        Node nodes[BlockSize];
        Block* next{nullptr};
    };

    Block* first_block_{nullptr};
    Node* free_list_{nullptr};
    size_t allocated_count_{0};

    void allocate_new_block() {
        Block* new_block = new Block();
        new_block->next = first_block_;
        first_block_ = new_block;

        for (size_t i = 0; i < BlockSize - 1; ++i) {
            new_block->nodes[i].next = &(new_block->nodes[i + 1]);
        }
        new_block->nodes[BlockSize - 1].next = free_list_;
        free_list_ = &(new_block->nodes[0]);
    }

public:
    explicit MemoryPool(size_t initial_blocks = 4) {
        for (size_t i = 0; i < initial_blocks; ++i) {
            allocate_new_block();
        }
    }

    ~MemoryPool() {
        Block* current = first_block_;
        while (current) {
            Block* next = current->next;
            delete current;
            current = next;
        }
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    template <typename... Args>
    [[nodiscard]] T* allocate(Args&&... args) {
        if (!free_list_) {
            allocate_new_block();
        }

        Node* node = free_list_;
        free_list_ = free_list_->next;
        allocated_count_++;

        T* ptr = reinterpret_cast<T*>(node->storage);
        return new (ptr) T(std::forward<Args>(args)...);
    }

    void deallocate(T* ptr) noexcept {
        if (!ptr) return;
        ptr->~T();
        Node* node = reinterpret_cast<Node*>(ptr);
        node->next = free_list_;
        free_list_ = node;
        allocated_count_--;
    }

    [[nodiscard]] size_t active_count() const noexcept {
        return allocated_count_;
    }
};

} // namespace matching
