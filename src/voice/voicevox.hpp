#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mimi::voice {

struct VoicevoxStyle {
    std::string speaker;  // character, e.g. 冥鳴ひまり
    std::string style;    // delivery, e.g. ノーマル
    int id = 0;           // style id passed to synthesis
};

// One mora of the synthesised speech: when its vowel starts, how long the
// vowel is held, and which vowel it is.
//
// This falls out of synthesis for free. VOICEVOX plans the prosody before it
// renders any audio, and that plan is a list of moras with explicit consonant
// and vowel durations -- so the mouth shapes and their timings are known
// exactly, rather than being guessed at from the waveform afterwards. It is
// what drives the avatar's lip sync.
struct Mora {
    double t = 0;       // seconds from the start of the clip
    double length = 0;  // how long the vowel is held
    // 'a', 'i', 'u', 'e', 'o', or '\0' for a mora with no open mouth shape:
    // ん, the geminate っ, and pauses all close it.
    char vowel = '\0';
};

// Offline Japanese TTS through the embedded VOICEVOX CORE library.
//
// This does NOT talk to the VOICEVOX.app HTTP engine on :50021 -- it links
// libvoicevox_core directly and runs the neural synthesiser in-process, so Mimi
// keeps her voice even when the VOICEVOX app is closed (or not installed). The
// runtime files (the core dylib, VOICEVOX's ONNX Runtime build, the Open JTalk
// dictionary and the .vvm voice models) are fetched by scripts/fetch_voicevox.sh
// and discovered at runtime; see find_root().
//
// The default character is 冥鳴ひまり (Meimei Himari), style ノーマル / id 14,
// which lives in models/vvms/1.vvm.
//
// Everything here degrades quietly: if the runtime files are missing or the
// library was not compiled in, available() is false and the caller falls back
// to AVSpeech / Kyoko.
class Voicevox {
public:
    struct Config {
        // Root of the fetched voicevox_core/ tree. Empty = auto-discover.
        std::filesystem::path root;
        std::string model_file = "1.vvm";  // the .vvm holding the default voice
        int style_id = 14;                 // 冥鳴ひまり / ノーマル
        double speed = 1.0;
        double pitch = 0.0;         // -0.15 .. 0.15
        double intonation = 1.15;   // >1 gives livelier, less flat delivery
        std::chrono::seconds timeout{30};
    };

    explicit Voicevox(Config config);
    ~Voicevox();

    // True once the engine has been initialised and can synthesise now.
    bool available() const;

    // Loads the ONNX Runtime, dictionary, synthesiser and voice model. Slow the
    // first time (the model is read from disk), so the caller runs it off the
    // construction path. Idempotent; returns available(). The `wait` argument is
    // accepted for interface compatibility and otherwise unused -- there is no
    // external process to wait on.
    bool ensure_running(std::chrono::seconds wait = std::chrono::seconds{40});

    // Every style the loaded model offers.
    std::vector<VoicevoxStyle> styles() const;

    // Japanese text -> 24 kHz mono WAV bytes. Empty on any failure, so callers
    // can simply fall through to another backend.
    std::vector<std::uint8_t> synthesize(const std::string& text) const;
    // The same, also handing back the mora timeline the prosody plan was built
    // from. `timeline` is left untouched when synthesis fails.
    std::vector<std::uint8_t> synthesize(const std::string& text,
                                         std::vector<Mora>& timeline) const;

    const Config& config() const noexcept { return config_; }
    void set_style(int id) { config_.style_id = id; }

    // Where the runtime tree was found (or would be looked for), for messages.
    static std::string install_hint();

private:
    struct Impl;
    Config config_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mimi::voice
