#include "audio/capture.hpp"

#include "core/log.hpp"

#include "miniaudio.h"

#include <atomic>
#include <stdexcept>

namespace mimi::audio {
namespace {
constexpr std::string_view kTag = "audio";
}

struct Capture::Impl {
    ma_context context{};
    ma_device device{};
    bool context_ready = false;
    bool device_ready = false;
    bool started = false;
    std::atomic<std::uint64_t> xruns{0};
    RingBuffer* sink = nullptr;
};

namespace {

// Realtime thread. No locks, no allocation, no logging.
void on_frames(ma_device* device, void* /*output*/, const void* input,
               ma_uint32 frame_count) {
    auto* impl = static_cast<Capture::Impl*>(device->pUserData);
    if (impl == nullptr || impl->sink == nullptr || input == nullptr) return;
    impl->sink->write(static_cast<const float*>(input), frame_count);
}

void on_notification(const ma_device_notification* note) {
    if (note == nullptr || note->pDevice == nullptr) return;
    auto* impl = static_cast<Capture::Impl*>(note->pDevice->pUserData);
    if (impl == nullptr) return;
    if (note->type == ma_device_notification_type_interruption_began) {
        impl->xruns.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace

namespace {
std::size_t ring_capacity(const Capture::Options& o) {
    const auto n = static_cast<std::size_t>(o.buffer_seconds * o.sample_rate);
    return n > 0 ? n : o.sample_rate;  // never hand RingBuffer a zero
}
}  // namespace

Capture::Capture() : Capture(Options{}) {}

Capture::Capture(Options options)
    : options_(std::move(options)),
      buffer_(ring_capacity(options_)),
      impl_(std::make_unique<Impl>()) {
    impl_->sink = &buffer_;
}

Capture::~Capture() { stop(); }

void Capture::start() {
    if (impl_->started) return;

    if (!impl_->context_ready) {
        if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
            throw std::runtime_error("could not initialise the audio context");
        }
        impl_->context_ready = true;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;
    config.capture.shareMode = ma_share_mode_shared;
    config.sampleRate = options_.sample_rate;
    config.periodSizeInFrames = options_.block_frames;
    config.periods = 3;
    config.dataCallback = on_frames;
    config.notificationCallback = on_notification;
    config.pUserData = impl_.get();

    // Resolve a named device to its miniaudio id, if one was requested.
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    ma_device_id chosen{};
    bool have_chosen = false;
    if (!options_.device_id.empty() &&
        ma_context_get_devices(&impl_->context, nullptr, nullptr, &infos, &count) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            if (options_.device_id == infos[i].name) {
                chosen = infos[i].id;
                have_chosen = true;
                break;
            }
        }
        if (!have_chosen) {
            log::warn(kTag, "no input device named '{}', using the default",
                      options_.device_id);
        }
    }
    if (have_chosen) config.capture.pDeviceID = &chosen;

    if (ma_device_init(&impl_->context, &config, &impl_->device) != MA_SUCCESS) {
        throw std::runtime_error(
            "could not open the microphone -- check System Settings > Privacy & "
            "Security > Microphone");
    }
    impl_->device_ready = true;
    device_name_ = impl_->device.capture.name;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        impl_->device_ready = false;
        throw std::runtime_error("could not start the microphone");
    }
    impl_->started = true;

    log::info(kTag, "listening on '{}' at {} Hz ({} ms blocks, {:.0f}s history)",
              device_name_, options_.sample_rate,
              options_.block_frames * 1000 / options_.sample_rate,
              options_.buffer_seconds);
}

void Capture::stop() noexcept {
    if (impl_ == nullptr) return;
    if (impl_->device_ready) {
        ma_device_uninit(&impl_->device);  // stops the device too
        impl_->device_ready = false;
        impl_->started = false;
    }
    if (impl_->context_ready) {
        ma_context_uninit(&impl_->context);
        impl_->context_ready = false;
    }
}

bool Capture::running() const noexcept { return impl_ != nullptr && impl_->started; }

std::uint64_t Capture::xruns() const noexcept {
    return impl_ == nullptr ? 0 : impl_->xruns.load(std::memory_order_relaxed);
}

std::vector<DeviceInfo> Capture::devices() {
    std::vector<DeviceInfo> out;

    ma_context context{};
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) return out;

    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, nullptr, nullptr, &infos, &count) == MA_SUCCESS) {
        out.reserve(count);
        for (ma_uint32 i = 0; i < count; ++i) {
            out.push_back({infos[i].name, infos[i].name, infos[i].isDefault != 0});
        }
    }
    ma_context_uninit(&context);
    return out;
}

}  // namespace mimi::audio
