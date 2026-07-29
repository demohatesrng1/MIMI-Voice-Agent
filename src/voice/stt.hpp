#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct whisper_context;

namespace mimi::voice {

struct Transcript {
    std::string text;
    float confidence = 0.0f;             // mean segment probability, 0..1
    std::chrono::milliseconds took{0};
    bool empty() const noexcept { return text.empty(); }
};

// whisper.cpp, GPU-accelerated through GGML's Metal backend.
//
// Only called once an utterance has already been captured and endpointed, so
// it is not on the realtime path -- but it is on the path the user *feels*,
// between finishing a sentence and Mimi answering. Everything here is tuned
// for latency on a short command rather than throughput on a long recording:
// greedy sampling, no cross-utterance context, single pass.
class Transcriber {
public:
    struct Config {
        std::filesystem::path model_path;
        std::string language = "en";
        int threads = 4;  // performance cores; the E-cores hurt more than help

        // Whisper is biased by whatever it has seen. Priming it with the
        // vocabulary Mimi actually hears -- her name, the verbs, common app and
        // site names -- measurably improves proper nouns in short commands.
        std::string initial_prompt =
            "Hey Mimi, open YouTube, launch Spotify, search for Rust tutorials, "
            "summarize this page, take notes, remind me in twenty minutes, "
            "what time is it, how is my battery, take a screenshot.";

        // Reject a decode whose own no-speech probability is this high.
        float no_speech_threshold = 0.6f;
        // Reject a decode this unlikely on average; catches confident nonsense.
        float logprob_threshold = -1.0f;
        // Anything shorter than this is a cough, not a command.
        std::chrono::milliseconds min_audio{250};
    };

    explicit Transcriber(Config config);
    ~Transcriber();

    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;

    // Transcribes 16 kHz mono float audio. Returns an empty transcript for
    // silence, noise, or a decode that fails the confidence gates -- callers
    // should treat that as "nothing was said", not as an error.
    Transcript transcribe(std::span<const float> audio);

    // Runs a short silent decode to force model load, Metal pipeline
    // compilation and buffer allocation to happen now rather than on the user's
    // first real sentence, where it would read as a two-second hang.
    void warmup();

    bool using_gpu() const noexcept { return using_gpu_; }
    const std::filesystem::path& model_path() const noexcept { return config_.model_path; }

private:
    Config config_;
    whisper_context* ctx_ = nullptr;
    bool using_gpu_ = false;
};

// True when `text` is one of whisper's stock hallucinations on silence --
// "Thank you.", "[BLANK_AUDIO]", subtitle credits and friends. These come back
// with high confidence, so they cannot be filtered by score alone.
bool is_hallucination(std::string_view text);

}  // namespace mimi::voice
