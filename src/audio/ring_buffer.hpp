#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace mimi::audio {

// An absolute position in the stream: the count of samples captured before it.
// Never wraps in any realistic runtime (uint64 at 16 kHz ≈ 36 million years),
// so a cursor is stable and comparable for the life of the process.
using Cursor = std::uint64_t;

inline constexpr Cursor kNow = std::numeric_limits<Cursor>::max();

// Single-producer / multi-consumer circular buffer of mono float samples.
//
// The producer is the CoreAudio realtime thread, so write() takes no locks,
// allocates nothing, and never blocks -- a stall there is an audible dropout.
// Consumers hold absolute cursors instead of indices, which lets them run at
// different frame sizes (the wake word wants 1280, the VAD wants 512), fall
// behind independently, and read *backwards* into history for pre-roll.
//
// Overrun policy: a consumer that falls further behind than the capacity has
// its cursor snapped forward to the oldest surviving sample. Dropping audio
// beats blocking the thread that has to notice a wake word.
class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity);

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // --- producer (realtime thread) ---------------------------------------

    void write(const float* data, std::size_t count) noexcept;
    void write(std::span<const float> data) noexcept { write(data.data(), data.size()); }

    // --- consumers --------------------------------------------------------

    // One past the newest sample written.
    Cursor total() const noexcept { return total_.load(std::memory_order_acquire); }

    // The oldest sample still in the buffer.
    Cursor oldest() const noexcept;

    // How many samples are readable from `from` right now.
    std::size_t available(Cursor from) const noexcept;

    // Copy exactly `count` samples starting at `from`. Fails (returning false,
    // leaving `out` untouched) if that span is not entirely present.
    bool copy(Cursor from, float* out, std::size_t count) const noexcept;

    // Wait for `count` samples at `from`, copy them, and advance `from` past
    // them. Returns false on timeout, in which case `from` is only adjusted if
    // it had already been overrun.
    bool read(Cursor& from, float* out, std::size_t count,
              std::chrono::milliseconds timeout = std::chrono::milliseconds{250}) const;

    // Everything from `from` up to `to` (default: now), clamped to what is
    // still live. Allocates -- for utterance extraction, not the hot path.
    std::vector<float> slice(Cursor from, Cursor to = kNow) const;

    // The most recent `count` samples, for pre-roll. Shorter if less exists.
    std::vector<float> tail(std::size_t count) const;

    std::size_t capacity() const noexcept { return capacity_; }

    // Samples the producer overwrote in a single write() because the block was
    // larger than the whole buffer. Always 0 in practice; a canary.
    std::uint64_t clipped_writes() const noexcept {
        return clipped_.load(std::memory_order_relaxed);
    }

private:
    // Copies `count` samples at `from` with no bounds checking.
    void copy_unchecked(Cursor from, float* out, std::size_t count) const noexcept;

    std::vector<float> buf_;
    std::size_t capacity_;
    std::atomic<Cursor> total_{0};
    std::atomic<std::uint64_t> clipped_{0};
};

}  // namespace mimi::audio
