#include "brain/tools.hpp"

#include "brain/shell.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <regex>
#include <sstream>
#include <sys/statvfs.h>
#include <thread>

namespace mimi::brain::tools {
namespace {

constexpr std::string_view kTag = "tools";

std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

std::string lowercase(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Browsers, in the order we ask them. Each entry is the app name plus the
// AppleScript for its front tab -- Safari calls it a document, the others a tab.
struct Browser {
    const char* app;
    const char* url_script;
    const char* title_script;
};

// Every Chromium browser answers the same script under its own name, so adding
// one is a line. Firefox is the exception: it exposes no scriptable tab model,
// so only its window title is reachable -- handled separately below.
constexpr std::array<Browser, 7> kBrowsers{{
    {"Brave Browser",
     "tell application \"Brave Browser\" to get URL of active tab of front window",
     "tell application \"Brave Browser\" to get title of active tab of front window"},
    {"Google Chrome",
     "tell application \"Google Chrome\" to get URL of active tab of front window",
     "tell application \"Google Chrome\" to get title of active tab of front window"},
    {"Arc", "tell application \"Arc\" to get URL of active tab of front window",
     "tell application \"Arc\" to get title of active tab of front window"},
    {"Safari", "tell application \"Safari\" to get URL of front document",
     "tell application \"Safari\" to get name of front document"},
    {"Microsoft Edge",
     "tell application \"Microsoft Edge\" to get URL of active tab of front window",
     "tell application \"Microsoft Edge\" to get title of active tab of front window"},
    {"Vivaldi", "tell application \"Vivaldi\" to get URL of active tab of front window",
     "tell application \"Vivaldi\" to get title of active tab of front window"},
    {"Opera", "tell application \"Opera\" to get URL of active tab of front window",
     "tell application \"Opera\" to get title of active tab of front window"},
}};

bool app_running(const char* name) {
    return osascript(std::string("application \"") + name + "\" is running") == "true";
}

double bytes_to_gb(double bytes) { return bytes / (1024.0 * 1024.0 * 1024.0); }

}  // namespace

// ---------------------------------------------------------------- reading ---

std::string what_time_ja() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);

    static constexpr std::array<const char*, 7> kDays{"日", "月", "火", "水", "木", "金", "土"};
    const int hour12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    const char* meridiem = local.tm_hour < 12 ? "午前" : "午後";

    std::ostringstream out;
    out << "今は" << meridiem << hour12 << "時" << local.tm_min << "分、"
        << (local.tm_mon + 1) << "月" << local.tm_mday << "日"
        << kDays[local.tm_wday] << "曜日です。";
    return out.str();
}

std::string what_time_en() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 128> buffer{};
    std::strftime(buffer.data(), buffer.size(), "It's %-I:%M %p on %A, %B %-d.", &local);
    return buffer.data();
}

BatteryStatus battery() {
    BatteryStatus status;
    const auto result = run("pmset", {"-g", "batt"});
    if (!result.ok()) return status;

    std::smatch match;
    if (std::regex_search(result.out, match, std::regex(R"((\d+)%; *(\w+))"))) {
        status.percent = std::stoi(match[1].str());
        const std::string raw = match[2].str();
        if (raw == "discharging")    status.state = "on battery";
        else if (raw == "charging")  status.state = "charging";
        else if (raw == "charged")   status.state = "fully charged";
        else                         status.state = raw;
    }
    if (std::regex_search(result.out, match, std::regex(R"((\d+:\d\d) remaining)"))) {
        status.remaining = match[1].str();
    }
    return status;
}

SystemInfo system_info() {
    SystemInfo info;

    if (const auto host = run("scutil", {"--get", "ComputerName"}); host.ok()) {
        info.host = trim(host.out);
    }
    if (const auto version = run("sw_vers", {"-productVersion"}); version.ok()) {
        info.macos = trim(version.out);
    }

    // Physical memory, then the page breakdown -- macOS "used" is not simply
    // total minus free, because inactive and compressed pages both count.
    if (const auto mem = run("sysctl", {"-n", "hw.memsize"}); mem.ok()) {
        info.memory_total_gb = bytes_to_gb(std::strtod(trim(mem.out).c_str(), nullptr));
    }
    if (const auto vm = run("vm_stat", {}); vm.ok()) {
        long page_size = 4096;
        std::smatch match;
        if (std::regex_search(vm.out, match, std::regex(R"(page size of (\d+) bytes)"))) {
            page_size = std::stol(match[1].str());
        }
        const auto pages = [&](const char* label) -> double {
            std::smatch found;
            const std::regex pattern(std::string(label) + R"(:\s+(\d+))");
            return std::regex_search(vm.out, found, pattern) ? std::stod(found[1].str()) : 0.0;
        };
        const double used_pages = pages("Pages active") + pages("Pages wired down") +
                                  pages("Pages occupied by compressor");
        info.memory_used_gb = bytes_to_gb(used_pages * static_cast<double>(page_size));
    }

    struct statvfs vfs{};
    if (::statvfs("/System/Volumes/Data", &vfs) == 0 || ::statvfs("/", &vfs) == 0) {
        info.disk_free_gb = bytes_to_gb(static_cast<double>(vfs.f_bavail) * vfs.f_frsize);
        info.disk_total_gb = bytes_to_gb(static_cast<double>(vfs.f_blocks) * vfs.f_frsize);
    }

    if (const auto uptime = run("uptime", {}); uptime.ok()) {
        std::smatch match;
        if (std::regex_search(uptime.out, match, std::regex(R"(up\s+(.+?),\s+\d+ user)"))) {
            info.uptime = match[1].str();
        }
    }
    return info;
}

std::string clipboard() {
    const auto result = run("pbpaste", {});
    return result.ok() ? result.out : std::string{};
}

BrowserTab current_tab() {
    BrowserTab tab;
    for (const auto& browser : kBrowsers) {
        if (!app_running(browser.app)) continue;
        const std::string url = osascript(browser.url_script);
        if (url.rfind("http", 0) != 0) continue;
        tab.url = url;
        tab.title = osascript(browser.title_script);
        tab.app = browser.app;
        return tab;
    }

    // Firefox has no scriptable tabs. The window title is still enough to know
    // what the user is looking at, which is what "summarise this" needs; the
    // page itself then has to come from the clipboard or be named aloud.
    if (app_running("firefox")) {
        const std::string title =
            osascript("tell application \"System Events\" to tell process \"firefox\" "
                      "to get name of front window");
        if (!title.empty()) {
            tab.app = "firefox";
            tab.title = title;
        }
    }
    return tab;
}

std::vector<std::string> running_apps() {
    const std::string raw = osascript(
        "tell application \"System Events\" to get name of every process "
        "whose background only is false");
    std::vector<std::string> apps;
    std::istringstream stream(raw);
    std::string item;
    while (std::getline(stream, item, ',')) {
        const std::string name = trim(item);
        if (!name.empty()) apps.push_back(name);
    }
    return apps;
}

int volume() {
    const std::string raw = osascript("output volume of (get volume settings)");
    try {
        return std::stoi(raw);
    } catch (...) {
        return -1;
    }
}

int brightness_percent() {
    const auto result = run("brightness", {"-l"});  // optional homebrew tool
    if (!result.ok()) return -1;
    std::smatch match;
    if (std::regex_search(result.out, match, std::regex(R"(brightness (\d*\.?\d+))"))) {
        return static_cast<int>(std::lround(std::stod(match[1].str()) * 100.0));
    }
    return -1;
}

// --------------------------------------------------------------- changing ---

void set_clipboard(const std::string& text) {
    // Through stdin rather than an argument: clipboard contents can be huge and
    // can contain anything at all.
    if (FILE* pipe = ::popen("pbcopy", "w")) {
        std::fwrite(text.data(), 1, text.size(), pipe);
        ::pclose(pipe);
    }
}

bool set_volume(int percent) {
    percent = std::clamp(percent, 0, 100);
    osascript("set volume output volume " + std::to_string(percent));
    return true;
}

bool nudge_volume(int delta) {
    const int current = volume();
    if (current < 0) return false;
    return set_volume(current + delta);
}

bool set_muted(bool muted) {
    osascript(std::string("set volume ") + (muted ? "with" : "without") + " output muted");
    return true;
}

bool set_brightness(int percent) {
    percent = std::clamp(percent, 0, 100);
    const auto result =
        run("brightness", {std::to_string(percent / 100.0)});
    if (!result.ok()) {
        log::debug(kTag, "brightness control needs `brew install brightness`");
        return false;
    }
    return true;
}

bool open_app(const std::string& name) {
    if (name.empty()) return false;
    return run("open", {"-a", name}).ok();
}

bool quit_app(const std::string& name) {
    if (name.empty()) return false;
    osascript("tell application \"" + applescript_quote(name) + "\" to quit");
    return true;
}

bool open_url(const std::string& url) {
    if (url.rfind("http", 0) != 0 && url.rfind("file://", 0) != 0) return false;
    return run("open", {url}).ok();
}

bool reveal_in_finder(const std::string& path) {
    if (path.empty()) return false;
    return run("open", {"-R", path}).ok();
}

namespace {
// System Events key codes for the media keys.
bool media_key(int code) {
    osascript("tell application \"System Events\" to key code " + std::to_string(code));
    return true;
}
}  // namespace

bool media_playpause() { return media_key(16 + 84); }  // NX_KEYTYPE_PLAY via F8
bool media_next()      { return media_key(17 + 84); }
bool media_previous()  { return media_key(18 + 84); }

std::string screenshot() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 64> stamp{};
    std::strftime(stamp.data(), stamp.size(), "%H-%M-%S", &local);

    const char* home = std::getenv("HOME");
    const std::string path =
        std::string(home ? home : ".") + "/Desktop/mimi_" + stamp.data() + ".png";

    if (!run("screencapture", {"-x", path}).ok()) return {};
    return path;
}

std::string screenshot_interactive() {
    const char* home = std::getenv("HOME");
    const std::string path = std::string(home ? home : ".") + "/Desktop/mimi_selection.png";
    if (!run("screencapture", {"-i", path}, 120).ok()) return {};
    return path;
}

bool notify(const std::string& title, const std::string& text) {
    osascript("display notification \"" + applescript_quote(text) + "\" with title \"" +
              applescript_quote(title) + "\"");
    return true;
}

bool lock_screen() {
    return run("pmset", {"displaysleepnow"}).ok();
}

bool sleep_display() { return lock_screen(); }

// ---------------------------------------------------------------- finding ---

std::vector<std::string> find_files(const std::string& name, int limit) {
    std::vector<std::string> hits;
    if (name.empty()) return hits;

    const auto result = run("mdfind", {"-name", name}, 15);
    if (!result.ok()) return hits;

    std::istringstream stream(result.out);
    std::string line;
    while (std::getline(stream, line) && static_cast<int>(hits.size()) < limit) {
        if (!trim(line).empty()) hits.push_back(trim(line));
    }
    return hits;
}

namespace {

std::string url_decode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            out += static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else if (text[i] == '+') {
            out += ' ';
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string strip_tags(const std::string& html) {
    std::string out;
    out.reserve(html.size() / 2);
    bool inside = false;
    for (char c : html) {
        if (c == '<') inside = true;
        else if (c == '>') inside = false;
        else if (!inside) out += c;
    }
    return out;
}

}  // namespace

std::vector<std::pair<std::string, std::string>> web_search(const std::string& query,
                                                            int limit) {
    std::vector<std::pair<std::string, std::string>> results;
    if (query.empty()) return results;

    // curl rather than httplib: httplib needs OpenSSL for TLS, and bundling
    // that into the .app buys nothing when macOS already ships curl. The argv
    // form means the query is never interpreted as syntax.
    const auto fetched = run("curl",
                             {"-fsSL", "--max-time", "15", "-A", "Mozilla/5.0",
                              "--data-urlencode", "q=" + query,
                              "https://html.duckduckgo.com/html/"},
                             20);
    if (!fetched.ok()) {
        log::warn(kTag, "search failed (curl {})", fetched.exit_code);
        return results;
    }
    const std::string body = fetched.out;

    // The result anchors carry the real destination in a uddg= parameter.
    // Custom delimiter: the pattern itself contains )" , which would otherwise
    // close a plain R"( ... )" literal early.
    const std::regex anchor(
        R"rx(<a[^>]*class="result__a"[^>]*href="([^"]+)"[^>]*>([\s\S]*?)</a>)rx");
    auto begin = std::sregex_iterator(body.begin(), body.end(), anchor);
    for (auto it = begin; it != std::sregex_iterator() &&
                          static_cast<int>(results.size()) < limit; ++it) {
        std::string href = (*it)[1].str();
        if (const auto at = href.find("uddg="); at != std::string::npos) {
            href = href.substr(at + 5);
            if (const auto amp = href.find('&'); amp != std::string::npos) {
                href = href.substr(0, amp);
            }
            href = url_decode(href);
        }
        if (href.rfind("http", 0) != 0) continue;
        // DuckDuckGo mixes sponsored results in with the organic ones; they
        // point at its own click tracker rather than at the destination.
        if (href.find("duckduckgo.com/y.js") != std::string::npos ||
            href.find("/y.js?ad_") != std::string::npos) {
            continue;
        }
        results.emplace_back(trim(strip_tags((*it)[2].str())), href);
    }
    return results;
}

std::string fetch_page_text(const std::string& url, std::string* title_out) {
    if (url.rfind("http", 0) != 0) return {};

    const auto fetched = run("curl",
                             {"-fsSL", "--max-time", "20", "-A", "Mozilla/5.0", url}, 25);
    if (!fetched.ok()) return {};
    std::string html = fetched.out;

    if (title_out != nullptr) {
        std::smatch match;
        if (std::regex_search(html, match, std::regex(R"(<title[^>]*>([\s\S]*?)</title>)",
                                                      std::regex::icase))) {
            *title_out = trim(strip_tags(match[1].str()));
        }
    }

    // Drop the elements that never contain reading material, then flatten.
    for (const char* tag : {"script", "style", "noscript", "svg", "nav", "header", "footer",
                            "aside", "form", "iframe"}) {
        const std::regex block(std::string("<") + tag + R"([\s\S]*?</)" + tag + ">",
                               std::regex::icase);
        html = std::regex_replace(html, block, " ");
    }
    std::string text = strip_tags(html);

    // Collapse the whitespace the markup left behind.
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

// ----------------------------------------------------------------- timers ---

std::optional<Reminder> parse_reminder(const std::string& text) {
    const std::string lower = lowercase(text);

    // Japanese: "20分後に休憩して" / "5秒後に"
    {
        const std::regex pattern(R"((\d+)\s*(秒|分|時間)後(?:に)?\s*(.*))");
        std::smatch match;
        if (std::regex_search(text, match, pattern)) {
            const int amount = std::stoi(match[1].str());
            const std::string unit = match[2].str();
            const int scale = unit == "秒" ? 1 : (unit == "分" ? 60 : 3600);
            Reminder reminder;
            reminder.delay = std::chrono::seconds{amount * scale};
            reminder.what = trim(match[3].str());
            // "休憩と教えて" -> "休憩": the verb is the request, not the subject.
            for (const char* tail : {"と教えて", "って教えて", "と言って", "って言って",
                                     "と知らせて", "とお知らせして", "して", "教えて"}) {
                const auto at = reminder.what.rfind(tail);
                if (at != std::string::npos &&
                    at + std::strlen(tail) == reminder.what.size() && at > 0) {
                    reminder.what = trim(reminder.what.substr(0, at));
                    break;
                }
            }
            return reminder;
        }
    }

    // English: "remind me in 20 minutes to stretch"
    if (lower.find("remind") != std::string::npos || lower.find("timer") != std::string::npos) {
        const std::regex pattern(R"(in\s+(\d+)\s*(second|minute|hour)s?\s*(?:to\s+)?(.*))");
        std::smatch match;
        if (std::regex_search(lower, match, pattern)) {
            const int amount = std::stoi(match[1].str());
            const std::string unit = match[2].str();
            const int scale = unit == "second" ? 1 : (unit == "minute" ? 60 : 3600);
            Reminder reminder;
            reminder.delay = std::chrono::seconds{amount * scale};
            reminder.what = trim(match[3].str());
            return reminder;
        }
    }
    return std::nullopt;
}

void schedule(std::chrono::seconds delay, std::string text,
              std::function<void(const std::string&)> on_fire) {
    std::thread([delay, text = std::move(text), on_fire = std::move(on_fire)] {
        std::this_thread::sleep_for(delay);
        if (on_fire) on_fire(text);
    }).detach();
}

std::string url_for_site(const std::string& spoken) {
    std::string name = lowercase(trim(spoken));
    if (name.empty()) return {};
    if (name.rfind("http", 0) == 0) return name;

    // Strip the words that surround a site name in speech.
    for (const char* filler : {"を開いて", "開いて", "を表示", "please", "the website",
                               "website", "dot com", "サイト"}) {
        for (auto at = name.find(filler); at != std::string::npos; at = name.find(filler)) {
            name.erase(at, std::strlen(filler));
        }
    }
    // Spoken site names arrive with spaces that are not in the domain.
    name.erase(std::remove_if(name.begin(), name.end(),
                              [](unsigned char c) { return std::isspace(c) != 0; }),
               name.end());
    if (name.empty()) return {};

    // Well-known names whose domain is not simply the name.
    static const std::vector<std::pair<const char*, const char*>> kKnown{
        {"youtube", "https://youtube.com"},   {"ユーチューブ", "https://youtube.com"},
        {"google", "https://google.com"},     {"グーグル", "https://google.com"},
        {"github", "https://github.com"},     {"twitter", "https://x.com"},
        {"x", "https://x.com"},               {"reddit", "https://reddit.com"},
        {"amazon", "https://amazon.co.jp"},   {"アマゾン", "https://amazon.co.jp"},
        {"spotify", "https://open.spotify.com"},
        {"netflix", "https://netflix.com"},   {"ネットフリックス", "https://netflix.com"},
        {"gmail", "https://mail.google.com"}, {"maps", "https://maps.google.com"},
        {"wikipedia", "https://wikipedia.org"},
    };
    for (const auto& [key, url] : kKnown) {
        if (name == key) return url;
    }
    if (name.find('.') != std::string::npos) return "https://" + name;
    return "https://" + name + ".com";
}

}  // namespace mimi::brain::tools
