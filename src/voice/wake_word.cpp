#include "voice/wake_word.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace mimi::voice {
namespace {

constexpr std::string_view kTag = "wake";

// --- frontend geometry, fixed by how the models were trained ---------------

constexpr std::size_t kMelBins = 32;
constexpr std::size_t kMelLookback = 480;  // 160 * 3, the STFT's reach backwards
constexpr std::size_t kMelHop = 160;       // 10 ms
constexpr std::size_t kMelMinInput = 640;  // below this the mel graph refuses
constexpr std::size_t kEmbedFrames = 76;   // mel frames per embedding
constexpr std::size_t kEmbedDims = 96;
constexpr std::size_t kClassifierFrames = 16;  // embeddings per score

// Ring caps, matching openWakeWord's melspectrogram_max_len / feature_buffer_max_len.
constexpr std::size_t kMelMaxFrames = 10 * 97;
constexpr std::size_t kFeatureMaxFrames = 120;

// CoreAudio hands us +/-1.0; the models were trained on int16 magnitudes.
constexpr float kInt16Scale = 32768.0f;

// Keep only the trailing `keep` elements of a vector.
void trim_front(std::vector<float>& v, std::size_t keep) {
    if (v.size() > keep) v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() - keep));
}

}  // namespace

std::string phrase_from_filename(const std::filesystem::path& path) {
    std::string name = path.stem().string();
    // "hey_jarvis_v0.1" -> "hey jarvis"
    if (const auto v = name.rfind("_v"); v != std::string::npos && v + 2 < name.size() &&
                                         std::isdigit(static_cast<unsigned char>(name[v + 2]))) {
        name.resize(v);
    }
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

WakeWord::WakeWord(Config config)
    : config_(std::move(config)),
      melspec_(config_.melspec_model),
      embedder_(config_.embedding_model) {
    if (config_.classifiers.empty()) {
        throw std::runtime_error("no wake-word classifier given");
    }
    for (const auto& path : config_.classifiers) {
        classifiers_.push_back(std::make_unique<OnnxModel>(path));
        names_.push_back(phrase_from_filename(path));
    }
    scores_.assign(classifiers_.size(), 0.0f);
    streak_.assign(classifiers_.size(), 0);

    reset();
    log::info(kTag, "listening for {} phrase{} (threshold {:.2f})", names_.size(),
              names_.size() == 1 ? "" : "s", config_.threshold);
}

WakeWord::~WakeWord() = default;

void WakeWord::reset() {
    raw_.clear();
    raw_.reserve(kFrame + kMelLookback);

    // openWakeWord seeds the mel buffer with ones((76, 32)) so the very first
    // chunk already has a full embedding window behind it.
    mel_.assign(kEmbedFrames * kMelBins, 1.0f);

    // It seeds features from 4 s of *random* noise, which cannot be reproduced
    // and does not need to be: after 16 pushes the classifier window holds only
    // real audio. Zeros keep the early frames quiet instead of noisy.
    features_.assign(kClassifierFrames * kEmbedDims, 0.0f);

    std::fill(scores_.begin(), scores_.end(), 0.0f);
    std::fill(streak_.begin(), streak_.end(), 0);
    last_fired_ = {};
}

void WakeWord::append_mel(const float* audio, std::size_t count) {
    if (count < kMelMinInput) return;

    const std::array<std::int64_t, 2> shape{1, static_cast<std::int64_t>(count)};
    auto output = melspec_.run_one(
        float_tensor(const_cast<float*>(audio), count, shape));

    const auto info = output.GetTensorTypeAndShapeInfo();
    const auto dims = info.GetShape();          // [1, 1, frames, 32]
    if (dims.size() != 4 || dims[3] != static_cast<std::int64_t>(kMelBins)) {
        throw std::runtime_error("unexpected melspectrogram output rank");
    }
    const auto frames = static_cast<std::size_t>(dims[2]);
    const float* data = output.GetTensorData<float>();

    // The transform that reconciles this ONNX export with the original
    // TensorFlow speech-embedding frontend. Without it every score is garbage.
    const std::size_t base = mel_.size();
    mel_.resize(base + frames * kMelBins);
    for (std::size_t i = 0; i < frames * kMelBins; ++i) {
        mel_[base + i] = data[i] / 10.0f + 2.0f;
    }

    trim_front(mel_, kMelMaxFrames * kMelBins);
}

void WakeWord::append_embedding() {
    const std::size_t needed = kEmbedFrames * kMelBins;
    if (mel_.size() < needed) return;

    float* window = mel_.data() + (mel_.size() - needed);
    const std::array<std::int64_t, 4> shape{
        1, static_cast<std::int64_t>(kEmbedFrames), static_cast<std::int64_t>(kMelBins), 1};

    auto output = embedder_.run_one(float_tensor(window, needed, shape));
    const float* data = output.GetTensorData<float>();

    features_.insert(features_.end(), data, data + kEmbedDims);
    trim_front(features_, kFeatureMaxFrames * kEmbedDims);
}

std::optional<WakeWord::Detection> WakeWord::push(const float* frame) {
    if (frame == nullptr) return std::nullopt;

    // Keep the previous chunk's tail so the mel graph has its 480-sample
    // lookback; the first call simply has less and yields fewer frames, exactly
    // as the reference does.
    if (raw_.size() > kMelLookback) trim_front(raw_, kMelLookback);
    const std::size_t carried = raw_.size();

    raw_.reserve(carried + kFrame);
    for (std::size_t i = 0; i < kFrame; ++i) raw_.push_back(frame[i] * kInt16Scale);

    append_mel(raw_.data(), raw_.size());
    append_embedding();

    const std::size_t window = kClassifierFrames * kEmbedDims;
    if (features_.size() < window) return std::nullopt;

    float* features = features_.data() + (features_.size() - window);
    const std::array<std::int64_t, 3> shape{1, static_cast<std::int64_t>(kClassifierFrames),
                                            static_cast<std::int64_t>(kEmbedDims)};

    const auto now = std::chrono::steady_clock::now();
    const bool cooling = last_fired_.time_since_epoch().count() != 0 &&
                         now - last_fired_ < config_.refractory;

    std::optional<Detection> best;
    for (std::size_t i = 0; i < classifiers_.size(); ++i) {
        auto output = classifiers_[i]->run_one(float_tensor(features, window, shape));
        const float score = *output.GetTensorData<float>();
        scores_[i] = score;

        if (score < config_.threshold) {
            streak_[i] = 0;
            continue;
        }
        ++streak_[i];
        if (streak_[i] < config_.trigger_frames || cooling) continue;
        if (!best || score > best->score) best = Detection{names_[i], score};
    }

    if (best) {
        last_fired_ = now;
        std::fill(streak_.begin(), streak_.end(), 0);
        log::info(kTag, "heard '{}' ({:.3f})", best->name, best->score);
    }
    return best;
}

}  // namespace mimi::voice
