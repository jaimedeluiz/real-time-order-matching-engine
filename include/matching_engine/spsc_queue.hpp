#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>
#include <vector>

namespace matching {

#if defined(__cpp_lib_hardware_interference_size)
    constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    constexpr size_t CACHE_LINE_SIZE = 64;
#endif

// High-throughput, wait-free, cache-conscious Single-Producer Single-Consumer (SPSC) Ring Buffer.
// Decouples network/gateway ingest thread from the deterministic single-threaded matching core.
// Capacity must be a power of 2.
template <typename T, size_t Capacity = 65536>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

private:
    struct PaddedTail {
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> value{0};
    } tail_;

    struct PaddedHead {
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> value{0};
    } head_;

    // Buffer storage
    alignas(CACHE_LINE_SIZE) T buffer_[Capacity];

    static constexpr size_t MASK = Capacity - 1;

public:
    SPSCQueue() = default;
    ~SPSCQueue() = default;

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Called only by Producer thread
    template <typename... Args>
    bool emplace(Args&&... args) {
        const size_t current_tail = tail_.value.load(std::memory_order_relaxed);
        const size_t current_head = head_.value.load(std::memory_order_acquire);

        if ((current_tail - current_head) >= Capacity) {
            return false; // Queue full
        }

        buffer_[current_tail & MASK] = T(std::forward<Args>(args)...);
        tail_.value.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    bool push(const T& item) {
        return emplace(item);
    }

    bool push(T&& item) {
        return emplace(std::move(item));
    }

    // Called only by Consumer thread
    bool pop(T& item) {
        const size_t current_head = head_.value.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.value.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return false; // Queue empty
        }

        item = std::move(buffer_[current_head & MASK]);
        head_.value.store(current_head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.value.load(std::memory_order_relaxed) == tail_.value.load(std::memory_order_relaxed);
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t h = head_.value.load(std::memory_order_relaxed);
        const size_t t = tail_.value.load(std::memory_order_relaxed);
        return (t >= h) ? (t - h) : (Capacity - (h - t));
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept {
        return Capacity;
    }
};

} // namespace matching
