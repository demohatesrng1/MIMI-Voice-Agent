#include "voice/listener.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace mimi::voice {
namespace {

constexpr std::string_view kTag = "listen";

// Every phrasing of the wake word that might survive into a transcript.
constexpr std::array<std::string_view, 8> kWakePhrases{
    "hey mimi", "hi mimi", "ok mimi", "hey jarvis", "hey mycroft",
    "alexa",    "mimi",    "jarvis",
};

std::string lowercase(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool is_boundary(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0 ||
           std::ispunct(static_cast<unsigned char>(c)) != 0;
}

}  // namespace

std::string_view to_string(State state) noexcept {
    switch (state) {
        case State::Idle:      return "idle";
        case State::Listening: return "listening";
        case State::Thinking:  return "thinking";
        case State::Speaking:  return "speaking";
        case State::Paused:    return "paused";
    }
    return "?";
}

std::string strip_wake_phrase(std::string_view text) {
    const std::string lower = lowercase(text);

    std::size_t best_end = 0;
    for (std::string_view phrase : kWakePhrases) {
        const auto at = lower.find(phrase);
        // Only strip near the front -- "ask mimi" mid-sentence is content.
        if (at == std::string::npos || at > 12) continue;
        const std::size_t end = at + phrase.size();
        // Must end on a word boundary, so "mimi" doesn't eat "minimal".
        if (end < lower.size() && !is_boundary(lower[end])) continue;
        best_end = std::max(best_end, end);
    }
    if (best_end == 0) return std::string(text);

    std::size_t start = best_end;
    while (start < text.size() && is_boundary(text[start])) ++start;
    return std::string(text.substr(start));
}

Listener::Listener(audio::Capture& capture, Config config)
    : capture_(capture),
      config_(std::move(config)),
      spotter_(config_.spotter),
      endpointer_(config_.endpoint) {
    if (config_.wake_backend == WakeBackend::OpenWakeWord) {
        WakeWord::Config wake_config;
        wake_config.melspec_model = config_.melspec_model;
        wake_config.embedding_model = config_.embedding_model;
        wake_config.classifiers = config_.wake_classifiers;
        wake_config.threshold = config_.wake_threshold;
        wake_config.trigger_frames = config_.trigger_frames;
        wake_ = std::make_unique<WakeWord>(std::move(wake_config));
    } else if (!config_.gate_model.empty()) {
        // Optional cheap first pass. Only worth it when the small model can be
        // trusted to spot the wake word -- ggml-tiny is fine for English and
        // produces nonsense for Japanese, so leave this empty for ja and let
        // the accurate model do both jobs in one decode.
        Transcriber::Config gate_config;
        gate_config.model_path = config_.gate_model;
        gate_config.language = config_.language;
        gate_config.threads = 2;
        gate_config.initial_prompt.clear();
        gate_ = std::make_unique<Transcriber>(std::move(gate_config));
    }

    vad_ = std::make_unique<SileroVad>(config_.vad_model);

    Transcriber::Config stt_config;
    stt_config.model_path = config_.whisper_model;
    stt_config.language = config_.language;
    stt_config.threads = config_.whisper_threads;
    if (config_.language != "en") stt_config.initial_prompt.clear();
    stt_ = std::make_unique<Transcriber>(std::move(stt_config));

    // In spotter mode the endpointer runs forever, segmenting whatever it
    // hears; there is no wake word to start a countdown from.
    if (config_.wake_backend == WakeBackend::PhraseSpotter) {
        endpointer_ = Endpointer(spotting_config());
    }
}

Endpointer::Config Listener::spotting_config() const {
    auto config = config_.endpoint;
    config.lead_in = std::chrono::milliseconds{0};  // never give up
    return config;
}

Listener::~Listener() { stop(); }

void Listener::on_utterance(UtteranceHandler handler) { on_utterance_ = std::move(handler); }
void Listener::on_state(StateHandler handler) { on_state_ = std::move(handler); }
void Listener::on_level(LevelHandler handler) { on_level_ = std::move(handler); }
void Listener::on_barge_in(std::function<void()> handler) { on_barge_in_ = std::move(handler); }

void Listener::warmup() {
    if (gate_) gate_->warmup();
    stt_->warmup();
}

void Listener::start() {
    if (running_.exchange(true)) return;
    cursor_ = capture_.now();
    worker_ = std::thread(&Listener::work, this);
    thread_ = std::thread(&Listener::run, this);
    log::info(kTag, "always-on listening started");
}

void Listener::stop() {
    if (!running_.exchange(false)) return;
    jobs_ready_.notify_all();
    once_ready_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (worker_.joinable()) worker_.join();
    log::info(kTag, "listening stopped");
}

void Listener::set_speaking(bool speaking) {
    speaking_.store(speaking, std::memory_order_relaxed);
    if (speaking) {
        enter(State::Speaking);
        return;
    }
    // Mimi just stopped talking. Hand the follow-up decision to the audio
    // thread rather than touching the VAD from here.
    if (config_.follow_up.count() > 0) {
        pending_follow_up_.store(true, std::memory_order_relaxed);
    }
    // Leave Thinking unconditionally. The audio loop deliberately ignores
    // frames in that state, so it would never observe the flag above and the
    // turn would hang there forever.
    enter(State::Idle);
}

void Listener::pause() {
    paused_.store(true, std::memory_order_relaxed);
    enter(State::Paused);
}

void Listener::resume() {
    paused_.store(false, std::memory_order_relaxed);
    if (wake_) wake_->reset();   // null on the spotter backend
    vad_->reset();
    vad_pending_.clear();
    enter(State::Idle);
}

bool Listener::paused() const noexcept { return paused_.load(std::memory_order_relaxed); }

void Listener::enter(State state) {
    const State previous = state_.exchange(state, std::memory_order_relaxed);
    if (previous == state) return;
    log::debug(kTag, "{} -> {}", to_string(previous), to_string(state));
    if (on_state_) on_state_(state);
}

float Listener::wake_threshold_now() const {
    return speaking_.load(std::memory_order_relaxed) ? config_.wake_threshold_speaking
                                                     : config_.wake_threshold;
}

audio::Cursor Listener::preroll_from(audio::Cursor cursor) const {
    const auto back =
        audio::samples_for(std::chrono::duration<double>(config_.preroll).count());
    return cursor > back ? cursor - back : 0;
}

void Listener::begin_utterance(audio::Cursor start, bool follow_up) {
    utterance_start_ = start;
    vad_pending_.clear();
    vad_->reset();
    follow_up_ = follow_up;

    // The wake word stops being fed while we record, so its rolling buffers
    // freeze holding the phrase that just triggered. Without clearing them, the
    // first frames pushed after we return to Idle land on top of that stale
    // context and score as another detection ~80 ms later.
    if (wake_) wake_->reset();   // null on the spotter backend

    // A follow-up has no wake word to prove intent, so it gets a patience
    // window: say nothing and it closes quietly instead of recording the room.
    auto config = config_.endpoint;
    if (follow_up) config.lead_in = config_.follow_up;
    endpointer_ = Endpointer(config);

    enter(State::Listening);
}

void Listener::finish_utterance(audio::Cursor end, bool gated) {
    auto audio = capture_.buffer().slice(utterance_start_, end);
    log::debug(kTag, "captured {:.2f}s", audio::seconds_for(audio.size()));
    enter(State::Thinking);

    const bool push_to_talk = force_listen_.load(std::memory_order_relaxed);

    // Hand off and get straight back to reading frames. Transcribing here would
    // stall the loop for hundreds of milliseconds and make barge-in impossible.
    {
        std::lock_guard lock(jobs_mutex_);
        jobs_.push_back(Job{std::move(audio), follow_up_, push_to_talk,
                            gated && !push_to_talk && !follow_up_});
    }
    jobs_ready_.notify_one();
}

void Listener::deliver_once(std::optional<std::string> text) {
    {
        std::lock_guard lock(once_mutex_);
        once_result_ = std::move(text);
        once_done_ = true;
    }
    once_ready_.notify_all();
    force_listen_.store(false, std::memory_order_relaxed);
}

void Listener::work() {
    while (running_.load(std::memory_order_relaxed)) {
        Job job;
        {
            std::unique_lock lock(jobs_mutex_);
            jobs_ready_.wait(lock, [this] {
                return !jobs_.empty() || !running_.load(std::memory_order_relaxed);
            });
            if (!running_.load(std::memory_order_relaxed)) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        std::string text;

        if (job.gated) {
            if (gate_) {
                // Cheap first pass: only good enough to answer "was that
                // addressed to Mimi?". Most room noise dies here.
                const auto rough = gate_->transcribe(job.audio);
                if (!spotter_.find(rough.text)) {
                    log::debug(kTag, "not for me: '{}'", rough.text);
                    enter(State::Idle);
                    continue;
                }
            }
            const auto full = stt_->transcribe(job.audio);
            const auto hit = spotter_.find(full.text);
            if (!hit) {
                log::debug(kTag, "not for me: '{}'", full.text);
                enter(State::Idle);
                continue;
            }
            text = hit.remainder;
            log::info(kTag, "addressed: '{}' -> '{}'", full.text, text);
        } else {
            text = strip_wake_phrase(stt_->transcribe(job.audio).text);
        }

        if (job.push_to_talk) {
            deliver_once(text.empty() ? std::nullopt : std::optional<std::string>(text));
            enter(State::Idle);
            continue;
        }
        if (text.empty()) {
            enter(State::Idle);
            continue;
        }

        // The handler produces the reply. If it speaks, it calls set_speaking()
        // and that is what moves us out of Thinking.
        if (on_utterance_) {
            on_utterance_(text, job.follow_up);
        } else {
            enter(State::Idle);
        }
    }
}

void Listener::run() {
    std::vector<float> frame(WakeWord::kFrame);
    constexpr auto kVadMs =
        std::chrono::milliseconds{SileroVad::kWindow * 1000 / audio::kSampleRate};

    while (running_.load(std::memory_order_relaxed)) {
        if (!capture_.buffer().read(cursor_, frame.data(), frame.size(),
                                    std::chrono::milliseconds{200})) {
            continue;  // nothing new yet
        }
        if (paused_.load(std::memory_order_relaxed)) continue;

        const State current = state_.load(std::memory_order_relaxed);

        // The worker owns this phase. Keep draining the ring so we don't fall
        // behind, but don't act on the audio.
        if (current == State::Thinking) continue;

        float rms = 0.0f;
        for (float s : frame) rms += s * s;
        rms = std::sqrt(rms / static_cast<float>(frame.size()));

        if (config_.wake_backend == WakeBackend::PhraseSpotter) {
            step_spotter(frame.data(), rms);
        } else {
            step_wake_word(frame.data(), current, rms);
        }
    }
}

// Feeds Silero and returns whatever the endpointer concluded from this frame.
Endpointer::Event Listener::pump_vad(const float* frame) {
    constexpr auto kVadMs =
        std::chrono::milliseconds{SileroVad::kWindow * 1000 / audio::kSampleRate};

    // Silero wants 512-sample windows and we read 1280 at a time, so carry the
    // remainder between frames -- feeding it discontiguous audio would corrupt
    // its LSTM state.
    vad_pending_.insert(vad_pending_.end(), frame, frame + WakeWord::kFrame);

    std::size_t offset = 0;
    Endpointer::Event event = Endpointer::Event::None;
    while (vad_pending_.size() - offset >= SileroVad::kWindow) {
        const float probability = vad_->probability(vad_pending_.data() + offset);
        offset += SileroVad::kWindow;
        event = endpointer_.push(probability, kVadMs);
        if (event != Endpointer::Event::None) break;
    }
    vad_pending_.erase(vad_pending_.begin(),
                       vad_pending_.begin() + static_cast<std::ptrdiff_t>(offset));
    return event;
}

// Trained-classifier path: wait for the wake word, then record until the
// endpointer closes the utterance.
void Listener::step_wake_word(const float* frame, State current, float rms) {
    if (current == State::Listening) {
        const auto event = pump_vad(frame);
        if (on_level_) on_level_(rms, 0.0f);

        switch (event) {
            case Endpointer::Event::SpeechEnd:
            case Endpointer::Event::MaxDuration:
                finish_utterance(cursor_, false);
                break;
            case Endpointer::Event::NoSpeech:
                log::debug(kTag, "nothing said, standing down");
                if (force_listen_.load(std::memory_order_relaxed)) deliver_once(std::nullopt);
                enter(State::Idle);
                break;
            default:
                break;
        }
        return;
    }

    if (force_listen_.load(std::memory_order_relaxed)) {
        pending_follow_up_.store(false, std::memory_order_relaxed);
        begin_utterance(preroll_from(cursor_), false);
        return;
    }
    if (pending_follow_up_.exchange(false, std::memory_order_relaxed)) {
        log::debug(kTag, "follow-up window open");
        begin_utterance(cursor_, true);
        return;
    }

    // Idle or Speaking: the wake word is armed in both, which is what lets the
    // user cut Mimi off mid-reply.
    wake_->set_threshold(wake_threshold_now());
    const auto hit = wake_->push(frame);

    if (on_level_) {
        const auto& scores = wake_->last_scores();
        on_level_(rms, scores.empty() ? 0.0f
                                      : *std::max_element(scores.begin(), scores.end()));
    }
    if (hit) {
        if (current == State::Speaking && config_.barge_in) {
            log::info(kTag, "barge-in");
            if (on_barge_in_) on_barge_in_();
            speaking_.store(false, std::memory_order_relaxed);
        }
        begin_utterance(preroll_from(cursor_), false);
    }
}

// Spotter path: there is no wake word to wait for, so the endpointer runs
// continuously and every speech segment is captured. Whether it was addressed
// to Mimi is decided afterwards, in text, on the worker thread.
void Listener::step_spotter(const float* frame, float rms) {
    // Mimi's own voice reaches the microphone, and the spotter has no wake-word
    // acoustics to reject it -- it just transcribes whatever it hears. Left
    // running, she replies to herself in a loop. Real barge-in needs acoustic
    // echo cancellation; until then the mic is closed while she talks, plus a
    // short tail so the room and the speaker have settled.
    if (speaking_.load(std::memory_order_relaxed)) {
        muted_until_ = std::chrono::steady_clock::now() + config_.echo_tail;
        if (on_level_) on_level_(rms, 0.0f);
        return;
    }
    if (muted_until_.time_since_epoch().count() != 0) {
        if (std::chrono::steady_clock::now() < muted_until_) {
            if (on_level_) on_level_(rms, 0.0f);
            return;
        }
        // Coming back after being deaf: the VAD's history is a gap, so clear it.
        muted_until_ = {};
        vad_->reset();
        vad_pending_.clear();
        endpointer_ = Endpointer(spotting_config());
    }

    // Push-to-talk needs no special case here: every speech segment is captured
    // anyway, and force_listen_ only decides that this one skips the wake test.
    const auto event = pump_vad(frame);
    if (on_level_) on_level_(rms, endpointer_.in_speech() ? 1.0f : 0.0f);

    switch (event) {
        case Endpointer::Event::SpeechStart:
            if (state_.load(std::memory_order_relaxed) != State::Listening) {
                // Reach back before the onset: Silero confirms speech a beat
                // after it actually started.
                utterance_start_ = preroll_from(cursor_);
                follow_up_ = pending_follow_up_.exchange(false, std::memory_order_relaxed);
                enter(State::Listening);
            }
            break;

        case Endpointer::Event::SpeechEnd:
        case Endpointer::Event::MaxDuration: {
            finish_utterance(cursor_, /*gated=*/true);
            // Segmenting is continuous, so start the next one from a clean slate.
            vad_->reset();
            vad_pending_.clear();
            endpointer_ = Endpointer(spotting_config());
            break;
        }
        default:
            break;
    }
}

std::optional<std::string> Listener::capture_once(std::chrono::milliseconds timeout) {
    {
        std::lock_guard lock(once_mutex_);
        once_result_.reset();
        once_done_ = false;
    }
    force_listen_.store(true, std::memory_order_relaxed);

    std::unique_lock lock(once_mutex_);
    once_ready_.wait_for(lock, timeout, [this] { return once_done_; });
    if (!once_done_) {
        force_listen_.store(false, std::memory_order_relaxed);
        return std::nullopt;
    }
    return once_result_;
}

}  // namespace mimi::voice
