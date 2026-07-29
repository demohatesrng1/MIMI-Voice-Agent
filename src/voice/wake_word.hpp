#pragma once

#include "voice/onnx_model.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mimi::voice {

// openWakeWord, ported to C++.
//
// Three chained graphs, each fed from a rolling buffer:
//
//   audio ──> melspectrogram ──> speech embedding ──> phrase classifier
//   1280       8 frames x 32       1 x 96 vector        one score
//
// The geometry is not arbitrary and none of it is tunable -- it has to match
// the Python implementation the models were trained against:
//
//   * audio enters at int16 scale (+/-32768), not +/-1.0
//   * mel frames are transformed x/10 + 2 to match the original TF frontend
//   * the mel model emits (N - 480)/160 frames, so 1280 new samples are fed
//     with 480 of lookback to produce exactly 8 aligned frames
//   * the embedder consumes the trailing 76 mel frames, stepping 8 at a time
//   * the classifier consumes the trailing 16 embeddings
//
// scripts/reference_scores.py + `mimi_wake --compare` check this numerically
// against openWakeWord itself.
class WakeWord {
public:
    // 80 ms at 16 kHz. push() takes exactly this many samples.
    static constexpr std::size_t kFrame = 1280;

    struct Config {
        std::filesystem::path melspec_model;
        std::filesystem::path embedding_model;
        std::vector<std::filesystem::path> classifiers;

        float threshold = 0.5f;
        // Consecutive frames over threshold before firing. 1 is responsive;
        // 2 trades ~80 ms of latency for far fewer false accepts.
        int trigger_frames = 1;
        // Ignore further detections for this long after one fires, so a single
        // spoken phrase cannot trigger on every frame it stays hot.
        std::chrono::milliseconds refractory{1500};
    };

    struct Detection {
        std::string name;
        float score = 0.0f;
    };

    explicit WakeWord(Config config);
    ~WakeWord();

    WakeWord(const WakeWord&) = delete;
    WakeWord& operator=(const WakeWord&) = delete;

    // Feed exactly kFrame samples in +/-1.0 float. Scaling to int16 range is
    // done internally. Returns the highest-scoring phrase over threshold.
    std::optional<Detection> push(const float* frame);

    // Per-classifier scores from the most recent push, in names() order.
    // For meters and threshold tuning.
    const std::vector<float>& last_scores() const noexcept { return scores_; }
    const std::vector<std::string>& names() const noexcept { return names_; }

    float threshold() const noexcept { return config_.threshold; }
    void set_threshold(float t) noexcept { config_.threshold = t; }

    // Clears the rolling buffers. Use when resuming after a long pause, so
    // stale context cannot combine with new audio into a phantom trigger.
    void reset();

private:
    void append_mel(const float* audio, std::size_t count);
    void append_embedding();

    Config config_;

    OnnxModel melspec_;
    OnnxModel embedder_;
    std::vector<std::unique_ptr<OnnxModel>> classifiers_;
    std::vector<std::string> names_;
    std::vector<float> scores_;
    std::vector<int> streak_;

    std::vector<float> raw_;       // trailing audio at int16 scale
    std::vector<float> mel_;       // frames x 32, row-major
    std::vector<float> features_;  // embeddings x 96, row-major

    std::chrono::steady_clock::time_point last_fired_{};
};

// Turns "hey_jarvis_v0.1.onnx" into "hey jarvis".
std::string phrase_from_filename(const std::filesystem::path& path);

}  // namespace mimi::voice
