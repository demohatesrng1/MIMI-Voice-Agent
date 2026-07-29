#include "voice/stt.hpp"

#include "core/log.hpp"

#include "whisper.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <string_view>

namespace mimi::voice {
namespace {

constexpr std::string_view kTag = "stt";
constexpr std::uint32_t kSampleRate = 16000;

// Whisper emits these confidently over silence and background noise. They are
// artefacts of its subtitle-heavy training data, not transcription errors, so
// no confidence threshold catches them -- they have to be matched by name.
constexpr std::array kHallucinations{
    // English
    "you", "thank you", "thanks", "thank you.", "thanks for watching",
    "thanks for watching!", "thank you for watching", "please subscribe",
    "subscribe", "bye", "bye.", "[blank_audio]", "(silence)", "[silence]",
    "(buzzing)", "(music)", "[music]", ".", "..", "...", ". .", "so", "uh", "um",
    // Japanese. Whisper was trained on a lot of subtitled video, so over
    // silence it reaches for sign-off boilerplate and caption markers. These
    // come back with high confidence -- "ご視聴ありがとうございました" scored
    // 75% on pure silence during warmup -- so no threshold filters them.
    "ご視聴ありがとうございました", "ご視聴ありがとうございます",
    "ご視聴ありがとうございました。", "ありがとうございました",
    "ありがとうございました。", "ありがとうございます",
    "おやすみなさい", "お疲れ様でした", "バイバイ",
    "(音楽)", "（音楽）", "[音楽]", "(拍手)", "（拍手）",
    "(笑)", "（笑）", "(咳)", "字幕", "字幕視聴者",
    "チャンネル登録お願いします", "。", "、", "！", "？",
};

std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

std::string lowercase(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

bool is_hallucination(std::string_view text) {
    const std::string normalised = lowercase(trim(text));
    if (normalised.empty()) return true;
    return std::find(kHallucinations.begin(), kHallucinations.end(), normalised) !=
           kHallucinations.end();
}

Transcriber::Transcriber(Config config) : config_(std::move(config)) {
    if (config_.model_path.empty()) {
        throw std::runtime_error("no whisper model configured");
    }
    std::error_code ec;
    if (!std::filesystem::exists(config_.model_path, ec)) {
        throw std::runtime_error("whisper model not found: " + config_.model_path.string() +
                                 "  (run scripts/fetch_models.sh)");
    }

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;
    cparams.flash_attn = true;

    ctx_ = whisper_init_from_file_with_params(config_.model_path.string().c_str(), cparams);
    if (ctx_ == nullptr) {
        throw std::runtime_error("could not load " + config_.model_path.string());
    }
    using_gpu_ = cparams.use_gpu;

    log::info(kTag, "{} loaded ({})", config_.model_path.filename().string(),
              using_gpu_ ? "Metal GPU" : "CPU");
}

Transcriber::~Transcriber() {
    if (ctx_ != nullptr) whisper_free(ctx_);
}

void Transcriber::warmup() {
    // Half a second of silence is enough to build every Metal pipeline and
    // allocate the KV cache; doing it here costs nothing the user can perceive.
    const std::vector<float> silence(kSampleRate / 2, 0.0f);
    const auto started = std::chrono::steady_clock::now();
    transcribe(silence);
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    log::debug(kTag, "warmup took {} ms", took.count());
}

Transcript Transcriber::transcribe(std::span<const float> audio) {
    Transcript result;
    if (ctx_ == nullptr || audio.empty()) return result;

    const auto duration = std::chrono::milliseconds{
        static_cast<std::int64_t>(audio.size() * 1000 / kSampleRate)};
    if (duration < config_.min_audio) {
        log::debug(kTag, "ignoring {} ms of audio", duration.count());
        return result;
    }

    const auto started = std::chrono::steady_clock::now();

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = config_.threads;
    params.language = config_.language.c_str();
    params.translate = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;

    // Each utterance is independent: carrying decoder context between them lets
    // one misheard command bias the next.
    params.no_context = true;
    params.single_segment = false;
    params.suppress_blank = true;
    params.temperature = 0.0f;
    params.temperature_inc = 0.0f;  // no fallback passes; latency over recovery
    params.entropy_thold = 2.4f;
    params.logprob_thold = config_.logprob_threshold;
    params.no_speech_thold = config_.no_speech_threshold;
    if (!config_.initial_prompt.empty()) {
        params.initial_prompt = config_.initial_prompt.c_str();
    }

    if (whisper_full(ctx_, params, audio.data(), static_cast<int>(audio.size())) != 0) {
        log::warn(kTag, "decode failed");
        return result;
    }

    std::string text;
    double probability_sum = 0.0;
    int token_count = 0;

    const int segments = whisper_full_n_segments(ctx_);
    for (int s = 0; s < segments; ++s) {
        text += whisper_full_get_segment_text(ctx_, s);
        for (int t = 0, n = whisper_full_n_tokens(ctx_, s); t < n; ++t) {
            const auto id = whisper_full_get_token_id(ctx_, s, t);
            if (id >= whisper_token_eot(ctx_)) continue;  // skip timestamps/specials
            probability_sum += whisper_full_get_token_p(ctx_, s, t);
            ++token_count;
        }
    }

    result.took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    result.confidence = token_count > 0 ? static_cast<float>(probability_sum / token_count) : 0.0f;
    result.text = trim(text);

    if (is_hallucination(result.text)) {
        log::debug(kTag, "dropped hallucination '{}' ({:.2f})", result.text, result.confidence);
        result.text.clear();
        return result;
    }

    log::info(kTag, "'{}' ({:.0f}% confident, {} ms for {:.1f}s audio)", result.text,
              result.confidence * 100.0f, result.took.count(), duration.count() / 1000.0);
    return result;
}

}  // namespace mimi::voice
