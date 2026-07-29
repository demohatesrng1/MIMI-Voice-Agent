#include "voice/vad.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace mimi::voice {
namespace {
constexpr std::string_view kTag = "vad";
constexpr std::size_t kStateElements = 2 * 1 * 128;
}  // namespace

SileroVad::SileroVad(const std::filesystem::path& model_path)
    : model_(model_path),
      state_(kStateElements, 0.0f),
      input_(kInput, 0.0f) {
    if (model_.input_count() != 3) {
        throw std::runtime_error("unexpected Silero VAD graph: " +
                                 std::to_string(model_.input_count()) +
                                 " inputs, expected 3 (input, state, sr)");
    }
    log::debug(kTag, "silero ready ({}+{} sample windows)", kContext, kWindow);
}

void SileroVad::reset() {
    std::fill(state_.begin(), state_.end(), 0.0f);
    std::fill(input_.begin(), input_.end(), 0.0f);
}

float SileroVad::probability(const float* window) {
    if (window == nullptr) return 0.0f;

    // Slide the previous call's tail into the context slot, then append the new
    // window behind it.
    std::memmove(input_.data(), input_.data() + kWindow, kContext * sizeof(float));
    std::memcpy(input_.data() + kContext, window, kWindow * sizeof(float));

    const std::array<std::int64_t, 2> audio_shape{1, static_cast<std::int64_t>(kInput)};
    const std::array<std::int64_t, 3> state_shape{2, 1, 128};

    std::array<Ort::Value, 3> inputs{
        float_tensor(input_.data(), input_.size(), audio_shape),
        float_tensor(state_.data(), state_.size(), state_shape),
        int64_tensor(&sample_rate_, 1, std::span<const std::int64_t>{}),
    };

    auto outputs = model_.run(inputs);
    if (outputs.size() < 2) return 0.0f;

    const float probability = *outputs[0].GetTensorData<float>();

    // Carry the LSTM state forward. Copy rather than alias: the output tensor
    // is owned by the run and dies at the end of this call.
    const float* next = outputs[1].GetTensorData<float>();
    std::memcpy(state_.data(), next, kStateElements * sizeof(float));

    return probability;
}

Endpointer::Endpointer() : Endpointer(Config{}) {}

Endpointer::Endpointer(Config config) : config_(config) {}

void Endpointer::reset() {
    in_speech_ = false;
    ever_spoke_ = false;
    elapsed_ms_ = std::chrono::milliseconds{0};
    speech_ms_ = std::chrono::milliseconds{0};
    silence_ms_ = std::chrono::milliseconds{0};
}

Endpointer::Event Endpointer::push(float probability, std::chrono::milliseconds window) {
    elapsed_ms_ += window;

    if (in_speech_) {
        // Hysteresis: stay in speech until we drop below the *lower* threshold.
        if (probability >= config_.end_threshold) {
            speech_ms_ += window;
            silence_ms_ = std::chrono::milliseconds{0};
        } else {
            silence_ms_ += window;
            if (silence_ms_ >= config_.trailing_silence) {
                in_speech_ = false;
                // A blip too short to be a word: treat it as noise and keep
                // waiting rather than shipping it to the transcriber.
                if (speech_ms_ < config_.min_speech) {
                    speech_ms_ = std::chrono::milliseconds{0};
                    return Event::None;
                }
                return Event::SpeechEnd;
            }
        }

        if (elapsed_ms_ >= config_.max_utterance) {
            in_speech_ = false;
            return Event::MaxDuration;
        }
        return Event::None;
    }

    if (probability >= config_.start_threshold) {
        in_speech_ = true;
        ever_spoke_ = true;
        silence_ms_ = std::chrono::milliseconds{0};
        speech_ms_ += window;
        return Event::SpeechStart;
    }

    // Nobody has said anything yet and the patience window has run out.
    if (!ever_spoke_ && config_.lead_in.count() > 0 && elapsed_ms_ >= config_.lead_in) {
        return Event::NoSpeech;
    }
    return Event::None;
}

}  // namespace mimi::voice
