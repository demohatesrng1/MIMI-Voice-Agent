#pragma once

#include "audio/ring_buffer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mimi::audio {

inline constexpr std::uint32_t kSampleRate = 16000;  // what every model wants
inline constexpr std::uint32_t kBlockFrames = 512;   // 32 ms per callback

inline constexpr std::size_t samples_for(double seconds) noexcept {
    return static_cast<std::size_t>(seconds * kSampleRate);
}
inline constexpr double seconds_for(std::size_t samples) noexcept {
    return static_cast<double>(samples) / kSampleRate;
}

struct DeviceInfo {
    std::string id;
    std::string name;
    bool is_default = false;
};

// Owns the microphone for the life of the process.
//
// The device is opened once and never closed mid-conversation, which is what
// makes always-on listening possible: there is no window where Mimi is deaf,
// and no lock for a push-to-talk request to contend with. miniaudio resamples
// the hardware rate (usually 48 kHz) down to 16 kHz for us.
class Capture {
public:
    struct Options {
        std::uint32_t sample_rate = kSampleRate;
        std::uint32_t block_frames = kBlockFrames;
        double buffer_seconds = 30.0;  // how far back consumers may reach
        std::string device_id;         // empty = system default
    };

    // Opaque; defined in capture.cpp so miniaudio.h stays out of this header.
    // Public only because the realtime callbacks need to name it.
    struct Impl;

    Capture();
    explicit Capture(Options options);
    ~Capture();

    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

    // Throws std::runtime_error if the device cannot be opened. On macOS the
    // first call triggers the microphone permission prompt.
    void start();
    void stop() noexcept;
    bool running() const noexcept;

    RingBuffer& buffer() noexcept { return buffer_; }
    const RingBuffer& buffer() const noexcept { return buffer_; }

    // A cursor meaning "this instant".
    Cursor now() const noexcept { return buffer_.total(); }

    const std::string& device_name() const noexcept { return device_name_; }
    std::uint32_t sample_rate() const noexcept { return options_.sample_rate; }

    // Callbacks the device reported as over/underrun. Non-zero means the
    // machine is struggling and audio was lost before it reached us.
    std::uint64_t xruns() const noexcept;

    static std::vector<DeviceInfo> devices();

private:
    Options options_;
    RingBuffer buffer_;
    std::string device_name_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mimi::audio
