// mimi_wake -- run the wake-word frontend over a WAV and, optionally, check it
// against openWakeWord's own scores.
//
//   mimi_wake testdata/hey_jarvis.wav
//   mimi_wake testdata/hey_jarvis.wav --compare testdata/ref_hey_jarvis.tsv
//   mimi_wake --live                       (default mic, prints live scores)
//
// The --compare mode is the real test: the C++ frontend has to reproduce
// openWakeWord's melspectrogram framing and buffer geometry, and a silent
// mismatch there degrades detection without ever looking broken.

#include "audio/capture.hpp"
#include "audio/wav.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "voice/wake_word.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true); }

// openWakeWord primes its feature buffer with random noise, so its first
// frames are unreproducible by construction. Once 16 real embeddings have
// landed the window holds only genuine audio and the two must agree.
constexpr int kSettleFrames = 16;

mimi::voice::WakeWord::Config default_config(const std::vector<std::string>& phrases) {
    mimi::voice::WakeWord::Config config;
    config.melspec_model = mimi::paths::models_dir() / "melspectrogram.onnx";
    config.embedding_model = mimi::paths::models_dir() / "embedding_model.onnx";
    for (const auto& p : phrases) {
        config.classifiers.push_back(mimi::paths::models_dir() / (p + "_v0.1.onnx"));
    }
    return config;
}

std::vector<float> load_reference(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);

    std::vector<float> scores;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("frame", 0) == 0) continue;
        std::istringstream parts(line);
        int frame = 0;
        float score = 0.0f;
        if (parts >> frame >> score) scores.push_back(score);
    }
    return scores;
}

int run_file(const std::string& wav_path, const std::vector<std::string>& phrases,
             const std::optional<std::string>& reference_path) {
    const auto wav = mimi::audio::read_wav(wav_path);
    if (wav.sample_rate != mimi::audio::kSampleRate) {
        std::fprintf(stderr, "error: need %u Hz audio, %s is %u Hz\n",
                     mimi::audio::kSampleRate, wav_path.c_str(), wav.sample_rate);
        return 1;
    }

    auto config = default_config(phrases);
    config.threshold = 0.5f;
    config.refractory = std::chrono::milliseconds{0};  // score every frame
    mimi::voice::WakeWord wake(std::move(config));

    const std::size_t frames = wav.samples.size() / mimi::voice::WakeWord::kFrame;
    std::vector<float> mine;
    mine.reserve(frames);

    for (std::size_t i = 0; i < frames; ++i) {
        wake.push(wav.samples.data() + i * mimi::voice::WakeWord::kFrame);
        mine.push_back(wake.last_scores().front());
    }

    if (!reference_path) {
        std::printf("\n%s  (%zu frames)\n\n", wav_path.c_str(), frames);
        for (std::size_t i = 0; i < mine.size(); ++i) {
            std::printf("%3zu  %.6f%s\n", i, mine[i], mine[i] > 0.5f ? "   <== DETECT" : "");
        }
        return 0;
    }

    const auto reference = load_reference(*reference_path);
    const std::size_t n = std::min(mine.size(), reference.size());
    if (n == 0) {
        std::fputs("error: no overlapping frames to compare\n", stderr);
        return 1;
    }

    std::printf("\n%-6s %10s %10s %10s\n", "frame", "c++", "python", "delta");
    std::printf("%s\n", std::string(42, '-').c_str());

    double worst = 0.0;
    double worst_settled = 0.0;
    std::size_t worst_frame = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double delta = std::fabs(static_cast<double>(mine[i]) - reference[i]);
        worst = std::max(worst, delta);
        if (static_cast<int>(i) >= kSettleFrames && delta > worst_settled) {
            worst_settled = delta;
            worst_frame = i;
        }
        const bool interesting = mine[i] > 0.05f || reference[i] > 0.05f || delta > 1e-4;
        if (interesting) {
            std::printf("%-6zu %10.6f %10.6f %10.2e%s\n", i, mine[i], reference[i], delta,
                        static_cast<int>(i) < kSettleFrames ? "  (priming)" : "");
        }
    }

    std::printf("\ncompared %zu frames, first %d are settling\n", n, kSettleFrames);
    std::printf("max delta overall        : %.3e\n", worst);
    std::printf("max delta after settling : %.3e  (frame %zu)\n", worst_settled, worst_frame);

    // float32 inference across two runtimes will not be bit-identical; anything
    // this small cannot move a 0.5 threshold.
    constexpr double kTolerance = 1e-4;
    const bool pass = worst_settled < kTolerance;
    std::printf("\n%s  (tolerance %.0e)\n", pass ? "PASS -- matches openWakeWord" : "FAIL",
                kTolerance);
    return pass ? 0 : 1;
}

int run_live(const std::vector<std::string>& phrases, float threshold) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    auto config = default_config(phrases);
    config.threshold = threshold;
    mimi::voice::WakeWord wake(std::move(config));

    mimi::audio::Capture capture;
    capture.start();

    std::printf("\nsay one of:");
    for (const auto& n : wake.names()) std::printf("  \"%s\"", n.c_str());
    std::printf("\nctrl-c to stop\n\n");

    auto cursor = capture.now();
    std::vector<float> frame(mimi::voice::WakeWord::kFrame);

    while (!g_stop.load()) {
        if (!capture.buffer().read(cursor, frame.data(), frame.size(),
                                   std::chrono::milliseconds{200})) {
            continue;
        }
        const auto hit = wake.push(frame.data());
        const float top =
            *std::max_element(wake.last_scores().begin(), wake.last_scores().end());

        const int filled = static_cast<int>(std::lround(top * 40.0f));
        std::string bar(static_cast<std::size_t>(std::clamp(filled, 0, 40)), '#');
        std::printf("\r%.3f |%-40s|%s", top, bar.c_str(), hit ? "  DETECTED" : "          ");
        std::fflush(stdout);
        if (hit) std::printf("\n>>> %s (%.3f)\n", hit->name.c_str(), hit->score);
    }
    std::printf("\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    std::vector<std::string> phrases{"hey_jarvis"};
    std::optional<std::string> wav_path;
    std::optional<std::string> reference_path;
    bool live = false;
    float threshold = 0.5f;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--live") live = true;
        else if (arg == "--compare" && i + 1 < argc) reference_path = argv[++i];
        else if (arg == "--phrase" && i + 1 < argc) phrases = {argv[++i]};
        else if (arg == "--threshold" && i + 1 < argc) threshold = std::stof(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            std::puts("usage: mimi_wake [WAV | --live] [--compare TSV] "
                      "[--phrase NAME] [--threshold F]");
            return 0;
        } else if (!arg.empty() && arg[0] != '-') wav_path = arg;
    }

    try {
        if (live) return run_live(phrases, threshold);
        if (!wav_path) {
            std::fputs("error: give a WAV file or --live\n", stderr);
            return 2;
        }
        return run_file(*wav_path, phrases, reference_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
