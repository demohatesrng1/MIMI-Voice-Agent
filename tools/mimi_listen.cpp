// mimi_listen -- the whole voice pipeline, end to end, without a GUI.
//
//   mimi_listen                      always-on: wake word -> endpoint -> text
//   mimi_listen --once               push-to-talk: one utterance, no wake word
//   mimi_listen --file clip.wav      transcribe a file (checks whisper alone)
//
// Prints a live state line so the parts that are normally invisible -- what the
// wake score is doing, when the endpointer decides you stopped -- can be
// watched while speaking.

#include "audio/capture.hpp"
#include "audio/wav.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "voice/listener.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true); }

mimi::voice::Listener::Config default_config(const std::string& phrase,
                                             bool use_openwakeword,
                                             const std::string& language) {
    mimi::voice::Listener::Config config;
    const auto& models = mimi::paths::models_dir();
    config.vad_model = models / "silero_vad.onnx";
    config.language = language;

    if (use_openwakeword) {
        config.wake_backend = mimi::voice::Listener::WakeBackend::OpenWakeWord;
        config.melspec_model = models / "melspectrogram.onnx";
        config.embedding_model = models / "embedding_model.onnx";
        config.wake_classifiers = {models / (phrase + "_v0.1.onnx")};
        config.whisper_model = models / "ggml-small.en.bin";
    } else {
        config.wake_backend = mimi::voice::Listener::WakeBackend::PhraseSpotter;
        // Empty for Japanese: ggml-tiny cannot spot the wake word in ja.
        if (language == "en") config.gate_model = models / "ggml-tiny.bin";
        config.whisper_model = models / "ggml-small.bin";
    }
    return config;
}

const char* icon_for(mimi::voice::State state) {
    switch (state) {
        case mimi::voice::State::Idle:      return ".";
        case mimi::voice::State::Listening: return "*";
        case mimi::voice::State::Thinking:  return "?";
        case mimi::voice::State::Speaking:  return "!";
        case mimi::voice::State::Paused:    return "-";
    }
    return " ";
}

// Runs Silero over a file and prints what the endpointer would decide. The
// quickest way to tell a genuinely quiet recording apart from a broken VAD.
int dump_vad(const std::string& path) {
    const auto wav = mimi::audio::read_wav(path);
    if (wav.sample_rate != mimi::audio::kSampleRate) {
        std::fprintf(stderr, "error: need 16 kHz audio, got %u Hz\n", wav.sample_rate);
        return 1;
    }
    mimi::voice::SileroVad vad(mimi::paths::models_dir() / "silero_vad.onnx");
    mimi::voice::Endpointer endpointer;

    constexpr auto kWindow = mimi::voice::SileroVad::kWindow;
    constexpr auto kWindowMs =
        std::chrono::milliseconds{kWindow * 1000 / mimi::audio::kSampleRate};

    std::printf("\n%s  (%.2fs, %zu windows of %zu)\n\n", path.c_str(),
                mimi::audio::seconds_for(wav.samples.size()), wav.samples.size() / kWindow,
                kWindow);
    std::printf("%8s %6s  %s\n", "time", "p", "speech");

    float peak = 0.0f;
    std::size_t above = 0;
    for (std::size_t i = 0; i + kWindow <= wav.samples.size(); i += kWindow) {
        const float p = vad.probability(wav.samples.data() + i);
        peak = std::max(peak, p);
        if (p >= 0.5f) ++above;
        const auto event = endpointer.push(p, kWindowMs);

        const int bars = static_cast<int>(std::lround(p * 30.0f));
        std::string bar(static_cast<std::size_t>(std::clamp(bars, 0, 30)), '#');
        const char* marker = "";
        switch (event) {
            case mimi::voice::Endpointer::Event::SpeechStart: marker = "  <- start"; break;
            case mimi::voice::Endpointer::Event::SpeechEnd:   marker = "  <- END";   break;
            case mimi::voice::Endpointer::Event::NoSpeech:    marker = "  <- gave up"; break;
            case mimi::voice::Endpointer::Event::MaxDuration: marker = "  <- max";   break;
            default: break;
        }
        std::printf("%7.2fs %6.3f  %-30s%s\n", mimi::audio::seconds_for(i), p, bar.c_str(),
                    marker);
    }
    std::printf("\npeak probability %.3f, %zu windows over 0.5\n", peak, above);
    return peak >= 0.5f ? 0 : 1;
}

int transcribe_file(const std::string& path, const std::string& language,
                    const std::string& model) {
    const auto wav = mimi::audio::read_wav(path);
    if (wav.sample_rate != mimi::audio::kSampleRate) {
        std::fprintf(stderr, "error: need 16 kHz audio, got %u Hz\n", wav.sample_rate);
        return 1;
    }
    mimi::voice::Transcriber::Config config;
    config.model_path = mimi::paths::models_dir() / model;
    config.language = language;
    if (language != "en") config.initial_prompt.clear();
    mimi::voice::Transcriber stt(std::move(config));

    const auto result = stt.transcribe(wav.samples);
    std::printf("\n%.2fs audio -> \"%s\"\n", mimi::audio::seconds_for(wav.samples.size()),
                result.text.c_str());
    std::printf("confidence %.0f%%, decoded in %lld ms (%.1fx realtime)\n",
                result.confidence * 100.0f, static_cast<long long>(result.took.count()),
                result.took.count() > 0
                    ? mimi::audio::seconds_for(wav.samples.size()) * 1000.0 / result.took.count()
                    : 0.0);
    return result.empty() ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    std::string phrase = "hey_jarvis";
    std::string file;
    std::string vad_file;
    std::string language = "ja";
    std::string model = "ggml-small.en.bin";
    bool once = false;
    bool use_oww = false;
    float threshold = 0.5f;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--once") once = true;
        else if (arg == "--file" && i + 1 < argc) file = argv[++i];
        else if (arg == "--vad" && i + 1 < argc) vad_file = argv[++i];
        else if (arg == "--lang" && i + 1 < argc) language = argv[++i];
        else if (arg == "--openwakeword") use_oww = true;
        else if (arg == "--model" && i + 1 < argc) model = argv[++i];
        else if (arg == "--phrase" && i + 1 < argc) phrase = argv[++i];
        else if (arg == "--threshold" && i + 1 < argc) threshold = std::stof(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            std::puts("usage: mimi_listen [--once] [--file WAV] [--vad WAV] "
                      "[--phrase NAME] [--threshold F]");
            return 0;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        if (!vad_file.empty()) return dump_vad(vad_file);
        if (!file.empty()) return transcribe_file(file, language, model);

        mimi::audio::Capture capture;
        capture.start();

        auto config = default_config(phrase, use_oww, language);
        config.wake_threshold = threshold;
        mimi::voice::Listener listener(capture, std::move(config));

        std::atomic<float> level{0.0f};
        std::atomic<float> wake_score{0.0f};

        listener.on_level([&](float rms, float score) {
            level.store(rms);
            wake_score.store(score);
        });
        listener.on_state([](mimi::voice::State state) {
            std::printf("\n[%s]\n", std::string(mimi::voice::to_string(state)).c_str());
        });
        listener.on_utterance([&](std::string text, bool follow_up) {
            std::printf("\n>>> \"%s\"%s\n\n", text.c_str(), follow_up ? "  (follow-up)" : "");
            // Nothing is speaking yet, so close the turn immediately. Once the
            // brain is wired in this is where the reply and TTS would go.
            listener.set_speaking(false);
        });

        std::printf("warming up whisper...\n");
        listener.warmup();

        if (once) {
            std::printf("\nspeak now (no wake word needed)\n");
            listener.start();
            const auto text = listener.capture_once(std::chrono::milliseconds{10000});
            listener.stop();
            capture.stop();
            if (!text) {
                std::puts("\nnothing heard");
                return 1;
            }
            std::printf("\n>>> \"%s\"\n", text->c_str());
            return 0;
        }

        listener.start();
        if (use_oww) {
            std::printf("\nsay \"%s\" then a command -- ctrl-c to stop\n\n",
                        phrase == "hey_jarvis" ? "hey jarvis" : phrase.c_str());
        } else {
            std::printf("\nsay \"\u306d\u3048\u30df\u30df\" (or \"hey mimi\") then a command "
                        "-- ctrl-c to stop\n\n");
        }

        while (!g_stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{80});
            const float rms = level.load();
            const float score = wake_score.load();
            const auto state = listener.state();

            const int bars = static_cast<int>(std::lround(std::min(rms * 8.0f, 1.0f) * 24.0f));
            std::string meter(static_cast<std::size_t>(std::clamp(bars, 0, 24)), '=');
            std::printf("\r %s %-9s |%-24s| wake %.2f  ", icon_for(state),
                        std::string(mimi::voice::to_string(state)).c_str(), meter.c_str(),
                        score);
            std::fflush(stdout);
        }

        std::printf("\n");
        listener.stop();
        capture.stop();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "\nerror: %s\n", e.what());
        return 1;
    }
}
