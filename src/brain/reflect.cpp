#include "brain/reflect.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace mimi::brain {
namespace {

constexpr std::string_view kTag = "reflect";

const char* kDigestPrompt =
    "あなたはミミ。ユーザーの一日について短いメモを書きます。"
    "以下はその日の記録です。読んだページ、検索、質問などが入っています。\n"
    "3〜5文の日本語で、親しみやすく一人称で書いてください。"
    "出来事を並べるのではなく、何に興味を持っていたか、何をしようとしていたかに触れてください。"
    "記録にないことは書かないこと。記号や箇条書きは使わないこと。";

const char* kProfilePrompt =
    "ユーザーの短いプロフィールを更新します。"
    "既存のプロフィールと今日のメモをもとに、150語以内の日本語で書き直してください。"
    "長く続いている興味や性格は残し、新しく分かったことを取り込み、"
    "一度きりの出来事は落としてください。箇条書きや見出しは使わないこと。";

std::filesystem::path profile_path() { return paths::data_file("profile.md"); }

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// One line per event, using whichever field actually carries the meaning.
std::string summarise(const Event& event) {
    const auto& record = event.record;
    for (const char* key : {"summary", "text", "query", "question", "title", "url", "app"}) {
        if (record.contains(key) && record[key].is_string()) {
            const std::string value = record[key];
            if (!value.empty()) return value;
        }
    }
    return record.dump();
}

}  // namespace

Reflection::Reflection(Ollama& ollama, Journal& journal)
    : ollama_(ollama), journal_(journal) {}

std::optional<std::string> Reflection::run(const std::string& day, bool clear_after) {
    const std::string target = day.empty() ? Journal::today() : day;
    const auto events = journal_.read_day(target);
    if (events.empty()) {
        log::debug(kTag, "nothing recorded for {}", target);
        return std::nullopt;
    }

    std::ostringstream log_text;
    for (const auto& event : events) {
        log_text << "- [" << event.kind << "] " << summarise(event) << '\n';
    }

    ChatOptions options;
    options.max_tokens = 320;
    const auto note = ollama_.chat(kDigestPrompt, "今日の記録:\n" + log_text.str(), options);
    if (!note) {
        log::warn(kTag, "could not write the note: {}", note.error);
        return std::nullopt;
    }

    save_digest(target, note.text);
    update_profile(note.text);
    if (clear_after) journal_.clear_day(target);

    log::info(kTag, "reflected on {} ({} events)", target, events.size());
    return note.text;
}

void Reflection::save_digest(const std::string& day, const std::string& note) const {
    const auto path = paths::data_subdir("digests") / (day + ".md");
    std::ofstream file(path);
    if (file) file << note << '\n';
}

void Reflection::update_profile(const std::string& note) {
    const std::string existing = profile();

    ChatOptions options;
    options.max_tokens = 320;
    const auto updated = ollama_.chat(
        kProfilePrompt,
        "今のプロフィール:\n" + (existing.empty() ? "（まだありません）" : existing) +
            "\n\n今日のメモ:\n" + note,
        options);
    if (!updated) return;

    std::ofstream file(profile_path());
    if (file) file << updated.text << '\n';
}

std::string Reflection::profile() const { return read_file(profile_path()); }

std::vector<std::string> Reflection::recent_digests(int limit) const {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(paths::data_subdir("digests"), ec)) {
        if (entry.path().extension() == ".md") files.push_back(entry.path());
    }
    std::sort(files.rbegin(), files.rend());

    std::vector<std::string> out;
    for (const auto& path : files) {
        if (static_cast<int>(out.size()) >= limit) break;
        out.push_back(path.stem().string() + "\n" + read_file(path));
    }
    return out;
}

}  // namespace mimi::brain
