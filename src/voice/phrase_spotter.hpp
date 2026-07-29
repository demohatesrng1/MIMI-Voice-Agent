#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mimi::voice {

// Decides whether a transcript was addressed to Mimi.
//
// This replaces a trained wake-word classifier. openWakeWord ships no "hey
// mimi" model and its pretrained phrases are tied to the specific acoustics
// they were trained on, so the phrase is spotted in text instead: Silero
// segments speech, whisper transcribes the segment, and this matches the
// result. The wake phrase becomes a string you can edit rather than a model
// you have to retrain.
//
// Japanese makes the matching the interesting part. Whisper writes the same
// sounds several ways, and for ミミ it usually picks 耳 -- the kanji for "ear",
// pronounced identically. Matching the literal katakana would never fire.
class PhraseSpotter {
public:
    struct Config {
        // Written however you like; they are normalised before comparison.
        std::vector<std::string> phrases{"ミミ", "みみ", "耳", "mimi"};
        // Optional leading address forms, stripped before matching.
        std::vector<std::string> greetings{"ねえ", "ねぇ", "ね", "おい", "こんにちは",
                                           "hey", "hi", "ok", "okay"};
        // The phrase must appear within this many normalised characters of the
        // start, so "耳が痛い" ("my ear hurts") isn't taken as an address.
        std::size_t search_window = 8;
    };

    struct Match {
        bool matched = false;
        std::string phrase;     // which phrase hit, normalised
        std::string remainder;  // the command, with the address removed
        explicit operator bool() const noexcept { return matched; }
    };

    explicit PhraseSpotter(Config config);
    PhraseSpotter();

    Match find(std::string_view transcript) const;

    const Config& config() const noexcept { return config_; }

private:
    Config config_;
    std::vector<std::string> normalised_phrases_;
    std::vector<std::string> normalised_greetings_;
};

// Folds the many ways the same sounds get written into one form:
// hiragana to katakana, full-width to half-width, kanji homophones of the
// wake word, lowercased latin, and all punctuation and spacing removed.
std::string normalise_japanese(std::string_view text);

// UTF-8 aware prefix trim, in code points rather than bytes.
std::string drop_leading_codepoints(std::string_view text, std::size_t count);

}  // namespace mimi::voice
