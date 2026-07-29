// mimi_say -- list the installed voices and try them.
//
//   mimi_say --list            every ja-JP voice, with its quality tier
//   mimi_say --all             list every language
//   mimi_say "こんにちは"        speak with the current default
//   mimi_say --voice Name "..." speak with a specific one

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
    bool list = false, all = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list") list = true;
        else if (arg == "--all") { list = true; all = true; }
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

    mimi::voice::Speaker::Config config;
    if (!voice.empty()) config.voice_name = voice;
    mimi::voice::Speaker speaker(std::move(config));

    bool done = false;
    speaker.speak(text, [&](bool) { done = true; });
    while (!done) std::this_thread::sleep_for(std::chrono::milliseconds{50});
    return 0;
}
