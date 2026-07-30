// mimi_route -- drive the router from the terminal, no microphone involved.
//
//   mimi_route "今何時ですか"      one utterance
//   mimi_route --dry               run the read-only suite
//   mimi_route                     interactive

#include "brain/ollama.hpp"
#include "brain/router.hpp"
#include "brain/tools.hpp"
#include "core/log.hpp"

#include <cstdio>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    mimi::brain::Ollama ollama({});
    if (!ollama.ensure_running()) {
        std::fputs("Could not start Ollama. Is it installed? (brew install ollama)\n",
                   stderr);
        return 1;
    }
    mimi::brain::Router router(ollama);
    router.on_reminder([](const std::string& text) {
        std::printf("\n[reminder fired] %s\n", text.c_str());
    });

    const auto show = [&](const std::string& utterance) {
        const auto reply = router.route(utterance);
        std::printf("  %-28s -> [%s%s] %s\n", utterance.c_str(), reply.action.c_str(),
                    reply.acted ? "*" : "", reply.text.c_str());
    };

    if (argc > 1 && std::string(argv[1]) == "--dry") {
        // "--dry" meant read-only by convention and not in fact: the reminder
        // case armed a real timer that went off minutes later, on whatever
        // machine happened to run the suite.
        mimi::brain::tools::set_rehearsing(true);
        std::puts("\nread-only commands (nothing is actually done)\n");
        for (const char* q : {"今何時ですか", "バッテリーはどのくらい",
                              "システムの空き容量を教えて", "音量はいくつ",
                              "5分後に休憩と教えて", "日本の首都はどこ"}) {
            show(q);
        }
        std::puts("");
        return 0;
    }

    if (argc > 1) {
        show(argv[1]);
        return 0;
    }

    std::puts("type an utterance, blank line to quit\n");
    std::string line;
    while (std::fputs("> ", stdout), std::fflush(stdout), std::getline(std::cin, line)) {
        if (line.empty()) break;
        show(line);
    }
    return 0;
}
