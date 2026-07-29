#include "brain/journal.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"

#include <array>
#include <ctime>
#include <fstream>

namespace mimi::brain {
namespace {

constexpr std::string_view kTag = "journal";

std::filesystem::path day_file(const std::string& day) {
    return paths::data_subdir("journal") / (day + ".jsonl");
}

std::string now_iso() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &local);
    return buffer.data();
}

}  // namespace

Journal::Journal() { paths::data_subdir("journal"); }

std::string Journal::today() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 16> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d", &local);
    return buffer.data();
}

void Journal::log(const std::string& kind, nlohmann::json record) {
    nlohmann::json event{
        {"time", now_iso()}, {"kind", kind}, {"record", std::move(record)}};

    const auto path = day_file(today());
    std::ofstream file(path, std::ios::app);
    if (!file) {
        log::warn(kTag, "cannot write {}", path.string());
        return;
    }
    // One object per line, unescaped, so the file stays readable in an editor.
    file << event.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
}

std::vector<Event> Journal::read_day(const std::string& day) const {
    std::vector<Event> events;
    const auto path = day_file(day.empty() ? today() : day);

    std::ifstream file(path);
    if (!file) return events;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            const auto parsed = nlohmann::json::parse(line);
            events.push_back({parsed.value("time", ""), parsed.value("kind", ""),
                              parsed.value("record", nlohmann::json::object())});
        } catch (const std::exception&) {
            // A truncated final line from a crash; the rest is still good.
        }
    }
    return events;
}

std::vector<std::string> Journal::days() const {
    std::vector<std::string> found;
    std::error_code ec;
    const auto dir = paths::data_subdir("journal");
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".jsonl") found.push_back(entry.path().stem().string());
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::vector<Event> Journal::read_all() const {
    std::vector<Event> events;
    for (const auto& day : days()) {
        auto some = read_day(day);
        events.insert(events.end(), std::make_move_iterator(some.begin()),
                      std::make_move_iterator(some.end()));
    }
    return events;
}

bool Journal::clear_day(const std::string& day) {
    std::error_code ec;
    return std::filesystem::remove(day_file(day.empty() ? today() : day), ec);
}

}  // namespace mimi::brain
