#pragma once

#include "voice/voicevox.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mimi::voice {

struct VoiceInfo {
    std::string identifier;  // e.g. com.apple.voice.compact.ja-JP.Kyoko
    std::string name;        // e.g. Kyoko
    std::string language;    // e.g. ja-JP
    bool enhanced = false;   // premium/enhanced quality variant
};

// Speech synthesis through AVSpeechSynthesizer.
//
// macOS ships good Japanese voices (Kyoko and friends) and Piper ships none at
// all -- there is no `ja` voice in piper-voices -- so the system synthesiser is
// both the better and the only local option here.
//
// Using the framework rather than shelling out to `say` matters for one
// feature: barge-in. stop() cuts the audio at the next sample boundary, so
// interrupting Mimi mid-sentence is instant rather than waiting on a process to
// die.
class Speaker {
public:
    struct Config {
        std::string language = "ja-JP";
        std::string voice_name = "Kyoko";  // empty = system default for language
        float rate = 0.48f;                // 0..1, AVSpeechUtterance scale
        float pitch = 1.0f;                // 0.5..2
        float volume = 1.0f;               // 0..1

        // Prefer VOICEVOX CORE (embedded, offline -- 冥鳴ひまり) over the system
        // voice. The system voices available here are all compact or Eloquence
        // quality, so this is the difference between sounding synthetic and
        // sounding like a person.
        bool prefer_voicevox = true;
        Voicevox::Config voicevox{};
    };

    // Opaque; defined in tts.mm so AVFoundation stays out of this header.
    // Public only because the Objective-C delegate has to name it.
    struct Impl;

    explicit Speaker(Config config);
    ~Speaker();

    Speaker(const Speaker&) = delete;
    Speaker& operator=(const Speaker&) = delete;

    // Starts speaking and returns immediately. `on_finished` runs when the
    // utterance completes or is cancelled -- the Listener uses it to close the
    // turn and open the follow-up window.
    void speak(const std::string& text, std::function<void(bool completed)> on_finished = {});

    // Cuts playback immediately. Safe to call when nothing is speaking.
    void stop();

    bool speaking() const;

    void set_voice(const std::string& name_or_identifier);
    void set_rate(float rate);
    const Config& config() const noexcept { return config_; }

    // True when speech is currently coming from VOICEVOX rather than the
    // system synthesiser.
    bool using_voicevox() const;
    // Tries to bring VOICEVOX up. Safe to call when it is not installed.
    bool start_voicevox();

    // Every installed voice, for the settings UI.
    static std::vector<VoiceInfo> voices();
    // Only those matching a BCP-47 language prefix, e.g. "ja".
    static std::vector<VoiceInfo> voices_for(const std::string& language_prefix);

private:
    bool speak_voicevox(const std::string& text, std::uint64_t generation);

    Config config_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mimi::voice
