#include "voice/phrase_spotter.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>

namespace mimi::voice {
namespace {

constexpr std::string_view kTag = "spot";

// --- minimal UTF-8 handling ------------------------------------------------

std::size_t sequence_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;  // invalid lead byte; step one and carry on
}

std::uint32_t decode(std::string_view text, std::size_t at, std::size_t length) {
    const auto byte = [&](std::size_t i) -> std::uint32_t {
        return static_cast<unsigned char>(text[at + i]);
    };
    switch (length) {
        case 2: return ((byte(0) & 0x1F) << 6) | (byte(1) & 0x3F);
        case 3: return ((byte(0) & 0x0F) << 12) | ((byte(1) & 0x3F) << 6) | (byte(2) & 0x3F);
        case 4:
            return ((byte(0) & 0x07) << 18) | ((byte(1) & 0x3F) << 12) |
                   ((byte(2) & 0x3F) << 6) | (byte(3) & 0x3F);
        default: return byte(0);
    }
}

void encode(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// --- normalisation ---------------------------------------------------------

// Kanji whisper reaches for when it hears the wake word. 耳 ("ear") is by far
// the most common -- it is exactly homophonous with ミミ, so it is what the
// model actually writes most of the time.
constexpr std::array<std::uint32_t, 3> kMimiKanji{
    0x8033,  // 耳  ear
    0x7F8E,  // 美  as in 美々
    0x5B9F,  // 実  occasionally seen
};

bool is_hiragana(std::uint32_t cp) { return cp >= 0x3041 && cp <= 0x3096; }
bool is_japanese_punctuation(std::uint32_t cp) {
    return (cp >= 0x3000 && cp <= 0x303F) ||  // 、。〜 etc
           cp == 0xFF01 || cp == 0xFF1F ||     // ！ ？
           cp == 0xFF0C || cp == 0xFF0E;       // ， ．
}

// Normalising deletes characters (spaces, punctuation) and can add them (耳 ->
// ミミ), so an offset into the normalised text says nothing about where that
// point is in the original. Recording a boundary per input code point is what
// lets the command be sliced out of the text the user actually said.
struct Normalised {
    std::string text;
    // (bytes emitted so far, bytes consumed so far), after each input code point.
    std::vector<std::pair<std::size_t, std::size_t>> boundaries;
    // Output byte offsets where a homophone kanji was expanded, so a match that
    // came from 耳 rather than ミミ can be held to a stricter test.
    std::vector<std::size_t> kanji_folded;

    // Byte offset in the original corresponding to `out_bytes` in `text`.
    std::size_t source_offset(std::size_t out_bytes) const {
        for (const auto& [out, in] : boundaries) {
            if (out >= out_bytes) return in;
        }
        return boundaries.empty() ? 0 : boundaries.back().second;
    }
};

Normalised normalise_mapped(std::string_view text, bool fold_kanji) {
    Normalised result;
    result.text.reserve(text.size());
    result.boundaries.reserve(text.size());

    std::string& out = result.text;

    for (std::size_t i = 0; i < text.size();) {
        const auto length = std::min(sequence_length(static_cast<unsigned char>(text[i])),
                                     text.size() - i);
        std::uint32_t cp = decode(text, i, length);
        i += length;
        // Every path below ends here, so record the mapping once on the way out.
        const auto mark = [&] { result.boundaries.emplace_back(out.size(), i); };

        // Drop everything that carries no sound.
        if (cp < 0x80) {
            const auto c = static_cast<unsigned char>(cp);
            if (std::isspace(c) || std::ispunct(c)) { mark(); continue; }
            if (std::isalpha(c)) cp = static_cast<std::uint32_t>(std::tolower(c));
            out += static_cast<char>(cp);
            mark();
            continue;
        }
        if (is_japanese_punctuation(cp)) { mark(); continue; }

        // Full-width latin and digits -> ASCII.
        if (cp >= 0xFF01 && cp <= 0xFF5E) {
            cp = cp - 0xFF00 + 0x20;
            const auto c = static_cast<unsigned char>(cp);
            if (std::isspace(c) || std::ispunct(c)) { mark(); continue; }
            if (std::isalpha(c)) cp = static_cast<std::uint32_t>(std::tolower(c));
            out += static_cast<char>(cp);
            mark();
            continue;
        }
        // Full-width space.
        if (cp == 0x3000) { mark(); continue; }

        // Hiragana -> katakana, so みみ and ミミ compare equal.
        if (is_hiragana(cp)) cp += 0x60;

        // Homophone kanji -> katakana ミ, so 耳 matches ミミ. Only for the wake
        // word: doing this to arbitrary text would mangle the command.
        if (fold_kanji &&
            std::find(kMimiKanji.begin(), kMimiKanji.end(), cp) != kMimiKanji.end()) {
            encode(out, 0x30DF);  // ミ
            encode(out, 0x30DF);  // ミ  -- 耳 is read "mimi", two morae
            result.kanji_folded.push_back(out.size());
            mark();
            continue;
        }

        encode(out, cp);
        mark();
    }
    result.boundaries.emplace_back(out.size(), text.size());
    return result;
}

std::size_t count_codepoints(std::string_view text) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < text.size();) {
        i += std::min(sequence_length(static_cast<unsigned char>(text[i])), text.size() - i);
        ++n;
    }
    return n;
}

}  // namespace

std::string normalise_japanese(std::string_view text) {
    return normalise_mapped(text, true).text;
}

std::string drop_leading_codepoints(std::string_view text, std::size_t count) {
    std::size_t i = 0;
    for (std::size_t n = 0; n < count && i < text.size(); ++n) {
        i += std::min(sequence_length(static_cast<unsigned char>(text[i])), text.size() - i);
    }
    return std::string(text.substr(i));
}

PhraseSpotter::PhraseSpotter() : PhraseSpotter(Config{}) {}

PhraseSpotter::PhraseSpotter(Config config) : config_(std::move(config)) {
    for (const auto& phrase : config_.phrases) {
        auto normalised = normalise_japanese(phrase);
        if (!normalised.empty()) normalised_phrases_.push_back(std::move(normalised));
    }
    for (const auto& greeting : config_.greetings) {
        auto normalised = normalise_mapped(greeting, false).text;
        if (!normalised.empty()) normalised_greetings_.push_back(std::move(normalised));
    }
    // Longest first, so ねぇ wins over ね and ミミ over ミ.
    const auto by_length = [](const std::string& a, const std::string& b) {
        return a.size() > b.size();
    };
    std::sort(normalised_phrases_.begin(), normalised_phrases_.end(), by_length);
    std::sort(normalised_greetings_.begin(), normalised_greetings_.end(), by_length);

    log::debug(kTag, "{} wake phrases, {} greetings", normalised_phrases_.size(),
               normalised_greetings_.size());
}

namespace {

// Case particles, in katakana because normalisation has already folded
// hiragana. A name directly followed by one of these is the subject of the
// sentence, not the person being addressed: 耳が痛い is "my ear hurts", not
// "Mimi, ...". This is what keeps the homophone folding from making Mimi
// answer every time someone mentions their ear.
constexpr std::array<std::uint32_t, 7> kCaseParticles{
    0x30AC,  // ガ
    0x30CF,  // ハ
    0x30F2,  // ヲ
    0x30CB,  // ニ
    0x30D8,  // ヘ
    0x30CE,  // ノ
    0x30C7,  // デ
};

bool starts_with_case_particle(std::string_view text) {
    if (text.empty()) return false;
    const auto length =
        std::min(sequence_length(static_cast<unsigned char>(text[0])), text.size());
    const auto cp = decode(text, 0, length);
    return std::find(kCaseParticles.begin(), kCaseParticles.end(), cp) !=
           kCaseParticles.end();
}

}  // namespace

PhraseSpotter::Match PhraseSpotter::find(std::string_view transcript) const {
    Match match;
    if (transcript.empty()) return match;

    const Normalised normalised = normalise_mapped(transcript, true);
    if (normalised.text.empty()) return match;

    // Strip a leading greeting so "ねえミミ" and "ミミ" behave the same.
    std::size_t offset = 0;
    for (const auto& greeting : normalised_greetings_) {
        if (normalised.text.size() > greeting.size() &&
            normalised.text.compare(0, greeting.size(), greeting) == 0) {
            offset = greeting.size();
            break;
        }
    }
    const std::string_view body = std::string_view(normalised.text).substr(offset);

    for (const auto& phrase : normalised_phrases_) {
        const auto at = body.find(phrase);
        if (at == std::string_view::npos) continue;
        // Far enough in and it is subject matter, not an address.
        if (count_codepoints(body.substr(0, at)) > config_.search_window) continue;

        const std::size_t end = at + phrase.size();
        if (starts_with_case_particle(body.substr(end))) continue;

        match.matched = true;
        match.phrase = phrase;

        // Map back through the normalisation so the command keeps the spacing
        // and punctuation the user actually said. Counting code points in the
        // normalised text would drift, because normalising both drops
        // characters (spaces) and adds them (耳 -> ミミ).
        match.remainder = std::string(transcript.substr(
            std::min(normalised.source_offset(offset + end), transcript.size())));

        // Trim any punctuation or spacing left at the seam.
        std::size_t skip = 0;
        while (skip < match.remainder.size()) {
            const auto length = std::min(
                sequence_length(static_cast<unsigned char>(match.remainder[skip])),
                match.remainder.size() - skip);
            const auto cp = decode(match.remainder, skip, length);
            const bool droppable =
                is_japanese_punctuation(cp) ||
                (cp < 0x80 && (std::isspace(static_cast<unsigned char>(cp)) ||
                               std::ispunct(static_cast<unsigned char>(cp))));
            if (!droppable) break;
            skip += length;
        }
        match.remainder = match.remainder.substr(skip);
        return match;
    }
    return match;
}

}  // namespace mimi::voice
