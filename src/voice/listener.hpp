#pragma once

#include "audio/capture.hpp"
#include "voice/phrase_spotter.hpp"
#include "voice/stt.hpp"
#include "voice/vad.hpp"
#include "voice/wake_word.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mimi::voice {

enum class State {
    Idle,       // wake word armed, nothing being recorded
    Listening,  // recording an utterance, VAD deciding when it ends
    Thinking,   // transcribing and answering
    Speaking,   // reply playing; wake word still armed so it can be cut off
    Paused,     // switched off by the user
};

std::string_view to_string(State state) noexcept;

// Removes a leading wake phrase from a transcript: pre-roll deliberately
// includes the tail of "hey mimi", so it shows up in what whisper returns.
std::string strip_wake_phrase(std::string_view text);

// The always-on listening loop.
//
// One thread reads 80 ms frames from the shared ring buffer and never blocks on
// anything slow. Transcription and the reply run on a second thread, so the
// loop keeps consuming audio while Mimi is thinking or talking -- which is what
// makes barge-in possible at all.
//
// Behaviours that separate this from a poll-and-transcribe loop:
//
//   pre-roll     the utterance starts *before* the wake word fired, so running
//                "hey mimi what time is it" together doesn't clip the command
//   endpointing  Silero decides when you stopped talking, instead of a fixed
//                recording window that either truncates you or makes you wait
//   barge-in     the wake word stays armed while Mimi speaks, so you can cut
//                her off mid-sentence
//   follow-up    for a few seconds after a reply she keeps listening without
//                needing the wake word again
class Listener {
public:
    // How Mimi decides she was addressed.
    enum class WakeBackend {
        // Trained openWakeWord classifier. Lowest latency and lowest CPU, but
        // only for phrases a model exists for -- there is no "hey mimi" model.
        OpenWakeWord,
        // Silero segments speech, a small whisper transcribes it, and the wake
        // phrase is matched in text. Works for any phrase with no training, at
        // the cost of a transcription per utterance in the room.
        PhraseSpotter,
    };

    struct Config {
        WakeBackend wake_backend = WakeBackend::PhraseSpotter;

        // OpenWakeWord backend only.
        std::filesystem::path melspec_model;
        std::filesystem::path embedding_model;
        std::vector<std::filesystem::path> wake_classifiers;

        // PhraseSpotter backend only: the cheap model used to decide whether a
        // segment was addressed to Mimi before the accurate one runs.
        std::filesystem::path gate_model;
        PhraseSpotter::Config spotter;

        std::filesystem::path whisper_model;
        std::filesystem::path vad_model;
        std::string language = "ja";

        float wake_threshold = 0.5f;
        // Raised while Mimi is speaking: her own voice bleeds into the mic and
        // makes false triggers much more likely without echo cancellation.
        float wake_threshold_speaking = 0.7f;
        int trigger_frames = 1;

        // How far back the utterance starts relative to the wake-word hit.
        std::chrono::milliseconds preroll{500};

        Endpointer::Config endpoint{};

        // After a reply, keep listening this long without a wake word. Zero
        // disables it.
        std::chrono::milliseconds follow_up{6000};

        // Barge-in is only safe with the trained wake word, whose acoustics
        // reject Mimi's own voice. The spotter transcribes anything, so it
        // would answer itself; see echo_tail.
        bool barge_in = true;
        // How long the microphone stays closed after she stops speaking, to
        // let the speaker and the room settle before listening resumes.
        std::chrono::milliseconds echo_tail{450};
        // Cutting her off by simply talking over her, on the spotter backend.
        //
        // The wake-word backend can do this acoustically; the spotter cannot,
        // because it has no model of the phrase and would transcribe her own
        // voice back to her. So it watches level instead: speech this much
        // louder than her playback, held this long, is a person leaning in
        // rather than the speaker bleeding into the microphone. Without echo
        // cancellation the threshold has to sit well above room bleed, which is
        // why it is deliberately high -- a false trigger cuts her off mid-answer.
        bool barge_in_on_speech = true;
        float barge_in_rms = 0.055f;
        std::chrono::milliseconds barge_in_hold{280};

        int whisper_threads = 4;
    };

    // `text` has had the wake phrase stripped. `follow_up` is true when it was
    // captured without a wake word, during the post-reply window.
    using UtteranceHandler = std::function<void(std::string text, bool follow_up)>;
    using StateHandler = std::function<void(State)>;
    // rms 0..1 and the top wake score, ~12x a second. For meters.
    using LevelHandler = std::function<void(float rms, float wake_score)>;

    Listener(audio::Capture& capture, Config config);
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    // Handlers must be installed before start(). They run on the worker thread,
    // never on the audio thread.
    void on_utterance(UtteranceHandler handler);
    void on_state(StateHandler handler);
    void on_level(LevelHandler handler);
    // Fired when the wake word interrupts a reply; stop the audio here.
    void on_barge_in(std::function<void()> handler);

    void start();
    void stop();

    // The app calls this around playback so barge-in can arm and follow-up can
    // start counting from when Mimi actually stopped talking.
    void set_speaking(bool speaking);

    void pause();
    void resume();
    bool paused() const noexcept;

    // Push-to-talk: record one utterance now, no wake word. Blocks until the
    // endpointer closes it or `timeout` passes.
    std::optional<std::string> capture_once(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{10000});

    State state() const noexcept { return state_.load(std::memory_order_relaxed); }

    // Warms whisper so the first real utterance isn't slowed by model load.
    void warmup();

private:
    // One captured utterance, handed from the audio thread to the worker.
    struct Job {
        std::vector<float> audio;
        bool follow_up = false;
        bool push_to_talk = false;
        // Still has to pass the wake-phrase test. False for push-to-talk and
        // for follow-ups, which have already earned their turn.
        bool gated = false;
    };

    void run();   // audio thread: never blocks on anything slow
    void work();  // worker thread: transcription and the reply
    void step_wake_word(const float* frame, State current, float rms);
    void step_spotter(const float* frame, float rms);
    Endpointer::Event pump_vad(const float* frame);
    void enter(State state);
    void begin_utterance(audio::Cursor start, bool follow_up);
    void finish_utterance(audio::Cursor end, bool gated);
    Endpointer::Config spotting_config() const;
    void deliver_once(std::optional<std::string> text);
    audio::Cursor preroll_from(audio::Cursor cursor) const;
    float wake_threshold_now() const;

    audio::Capture& capture_;
    Config config_;

    std::unique_ptr<WakeWord> wake_;    // OpenWakeWord backend
    std::unique_ptr<Transcriber> gate_; // PhraseSpotter backend
    PhraseSpotter spotter_;
    std::unique_ptr<SileroVad> vad_;
    std::unique_ptr<Transcriber> stt_;
    Endpointer endpointer_;

    UtteranceHandler on_utterance_;
    StateHandler on_state_;
    LevelHandler on_level_;
    std::function<void()> on_barge_in_;

    std::thread thread_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> speaking_{false};
    // How long the caller has been talking over her, on the spotter path.
    std::chrono::steady_clock::time_point loud_since_{};
    std::atomic<State> state_{State::Idle};

    std::deque<Job> jobs_;
    std::mutex jobs_mutex_;
    std::condition_variable jobs_ready_;

    // Set by capture_once() to force the loop into Listening.
    std::atomic<bool> force_listen_{false};
    std::mutex once_mutex_;
    std::condition_variable once_ready_;
    std::optional<std::string> once_result_;
    bool once_done_ = false;

    // set_speaking(false) raises this; the audio thread acts on it, so the VAD
    // is only ever touched from one thread.
    std::atomic<bool> pending_follow_up_{false};

    // Audio-thread only.
    audio::Cursor cursor_ = 0;
    audio::Cursor utterance_start_ = 0;
    bool follow_up_ = false;
    std::vector<float> vad_pending_;
    std::chrono::steady_clock::time_point muted_until_{};
};

}  // namespace mimi::voice
