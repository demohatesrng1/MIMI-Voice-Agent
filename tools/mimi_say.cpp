// mimi_say -- list the installed voices and try them.
//
//   mimi_say --list            every ja-JP voice, with its quality tier
//   mimi_say --all             list every language
//   mimi_say "こんにちは"        speak with the current default
//   mimi_say --voice Name "..." speak with a specific one
//   mimi_say --visemes "..."   print the mora timeline the avatar's mouth uses

#include "core/log.hpp"
#include "voice/tts.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    std::string text = "こんにちは、ミミです。今日はいい天気ですね。何かお手伝いできることはありますか。";
    std::string voice;
    bool list = false, all = false, visemes = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list") list = true;
        else if (arg == "--all") { list = true; all = true; }
        else if (arg == "--visemes") visemes = true;
        else if (arg == "--voice" && i + 1 < argc) voice = argv[++i];
        else if (!arg.empty() && arg[0] != '-') text = arg;
    }

    if (list) {
        const auto voices = all ? mimi::voice::Speaker::voices()
                                : mimi::voice::Speaker::voices_for("ja");
        std::printf("\n%-22s %-10s %-9s %s\n", "name", "language", "quality", "identifier");
        std::printf("%s\n", std::string(96, '-').c_str());
        for (const auto& v : voices) {
            std::printf("%-22s %-10s %-9s %s\n", v.name.c_str(), v.language.c_str(),
                        v.enhanced ? "ENHANCED" : "default", v.identifier.c_str());
        }
        std::printf("\n%zu voices\n", voices.size());
        return 0;
    }

    const bool want_voicevox = voice.empty();  // a named voice means the system one
    mimi::voice::Speaker::Config config;
    if (!want_voicevox) {
        config.voice_name = voice;
        config.prefer_voicevox = false;
    }
    mimi::voice::Speaker speaker(std::move(config));

    // VOICEVOX CORE initialises lazily (loading the model takes a moment), so
    // bring it up before speaking -- otherwise this falls back to the system voice.
    if (want_voicevox && speaker.start_voicevox()) {
        std::printf("voice: VOICEVOX CORE (冥鳴ひまり)\n");
    } else {
        std::printf("voice: system (%s)\n", speaker.config().voice_name.c_str());
    }

    // The avatar's lip sync is otherwise invisible until it is wrong, and the
    // one thing that makes it wrong is a timeline that drifts against the
    // audio. Printing it next to the clip is how you check the two agree.
    if (visemes) {
        speaker.on_visemes([](const std::vector<mimi::voice::Mora>& timeline, double delay) {
            std::printf("\n%zu moras, %.0f ms of playback latency allowed for\n", timeline.size(),
                        delay * 1000);
            std::printf("%8s  %8s  %s\n", "start", "held", "shape");
            for (const auto& mora : timeline) {
                std::printf("%7.3fs  %7.3fs  %c\n", mora.t, mora.length,
                            mora.vowel == '\0' ? '-' : mora.vowel);
            }
            if (!timeline.empty()) {
                const auto& last = timeline.back();
                std::printf("\nmouth closes at %.3fs\n", last.t + last.length);
            }
        });
    }

    bool done = false;
    speaker.speak(text, [&](bool) { done = true; });
    while (!done) std::this_thread::sleep_for(std::chrono::milliseconds{50});
    return 0;
}
