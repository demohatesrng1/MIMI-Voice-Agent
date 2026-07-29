#pragma once

#include "voice/onnx_model.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace mimi::voice {

// Silero VAD v5, run in streaming mode.
//
// The model carries an LSTM state between calls, so probabilities depend on
// what came before. That is what makes it far better than an energy gate in a
// noisy room -- but it also means the state must be reset between unrelated
// utterances, or the tail of one bleeds into the start of the next.
class SileroVad {
public:
    // New samples consumed per call at 16 kHz. Not a tunable.
    static constexpr std::size_t kWindow = 512;

    // v5 is fed the previous call's last 64 samples in front of the new 512.
    // Omitting them is not an error the model reports -- it just returns near
    // zero for everything, which reads as "nobody is talking" forever.
    static constexpr std::size_t kContext = 64;
    static constexpr std::size_t kInput = kContext + kWindow;  // 576

    explicit SileroVad(const std::filesystem::path& model_path);

    // Probability that `window` (exactly kWindow new samples) contains speech.
    float probability(const float* window);

    void reset();

private:
    OnnxModel model_;
    std::vector<float> state_;  // [2, 1, 128] LSTM carry
    std::vector<float> input_;  // [context | window], kept alive across Run()
    std::int64_t sample_rate_ = 16000;
};

// Turns a stream of per-window probabilities into utterance boundaries.
//
// Two thresholds rather than one: crossing `start` opens an utterance, but it
// stays open until the probability falls below the lower `end`. Without that
// hysteresis, the natural dips inside a sentence chop it into fragments.
class Endpointer {
public:
    struct Config {
        float start_threshold = 0.55f;
        float end_threshold = 0.35f;
        std::chrono::milliseconds min_speech{160};        // reject lip smacks
        std::chrono::milliseconds trailing_silence{700};  // how long a pause may run
        std::chrono::milliseconds max_utterance{15000};   // hard stop
        std::chrono::milliseconds lead_in{4000};          // silence before giving up
    };

    enum class Event {
        None,
        SpeechStart,
        SpeechEnd,     // trailing_silence elapsed after real speech
        MaxDuration,   // hit max_utterance while still talking
        NoSpeech,      // lead_in elapsed without anyone starting
    };

    Endpointer();
    explicit Endpointer(Config config);

    // Feed one window's probability. `window` is how much audio it covered.
    Event push(float probability,
               std::chrono::milliseconds window = std::chrono::milliseconds{32});

    void reset();

    bool in_speech() const noexcept { return in_speech_; }
    std::chrono::milliseconds speech_duration() const noexcept { return speech_ms_; }
    std::chrono::milliseconds elapsed() const noexcept { return elapsed_ms_; }

    const Config& config() const noexcept { return config_; }

private:
    Config config_;
    bool in_speech_ = false;
    bool ever_spoke_ = false;
    std::chrono::milliseconds elapsed_ms_{0};
    std::chrono::milliseconds speech_ms_{0};
    std::chrono::milliseconds silence_ms_{0};
};

}  // namespace mimi::voice
