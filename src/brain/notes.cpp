#include "brain/notes.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <fstream>
#include <cstdlib>
#include <sstream>

namespace mimi::brain {
namespace {

constexpr std::string_view kTag = "notes";

std::string now_iso() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &local);
    return buffer.data();
}

// A filename stem that sorts chronologically and is unique per second.
std::string stamp_id() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d-%H%M%S", &local);
    return buffer.data();
}

std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The first line of the body, capped so a dictated paragraph does not become a
// title the width of the screen. Cut on a UTF-8 boundary: a title is displayed,
// and half a kana renders as a replacement character.
std::string title_from(const std::string& body) {
    std::string first = trim(body.substr(0, body.find('\n')));
    constexpr std::size_t kMax = 60;
    if (first.size() <= kMax) return first;
    std::size_t cut = kMax;
    while (cut > 0 && (static_cast<unsigned char>(first[cut]) & 0xC0) == 0x80) --cut;
    return first.substr(0, cut) + "…";
}

}  // namespace

Notes::Notes() { dir_ = paths::data_subdir("notes").string(); }

std::string Notes::directory() const { return dir_; }

Note Notes::add(const std::string& body, const std::string& title) {
    Note note;
    const std::string text = trim(body);
    if (text.empty()) return note;

    note.id = stamp_id();
    note.created = now_iso();
    note.title = title.empty() ? title_from(text) : trim(title);
    note.body = text;

    // A second note in the same second would overwrite the first.
    std::filesystem::path path = std::filesystem::path(dir_) / (note.id + ".md");
    for (int suffix = 2; std::filesystem::exists(path); ++suffix) {
        note.id = stamp_id() + "-" + std::to_string(suffix);
        path = std::filesystem::path(dir_) / (note.id + ".md");
    }

    std::ofstream out(path);
    if (!out) {
        log::warn(kTag, "could not write {}", path.string());
        return Note{};
    }
    // Markdown with a title line, so the files read well on their own.
    out << "# " << note.title << "\n\n" << note.body << "\n";
    log::debug(kTag, "wrote note {}", note.id);
    return note;
}

bool Notes::append_to_latest(const std::string& text) {
    const Note note = latest();
    if (!note.valid()) return false;
    const std::string addition = trim(text);
    if (addition.empty()) return false;

    const std::filesystem::path path = std::filesystem::path(dir_) / (note.id + ".md");
    std::ofstream out(path, std::ios::app);
    if (!out) return false;
    out << "\n" << addition << "\n";
    return true;
}

std::vector<Note> Notes::all(int limit) const {
    std::vector<Note> found;
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(dir_, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (it->path().extension() == ".md") files.push_back(it->path());
    }
    // The id is a timestamp, so lexical order is *almost* chronological -- but
    // a second note in the same second gets a "-2" suffix, and '-' sorts below
    // '.', so "…215500-2.md" compares lower than "…215500.md" and the newer of
    // the two came out last. Compare the timestamp and the suffix separately.
    const auto parts = [](const std::filesystem::path& path) {
        const std::string stem = path.stem().string();
        constexpr std::size_t kStamp = 17;  // yyyy-mm-dd-hhmmss
        if (stem.size() <= kStamp) return std::pair<std::string, int>{stem, 1};
        return std::pair<std::string, int>{stem.substr(0, kStamp),
                                           std::atoi(stem.c_str() + kStamp + 1)};
    };
    std::sort(files.begin(), files.end(),
              [&parts](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return parts(a) > parts(b);  // newest first
              });

    for (const auto& path : files) {
        if (limit > 0 && static_cast<int>(found.size()) >= limit) break;
        Note note = get(path.stem().string());
        if (note.valid()) found.push_back(std::move(note));
    }
    return found;
}

Note Notes::get(const std::string& id) const {
    Note note;
    if (id.empty()) return note;
    const std::filesystem::path path = std::filesystem::path(dir_) / (id + ".md");
    std::ifstream in(path);
    if (!in) return note;

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string contents = buffer.str();

    note.id = id;
    // The stem is the creation time; parsing it back beats a second file.
    if (id.size() >= 17) {
        note.created = id.substr(0, 10) + "T" + id.substr(11, 2) + ":" + id.substr(13, 2) +
                       ":" + id.substr(15, 2);
    }

    // Strip the "# title" line back off, leaving the body as it was dictated.
    if (contents.rfind("# ", 0) == 0) {
        const auto newline = contents.find('\n');
        note.title = trim(contents.substr(2, newline - 2));
        note.body = newline == std::string::npos ? "" : trim(contents.substr(newline + 1));
    } else {
        note.body = trim(contents);
        note.title = title_from(note.body);
    }
    return note;
}

Note Notes::latest() const {
    const auto found = all(1);
    return found.empty() ? Note{} : found.front();
}

std::vector<Note> Notes::search(const std::string& query, int limit) const {
    std::vector<Note> hits;
    const std::string wanted = lowercase(trim(query));
    if (wanted.empty()) return hits;
    for (const auto& note : all()) {
        if (limit > 0 && static_cast<int>(hits.size()) >= limit) break;
        if (lowercase(note.title).find(wanted) != std::string::npos ||
            lowercase(note.body).find(wanted) != std::string::npos) {
            hits.push_back(note);
        }
    }
    return hits;
}

bool Notes::update(const std::string& id, const std::string& body,
                   const std::string& title) {
    if (id.empty()) return false;
    const std::filesystem::path path = std::filesystem::path(dir_) / (id + ".md");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;

    const std::string text = trim(body);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "# " << (title.empty() ? title_from(text) : trim(title)) << "\n\n" << text << "\n";
    return true;
}

namespace {

// Every character bigram in the text, lowercased. Bigrams are the smallest unit
// that carries meaning in both scripts: "会議" is one, and so is "me" in
// "meeting" -- short enough to survive different phrasings, long enough that
// matches are not accidental.
std::vector<std::string> bigrams(const std::string& text) {
    std::vector<std::string> out;
    const std::string lowered = lowercase(text);
    // Step by whole UTF-8 characters, so a bigram never cuts a kana in half.
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < lowered.size();) {
        starts.push_back(i);
        const unsigned char c = lowered[i];
        i += c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
    }
    for (std::size_t i = 0; i + 1 < starts.size(); ++i) {
        const std::size_t end = i + 2 < starts.size() ? starts[i + 2] : lowered.size();
        std::string pair = lowered.substr(starts[i], end - starts[i]);
        // Pure punctuation and whitespace carry nothing.
        if (pair.find_first_not_of(" \t\r\n、。,.!?？！「」") != std::string::npos) {
            out.push_back(std::move(pair));
        }
    }
    return out;
}

}  // namespace

std::vector<Note> Notes::relevant(const std::string& question, int limit) const {
    std::vector<Note> hits;
    const auto wanted = bigrams(trim(question));
    if (wanted.empty()) return hits;

    std::vector<std::pair<int, Note>> scored;
    for (auto& note : all()) {
        const std::string haystack = lowercase(note.title + " " + note.body);
        int score = 0;
        for (const auto& pair : wanted) {
            if (haystack.find(pair) != std::string::npos) ++score;
        }
        // Two shared bigrams is noise -- most questions share が or the with
        // something. Three is a real overlap of subject matter.
        if (score >= 3) scored.emplace_back(score, note);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (auto& [score, note] : scored) {
        if (limit > 0 && static_cast<int>(hits.size()) >= limit) break;
        hits.push_back(std::move(note));
    }
    return hits;
}

bool Notes::remove(const std::string& id) {
    if (id.empty()) return false;
    std::error_code ec;
    return std::filesystem::remove(std::filesystem::path(dir_) / (id + ".md"), ec);
}

}  // namespace mimi::brain
