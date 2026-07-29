// mimi_mic -- prove the capture path works before anything depends on it.
//
//   mimi_mic --list          show input devices
//   mimi_mic                 live level meter from the default device
//   mimi_mic --device "..."  meter a specific device
//
// Also exercises the ring buffer the way the voice pipeline will: a consumer
// pulling fixed 80 ms frames on its own cursor while the realtime thread writes.

#include "audio/capture.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true); }

constexpr std::size_t kFrame = 1280;  // 80 ms at 16 kHz -- openWakeWord's frame

int list_devices() {
    const auto devices = mimi::audio::Capture::devices();
    if (devices.empty()) {
        std::puts("no input devices found");
        return 1;
    }
    std::puts("input devices:");
    for (const auto& d : devices) {
        std::printf("  %s%s\n", d.name.c_str(), d.is_default ? "  (default)" : "");
    }
    return 0;
}

void draw_meter(float peak, float rms, std::size_t frames, std::uint64_t xruns) {
    // dBFS is a far better fit than a linear bar: speech sits around -30..-6 dB
    // and a linear meter makes all of it look like silence.
    const float db = peak > 1e-6f ? 20.0f * std::log10(peak) : -90.0f;
    const int filled = static_cast<int>((std::max(db, -60.0f) + 60.0f) / 60.0f * 46.0f);

    char bar[47];
    std::memset(bar, ' ', sizeof(bar));
    bar[46] = '\0';
    for (int i = 0; i < filled && i < 46; ++i) bar[i] = '#';

    std::printf("\r%6.1f dB |%s| rms %5.3f  %5zu frames  xrun %llu",
                db, bar, rms, frames, static_cast<unsigned long long>(xruns));
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    mimi::audio::Capture::Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list") return list_devices();
        if (arg == "--device" && i + 1 < argc) options.device_id = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::puts("usage: mimi_mic [--list] [--device NAME]");
            return 0;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    mimi::audio::Capture capture(options);
    try {
        capture.start();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::puts("speak into the mic -- ctrl-c to stop\n");

    // Start at "now" rather than 0 so we meter live audio, not buffered history.
    mimi::audio::Cursor cursor = capture.now();
    std::vector<float> frame(kFrame);
    std::size_t frames_read = 0;

    while (!g_stop.load()) {
        if (!capture.buffer().read(cursor, frame.data(), frame.size(),
                                   std::chrono::milliseconds{200})) {
            continue;  // timeout: nothing new yet
        }
        ++frames_read;

        float peak = 0.0f;
        double sum_squares = 0.0;
        for (float s : frame) {
            peak = std::max(peak, std::fabs(s));
            sum_squares += static_cast<double>(s) * s;
        }
        const auto rms = static_cast<float>(std::sqrt(sum_squares / frame.size()));

        if (frames_read % 3 == 0) draw_meter(peak, rms, frames_read, capture.xruns());
    }

    capture.stop();
    std::printf("\n\n%zu frames (%.1fs), %llu xruns, %llu clipped writes\n",
                frames_read, mimi::audio::seconds_for(frames_read * kFrame),
                static_cast<unsigned long long>(capture.xruns()),
                static_cast<unsigned long long>(capture.buffer().clipped_writes()));
    return 0;
}
