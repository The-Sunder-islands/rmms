#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <utility>

namespace rmms::backend::transport {

template <typename T, size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    RingBuffer() : m_write_pos(0), m_read_pos(0) {}

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    bool push(const T& item) {
        size_t w = m_write_pos.load(std::memory_order_relaxed);
        size_t next = (w + 1) & kMask;

        if (next == m_read_pos.load(std::memory_order_acquire))
            return false;

        m_buffer[w & kMask] = item;
        m_write_pos.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t r = m_read_pos.load(std::memory_order_relaxed);

        if (r == m_write_pos.load(std::memory_order_acquire))
            return false;

        item = m_buffer[r & kMask];
        m_read_pos.store(r + 1, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return m_read_pos.load(std::memory_order_acquire) ==
               m_write_pos.load(std::memory_order_acquire);
    }

    bool full() const {
        size_t w = m_write_pos.load(std::memory_order_relaxed);
        return ((w + 1) & kMask) == m_read_pos.load(std::memory_order_acquire);
    }

    size_t capacity() const { return Capacity; }

    size_t used() const {
        size_t w = m_write_pos.load(std::memory_order_acquire);
        size_t r = m_read_pos.load(std::memory_order_acquire);
        if (w >= r) return w - r;
        return Capacity - (r - w);
    }

private:
    static constexpr size_t kMask = Capacity - 1;

    std::array<T, Capacity>        m_buffer;
    std::atomic<size_t>            m_write_pos;
    std::atomic<size_t>            m_read_pos;
};

}  // namespace rmms::backend::transport
