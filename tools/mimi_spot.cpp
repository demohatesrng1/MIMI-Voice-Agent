// mimi_spot -- check the wake-phrase matcher against the many ways whisper
// writes the same sounds.
//
//   mimi_spot                    run the built-in cases
//   mimi_spot "ねえミミ、今何時"   try one string

#include "core/log.hpp"
#include "voice/phrase_spotter.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Case {
    const char* input;
    bool should_match;
    const char* expected_remainder;  // nullptr = don't check
};

// The homophone cases are the ones that matter: whisper writes ミミ as 耳 far
// more often than as katakana, because 耳 is a real word and ミミ is a name.
const std::vector<Case> kCases{
    {"ねえミミ、今何時ですか",      true,  "今何時ですか"},
    {"ねえ耳、今何時ですか?",       true,  "今何時ですか?"},
    {"ねえ耳、YouTubeを開いて",     true,  "YouTubeを開いて"},
    {"ミミ、電気を消して",          true,  "電気を消して"},
    {"みみ、天気は？",              true,  "天気は？"},
    {"耳、音楽をかけて",            true,  "音楽をかけて"},
    {"Hey Mimi, what time is it",   true,  "what time is it"},
    {"hey mimi open youtube",       true,  "open youtube"},
    {"ねぇミミ　スクリーンショット", true,  "スクリーンショット"},

    // Must NOT fire: the wake word appears, but not as an address.
    {"耳が痛いので病院に行きたい",   false, nullptr},
    {"今日はいい天気ですね",         false, nullptr},
    {"the ear infection is painful", false, nullptr},
    {"",                             false, nullptr},
};

}  // namespace

int main(int argc, char** argv) {
    mimi::log::configure_from_env();
    mimi::voice::PhraseSpotter spotter;

    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            const auto match = spotter.find(argv[i]);
            std::printf("%-40s -> %s", argv[i], match ? "MATCH" : "no match");
            if (match) std::printf("  phrase=%s  command=\"%s\"", match.phrase.c_str(),
                                   match.remainder.c_str());
            std::printf("\n");
        }
        return 0;
    }

    int failures = 0;
    std::printf("\n%-34s %-9s %s\n", "input", "expect", "result");
    std::printf("%s\n", std::string(78, '-').c_str());

    for (const auto& test : kCases) {
        const auto match = spotter.find(test.input);
        bool ok = (static_cast<bool>(match) == test.should_match);
        if (ok && match && test.expected_remainder != nullptr) {
            ok = (match.remainder == test.expected_remainder);
        }
        if (!ok) ++failures;

        std::printf("%-34s %-9s %-6s", test.input[0] ? test.input : "(empty)",
                    test.should_match ? "match" : "reject", match ? "match" : "reject");
        if (match) std::printf("  \"%s\"", match.remainder.c_str());
        if (!ok) {
            std::printf("   <== FAIL");
            if (test.expected_remainder != nullptr && match) {
                std::printf(" (wanted \"%s\")", test.expected_remainder);
            }
        }
        std::printf("\n");
    }

    std::printf("\n%zu cases, %d failed\n", kCases.size(), failures);
    return failures == 0 ? 0 : 1;
}
