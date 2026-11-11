#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace tactus
{

/** Wait-free single-producer / single-consumer ring buffer.

    This is the only channel between the UI thread and the audio thread.
    push() and pop() never allocate, lock or block. Storage is allocated once,
    in the constructor. T must be trivially copyable so that moving an item
    in or out can never run user code on the audio thread.
*/
#ifdef _MSC_VER
 #pragma warning (push)
 #pragma warning (disable : 4324) // padding from alignas is the whole point here
#endif

template <typename T>
class SpscQueue
{
    static_assert (std::is_trivially_copyable_v<T>, "SpscQueue items must be trivially copyable");

public:
    /** capacity is rounded up to a power of two; one slot is kept free. */
    explicit SpscQueue (std::size_t capacity)
    {
        std::size_t size = 2;
        while (size < capacity + 1)
            size <<= 1;
        buffer.resize (size);
        mask = size - 1;
    }

    /** Producer thread only. Returns false if the queue is full. */
    bool push (const T& item) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto next = (w + 1) & mask;
        if (next == readIndex.load (std::memory_order_acquire))
            return false;
        buffer[w] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    /** Consumer thread only. Returns false if the queue is empty. */
    bool pop (T& out) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire))
            return false;
        out = buffer[r];
        readIndex.store ((r + 1) & mask, std::memory_order_release);
        return true;
    }

private:
    std::vector<T> buffer;
    std::size_t mask = 0;
    alignas (64) std::atomic<std::size_t> writeIndex { 0 };
    alignas (64) std::atomic<std::size_t> readIndex { 0 };
};

#ifdef _MSC_VER
 #pragma warning (pop)
#endif

} // namespace tactus
