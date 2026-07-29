#include "audio/ring_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <thread>

namespace mimi::audio {

RingBuffer::RingBuffer(std::size_t capacity)
    : buf_(capacity == 0 ? 1 : capacity, 0.0f),
      capacity_(capacity == 0 ? 1 : capacity) {}

void RingBuffer::write(const float* data, std::size_t count) noexcept {
    if (data == nullptr || count == 0) return;

    // A block bigger than the entire ring can only leave its tail behind.
    if (count > capacity_) {
        data += (count - capacity_);
        count = capacity_;
        clipped_.fetch_add(1, std::memory_order_relaxed);
    }

    const Cursor total = total_.load(std::memory_order_relaxed);
    const std::size_t head = static_cast<std::size_t>(total % capacity_);
    const std::size_t first = std::min(count, capacity_ - head);

    std::memcpy(buf_.data() + head, data, first * sizeof(float));
    if (first < count) {
        std::memcpy(buf_.data(), data + first, (count - first) * sizeof(float));
    }

    // Release: consumers that acquire this value see the samples above.
    total_.store(total + count, std::memory_order_release);
}

Cursor RingBuffer::oldest() const noexcept {
    const Cursor total = total_.load(std::memory_order_acquire);
    return total > capacity_ ? total - capacity_ : 0;
}

std::size_t RingBuffer::available(Cursor from) const noexcept {
    const Cursor total = total_.load(std::memory_order_acquire);
    if (from >= total) return 0;
    const Cursor floor = total > capacity_ ? total - capacity_ : 0;
    return static_cast<std::size_t>(total - std::max(from, floor));
}

void RingBuffer::copy_unchecked(Cursor from, float* out, std::size_t count) const noexcept {
    const std::size_t start = static_cast<std::size_t>(from % capacity_);
    const std::size_t first = std::min(count, capacity_ - start);
    std::memcpy(out, buf_.data() + start, first * sizeof(float));
    if (first < count) {
        std::memcpy(out + first, buf_.data(), (count - first) * sizeof(float));
    }
}

bool RingBuffer::copy(Cursor from, float* out, std::size_t count) const noexcept {
    if (out == nullptr) return false;
    if (count == 0) return true;

    const Cursor total = total_.load(std::memory_order_acquire);
    const Cursor floor = total > capacity_ ? total - capacity_ : 0;
    if (from < floor || from + count > total) return false;

    copy_unchecked(from, out, count);
    return true;
}

bool RingBuffer::read(Cursor& from, float* out, std::size_t count,
                      std::chrono::milliseconds timeout) const {
    if (out == nullptr) return false;
    if (count == 0) return true;

    // The producer is a realtime thread, so it must not signal a condition
    // variable. Consumers poll instead; at an 80 ms frame cadence a 1 ms tick
    // is free, and it keeps the audio callback lock-free.
    constexpr auto kTick = std::chrono::milliseconds{1};
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        const Cursor total = total_.load(std::memory_order_acquire);
        const Cursor floor = total > capacity_ ? total - capacity_ : 0;
        if (from < floor) from = floor;  // overrun: give up on the stale span

        if (total - from >= count) {
            copy_unchecked(from, out, count);
            from += count;
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(kTick);
    }
}

std::vector<float> RingBuffer::slice(Cursor from, Cursor to) const {
    const Cursor total = total_.load(std::memory_order_acquire);
    const Cursor floor = total > capacity_ ? total - capacity_ : 0;

    const Cursor begin = std::max(from, floor);
    const Cursor end = std::min(to == kNow ? total : to, total);
    if (end <= begin) return {};

    std::vector<float> out(static_cast<std::size_t>(end - begin));
    copy_unchecked(begin, out.data(), out.size());
    return out;
}

std::vector<float> RingBuffer::tail(std::size_t count) const {
    const Cursor total = total_.load(std::memory_order_acquire);
    const Cursor begin = total > count ? total - count : 0;
    return slice(begin, total);
}

}  // namespace mimi::audio
