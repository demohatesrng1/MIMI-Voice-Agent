#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mimi::voice {

struct VoicevoxStyle {
    std::string speaker;  // character, e.g. 四国めたん
    std::string style;    // delivery, e.g. ノーマル / あまあま
    int id = 0;           // what /synthesis wants
};

// Client for a local VOICEVOX engine.
//
// The system voices on this Mac are all compact or Eloquence quality, which is
// why Mimi sounded synthetic: those are formant synthesisers with a hard
// ceiling, not something tuning can rescue. VOICEVOX is neural, free, runs
// entirely locally on :50021, and is built specifically for Japanese character
// voices -- much closer to a person, and a better fit for ミミ than any
// general-purpose TTS.
//
// Everything here degrades quietly: if the engine is not installed or not
// running, available() is false and the caller falls back to AVSpeech.
class Voicevox {
public:
    struct Config {
        std::string host = "http://localhost:50021";
        int style_id = 2;  // 四国めたん / ノーマル -- a calm, clear default
        double speed = 1.0;
        double pitch = 0.0;         // -0.15 .. 0.15
        double intonation = 1.15;   // >1 gives livelier, less flat delivery
        std::chrono::seconds timeout{30};
    };

    explicit Voicevox(Config config);
    ~Voicevox();

    // Is an engine answering right now?
    bool available() const;

    // Launches VOICEVOX.app if it is installed but not running, then waits for
    // the engine to answer. False if it is not installed at all.
    bool ensure_running(std::chrono::seconds wait = std::chrono::seconds{40});

    // Every character and style the engine offers.
    std::vector<VoicevoxStyle> styles() const;

    // Japanese text -> 24 kHz mono WAV bytes. Empty on any failure, so callers
    // can simply fall through to another backend.
    std::vector<std::uint8_t> synthesize(const std::string& text) const;

    const Config& config() const noexcept { return config_; }
    void set_style(int id) { config_.style_id = id; }

    // Where the app would be, for the "please install it" message.
    static std::string install_hint();

private:
    struct Impl;
    Config config_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mimi::voice
