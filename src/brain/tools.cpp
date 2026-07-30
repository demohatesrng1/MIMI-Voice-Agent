#include "brain/tools.hpp"

#include "brain/shell.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <regex>
#include <fstream>
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

namespace {
bool g_rehearsing = false;
}  // namespace

void set_rehearsing(bool rehearsing) { g_rehearsing = rehearsing; }
bool rehearsing() { return g_rehearsing; }

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
    if (g_rehearsing) return true;
    percent = std::clamp(percent, 0, 100);
    osascript("set volume output volume " + std::to_string(percent));
    return true;
}

bool nudge_volume(int delta) {
    if (g_rehearsing) return true;
    const int current = volume();
    if (current < 0) return false;
    return set_volume(current + delta);
}

bool set_muted(bool muted) {
    if (g_rehearsing) return true;
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

namespace {

// Removes each filler phrase from `text`, case-insensitively for ASCII.
//
// The offset must come from the original string for multibyte phrases:
// `lowercase` is byte-wise, so an offset found in a lowercased copy of
// Japanese text lands mid-character and erasing there shears a kana in half.
// Same defect, same fix as strip_words() in the router.
std::string strip_fillers(std::string text, std::initializer_list<const char*> fillers) {
    for (const char* filler : fillers) {
        const std::size_t length = std::strlen(filler);
        const bool ascii = std::all_of(filler, filler + length, [](unsigned char c) {
            return c < 0x80;
        });
        for (;;) {
            const auto at = ascii ? lowercase(text).find(filler) : text.find(filler);
            if (at == std::string::npos) break;
            text.erase(at, length);
        }
    }
    return trim(text);
}

// Names as they are spoken -> the bundle name macOS actually has on disk.
// `open -a` is picky: it matches a name almost exactly, so "chrome", "vs code"
// and "ライン" all fail against it even though the app is installed.
const std::vector<std::pair<const char*, const char*>>& app_aliases() {
    static const std::vector<std::pair<const char*, const char*>> kAliases{
        {"chrome", "Google Chrome"},        {"クローム", "Google Chrome"},
        {"グーグルクローム", "Google Chrome"},
        {"safari", "Safari"},               {"サファリ", "Safari"},
        {"firefox", "Firefox"},             {"ファイアフォックス", "Firefox"},
        {"edge", "Microsoft Edge"},         {"arc", "Arc"},
        {"vscode", "Visual Studio Code"},   {"vs code", "Visual Studio Code"},
        {"visual studio", "Visual Studio Code"},
        {"code", "Visual Studio Code"},     {"エディタ", "Visual Studio Code"},
        {"xcode", "Xcode"},                 {"terminal", "Terminal"},
        {"ターミナル", "Terminal"},          {"iterm", "iTerm"},
        {"finder", "Finder"},               {"ファインダー", "Finder"},
        {"spotify", "Spotify"},             {"スポティファイ", "Spotify"},
        {"music", "Music"},                 {"ミュージック", "Music"},
        {"itunes", "Music"},                {"mail", "Mail"},
        {"メール", "Mail"},                  {"messages", "Messages"},
        {"メッセージ", "Messages"},          {"line", "LINE"},
        {"ライン", "LINE"},                  {"discord", "Discord"},
        {"ディスコード", "Discord"},         {"slack", "Slack"},
        {"スラック", "Slack"},               {"zoom", "zoom.us"},
        {"ズーム", "zoom.us"},               {"notes", "Notes"},
        {"メモ", "Notes"},                   {"notion", "Notion"},
        {"calendar", "Calendar"},           {"カレンダー", "Calendar"},
        {"photos", "Photos"},               {"写真", "Photos"},
        {"preview", "Preview"},             {"プレビュー", "Preview"},
        {"calculator", "Calculator"},       {"計算機", "Calculator"},
        {"電卓", "Calculator"},              {"settings", "System Settings"},
        {"system settings", "System Settings"},
        {"preferences", "System Settings"}, {"設定", "System Settings"},
        {"システム設定", "System Settings"}, {"activity monitor", "Activity Monitor"},
        {"app store", "App Store"},         {"telegram", "Telegram"},
        {"whatsapp", "WhatsApp"},           {"obsidian", "Obsidian"},
        {"figma", "Figma"},                 {"steam", "Steam"},
        {"chatgpt", "ChatGPT"},             {"word", "Microsoft Word"},
        {"excel", "Microsoft Excel"},       {"powerpoint", "Microsoft PowerPoint"},
        {"ワード", "Microsoft Word"},        {"パワポ", "Microsoft PowerPoint"},
        // Browsers other than Chrome, so "ブラウザを開いて" lands somewhere real.
        {"brave", "Brave Browser"},         {"ブレイブ", "Brave Browser"},
        {"ブレイブブラウザ", "Brave Browser"},
        {"browser", "Brave Browser"},       {"ブラウザ", "Brave Browser"},
        // The rest of what people actually keep on a Mac.
        {"claude", "Claude"},               {"クロード", "Claude"},
        {"anki", "Anki"},                   {"アンキ", "Anki"},
        {"vlc", "VLC"},                     {"obs", "OBS"},
        {"docker", "Docker"},               {"ドッカー", "Docker"},
        {"github", "GitHub Desktop"},       {"github desktop", "GitHub Desktop"},
        {"kakao", "KakaoTalk"},             {"kakaotalk", "KakaoTalk"},
        {"カカオトーク", "KakaoTalk"},       {"カカオ", "KakaoTalk"},
        {"wechat", "WeChat"},               {"ウィーチャット", "WeChat"},
        {"telegram", "Telegram"},           {"テレグラム", "Telegram"},
        {"vpn", "ProtonVPN"},               {"protonvpn", "ProtonVPN"},
        {"wireshark", "Wireshark"},         {"ollama", "Ollama"},
        {"voicevox", "VOICEVOX"},           {"ボイスボックス", "VOICEVOX"},
        {"imovie", "iMovie"},               {"reminders", "Reminders"},
        {"リマインダー", "Reminders"},       {"shortcuts", "Shortcuts"},
        {"ショートカット", "Shortcuts"},     {"weather", "Weather"},
        {"天気", "Weather"},                 {"maps", "Maps"},
        {"地図", "Maps"},                    {"books", "Books"},
        {"quicktime", "QuickTime Player"},  {"textedit", "TextEdit"},
        {"テキストエディット", "TextEdit"},
        {"stickies", "Stickies"},           {"contacts", "Contacts"},
        {"連絡先", "Contacts"},              {"passwords", "Passwords"},
        {"パスワード", "Passwords"},         {"facetime", "FaceTime"},
        {"フェイスタイム", "FaceTime"},      {"podcasts", "Podcasts"},
        {"dictionary", "Dictionary"},       {"辞書", "Dictionary"},
    };
    return kAliases;
}

// Every .app installed, so a spoken name can be matched against reality rather
// than against a hardcoded list.
const std::vector<std::string>& installed_apps() {
    static const std::vector<std::string> kApps = [] {
        std::vector<std::string> found;
        const char* home = std::getenv("HOME");
        std::vector<std::string> roots{"/Applications", "/Applications/Utilities",
                                       "/System/Applications",
                                       "/System/Applications/Utilities"};
        if (home) roots.push_back(std::string(home) + "/Applications");
        for (const auto& root : roots) {
            std::error_code ec;
            for (std::filesystem::directory_iterator it(root, ec), end; it != end;
                 it.increment(ec)) {
                if (ec) break;
                const auto path = it->path();
                if (path.extension() == ".app") found.push_back(path.stem().string());
            }
        }
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    }();
    return kApps;
}

std::string squash(std::string text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isspace(c) || c == '.' || c == '-' || c == '_') continue;
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

// The spoken name, cleaned of the words that surround an app name in speech.
std::string strip_app_filler(const std::string& spoken) {
    return strip_fillers(spoken, {"を起動して", "を起動", "を開いて", "を開く", "開いて",
                                  "アプリ", "というアプリ", "please", "the app", " app",
                                  "launch ", "open ", "start "});
}

// A spoken app name -> something `open -a` will accept, or empty.
// True when an app of this name is actually on the disk.
//
// The alias table is a pronunciation guide, not an inventory: it maps "クローム"
// to "Google Chrome" whether or not Chrome is installed. Returning an alias
// unchecked meant `open -a "Google Chrome"` failed on a machine without it, the
// caller fell through to its website fallback, and "クロームを開いて" ended up
// opening a fabricated https://クローム.com. Every path here now has to name
// something real.
bool is_installed(const std::string& name) {
    if (name.empty()) return false;
    const std::string key = squash(name);
    const auto& apps = installed_apps();
    return std::any_of(apps.begin(), apps.end(),
                       [&](const std::string& app) { return squash(app) == key; });
}

std::string resolve_app(const std::string& spoken) {
    const std::string cleaned = strip_app_filler(spoken);
    if (cleaned.empty()) return {};
    const std::string key = squash(cleaned);
    if (key.empty()) return {};

    for (const auto& [alias, real] : app_aliases()) {
        if (squash(alias) == key && is_installed(real)) return real;
    }
    // An exact installed name wins over any partial match.
    for (const auto& app : installed_apps()) {
        if (squash(app) == key) return app;
    }
    // "photo booth" spoken as "photobooth", or only part of a long name.
    for (const auto& app : installed_apps()) {
        const std::string candidate = squash(app);
        if (candidate.find(key) != std::string::npos ||
            (key.size() >= 4 && key.find(candidate) != std::string::npos)) {
            return app;
        }
    }
    for (const auto& [alias, real] : app_aliases()) {
        const std::string candidate = squash(alias);
        if (key.find(candidate) != std::string::npos && candidate.size() >= 3 &&
            is_installed(real)) {
            return real;
        }
    }
    return {};
}

}  // namespace

bool open_app(const std::string& name) {
    if (name.empty()) return false;
    if (g_rehearsing) return !resolve_app(name).empty();
    if (const std::string resolved = resolve_app(name); !resolved.empty()) {
        if (run("open", {"-a", resolved}).ok()) return true;
    }
    // Fall back to whatever was said, in case the app is somewhere unusual.
    return run("open", {"-a", strip_app_filler(name)}).ok();
}

std::string resolve_app_name(const std::string& spoken) { return resolve_app(spoken); }

bool quit_app(const std::string& name) {
    if (name.empty()) return false;
    if (g_rehearsing) return true;
    osascript("tell application \"" + applescript_quote(name) + "\" to quit");
    return true;
}

bool open_url(const std::string& url) {
    if (url.rfind("http", 0) != 0 && url.rfind("file://", 0) != 0) return false;
    if (g_rehearsing) return true;
    return run("open", {url}).ok();
}

bool reveal_in_finder(const std::string& path) {
    if (path.empty()) return false;
    if (g_rehearsing) return true;
    return run("open", {"-R", path}).ok();
}

bool open_path(const std::string& path) {
    if (path.empty()) return false;
    if (g_rehearsing) return true;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    return run("open", {path}).ok();
}

std::string folder_for_name(const std::string& spoken) {
    const char* home_env = std::getenv("HOME");
    const std::string home = home_env ? home_env : "";
    std::string name = trim(spoken);
    if (name.empty()) return {};

    // A path said or pasted literally, rather than a folder's name.
    if (name.front() == '/' || name.rfind("~/", 0) == 0) {
        if (name.rfind("~/", 0) == 0) name = home + name.substr(1);
        std::error_code ec;
        return std::filesystem::is_directory(name, ec) ? name : std::string{};
    }

    name = strip_fillers(name, {"フォルダを開いて", "フォルダ", "を開いて", "開いて",
                                "the folder", "folder", "directory", "open "});
    if (name.empty() || home.empty()) return {};

    const std::string key = lowercase(name);
    static const std::vector<std::pair<const char*, const char*>> kFolders{
        {"downloads", "Downloads"},   {"ダウンロード", "Downloads"},
        {"documents", "Documents"},   {"ドキュメント", "Documents"},
        {"書類", "Documents"},         {"desktop", "Desktop"},
        {"デスクトップ", "Desktop"},   {"pictures", "Pictures"},
        {"ピクチャ", "Pictures"},      {"写真", "Pictures"},
        {"music", "Music"},           {"ミュージック", "Music"},
        {"音楽", "Music"},             {"movies", "Movies"},
        {"ムービー", "Movies"},        {"動画", "Movies"},
        {"applications", "Applications"}, {"アプリケーション", "Applications"},
        {"home", ""},                 {"ホーム", ""},
    };
    for (const auto& [alias, real] : kFolders) {
        if (key == alias || key == lowercase(real)) {
            const std::string path = real[0] == '\0' ? home : home + "/" + real;
            std::error_code ec;
            if (std::filesystem::is_directory(path, ec)) return path;
        }
    }

    // Not a standard folder: ask Spotlight for a directory by that name.
    const auto found = run("mdfind", {"-onlyin", home,
                                      "kMDItemContentType == 'public.folder' && "
                                      "kMDItemFSName == '" + name + "'"}, 15);
    if (found.ok()) {
        std::istringstream stream(found.out);
        std::string line;
        while (std::getline(stream, line)) {
            if (!trim(line).empty()) return trim(line);
        }
    }
    return {};
}

bool open_folder(const std::string& spoken) {
    const std::string path = folder_for_name(spoken);
    return path.empty() ? false : open_path(path);
}

std::string open_file(const std::string& spoken) {
    std::string name = trim(spoken);
    name = strip_fillers(name, {"というファイル", "ファイルを開いて", "ファイル", "を開いて",
                                "開いて", "the file", " file", "open "});
    if (name.empty()) return {};

    // An exact path first, then Spotlight by name.
    if (open_path(name)) return name;
    for (const auto& hit : find_files(name, 5)) {
        if (open_path(hit)) return hit;
    }
    return {};
}

namespace {

// AppleScript to read the first phone number off a Contacts card.
const char* kContactLookup =
    "tell application \"Contacts\"\n"
    "  set matches to (every person whose name contains \"{}\")\n"
    "  if (count of matches) is 0 then return \"\"\n"
    "  set card to item 1 of matches\n"
    "  if (count of phones of card) is 0 then return \"\"\n"
    "  return value of item 1 of phones of card\n"
    "end tell";

// True when the text is a phone number rather than somebody's name.
bool looks_like_number(const std::string& text) {
    int digits = 0;
    for (unsigned char c : text) {
        if (std::isdigit(c)) ++digits;
        else if (std::strchr("+-() .", c) == nullptr) return false;
    }
    return digits >= 3;
}

}  // namespace

std::string phone_for_contact(const std::string& name) {
    const std::string cleaned = trim(name);
    if (cleaned.empty()) return {};
    std::string script(kContactLookup);
    const auto at = script.find("{}");
    script.replace(at, 2, applescript_quote(cleaned));
    return trim(osascript(script));
}

bool place_call(const std::string& who, bool video) {
    std::string target = trim(who);
    target = strip_fillers(target, {"に電話をかけて", "に電話して", "へ電話", "電話をかけて",
                                    "電話して", "call ", "phone ", "facetime ", "please"});
    if (target.empty()) return false;

    // A name has to become a number first; FaceTime's URL scheme takes both,
    // but a name it cannot resolve fails silently rather than reporting back.
    std::string number = target;
    if (!looks_like_number(target)) {
        number = phone_for_contact(target);
        if (number.empty()) return false;
    }
    // Strip the spacing people read numbers with; the scheme wants it bare.
    number.erase(std::remove_if(number.begin(), number.end(),
                                [](unsigned char c) {
                                    return std::isspace(c) || c == '-' || c == '(' ||
                                           c == ')' || c == '.';
                                }),
                 number.end());
    if (number.empty()) return false;

    const std::string url = (video ? "facetime://" : "facetime-audio://") + number;
    return run("open", {url}).ok();
}

namespace {
// Percent-encoding, so a spoken query with spaces or Japanese survives the URL.
std::string url_encode(std::string_view text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            static const char* kHex = "0123456789ABCDEF";
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}
}  // namespace

bool search_in_browser(const std::string& query) {
    const std::string cleaned = trim(query);
    if (cleaned.empty()) return false;
    // Google for what the user actually sees. The scraped result list below
    // still comes from DuckDuckGo, whose HTML endpoint can be read without an
    // API key -- Google blocks that -- but nobody should be sent to a search
    // engine they did not choose.
    return open_url("https://www.google.com/search?q=" + url_encode(cleaned));
}

namespace {
// System Events key codes for the media keys.
bool media_key(int code) {
    if (g_rehearsing) return true;
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
    if (g_rehearsing) return true;
    osascript("display notification \"" + applescript_quote(text) + "\" with title \"" +
              applescript_quote(title) + "\"");
    return true;
}

bool notify(const std::string& title, const std::string& subtitle,
            const std::string& text) {
    if (g_rehearsing) return true;
    osascript("display notification \"" + applescript_quote(text) + "\" with title \"" +
              applescript_quote(title) + "\" subtitle \"" +
              applescript_quote(subtitle) + "\"");
    return true;
}

std::string reminder_announcement(const std::string& what) {
    const std::string subject = trim(what);
    if (subject.empty()) return "お知らせの時間です。";
    const auto ends_with = [&subject](std::string_view tail) {
        return subject.size() >= tail.size() &&
               subject.compare(subject.size() - tail.size(), tail.size(), tail) == 0;
    };

    // 「ゴミを出して」 is already an instruction; announcing it as a noun phrase
    // reads as a fragment.
    for (std::string_view tail : {"して", "しろ", "ください", "ね", "よ"}) {
        if (ends_with(tail)) return subject + "、という時間です。";
    }

    // A plain-form verb takes 時間 directly: 「薬を飲む時間です」. Only a noun
    // takes の, and getting this wrong produced 「薬を飲むの時間です」.
    for (std::string_view tail : {"う", "く", "ぐ", "す", "つ", "ぬ", "ぶ", "む", "る"}) {
        if (ends_with(tail)) return subject + "時間です。";
    }
    return subject + "の時間です。";
}

namespace {
std::filesystem::path cue_file() { return paths::data_file("notification_cue.m4a"); }
}  // namespace

std::string record_notification_cue(const std::string& name) {
    const std::string called = trim(name);
    // In her own language and her own voice. Written in English first, which
    // meant the one thing she said unprompted was the one thing not in Japanese
    // -- and `say` reached for an English system voice to do it, so a Japanese
    // assistant announced herself as somebody else entirely.
    const std::string line =
        called.empty() ? "ねえ、お知らせがあります。"
                       : "ねえ" + called + "さん、お知らせがあります。";

    const auto aiff = std::filesystem::temp_directory_path() / "mimi_cue.aiff";
    // -v Kyoko: the same voice she speaks with, rather than whatever the system
    // default happens to be.
    if (!run("say", {"-v", "Kyoko", "-o", aiff.string(), line}, 20).ok()) {
        log::warn(kTag, "could not record the notification cue");
        return {};
    }
    const auto out = cue_file();
    std::error_code ec;
    std::filesystem::remove(out, ec);
    // AAC in an m4a container: the only compressed format afconvert can write.
    const bool converted =
        run("afconvert", {"-f", "m4af", "-d", "aac", aiff.string(), out.string()}, 20).ok();
    std::filesystem::remove(aiff, ec);
    if (!converted) {
        log::warn(kTag, "could not encode the notification cue");
        return {};
    }
    log::info(kTag, "recorded the notification cue for '{}'", called);
    return out.string();
}

std::string notification_cue_path() {
    std::error_code ec;
    const auto path = cue_file();
    return std::filesystem::exists(path, ec) ? path.string() : std::string{};
}

void play_notification_cue() {
    const std::string path = notification_cue_path();
    if (path.empty()) return;
    // Detached: a reminder must not wait on its own doorbell.
    std::thread([path] { run("afplay", {path}, 15); }).detach();
}

bool lock_screen() {
    if (g_rehearsing) return true;
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

namespace {

// Pending reminders, as one JSON array on disk. Small and rewritten whole:
// there are never many, and a partial write of a list this size is not worth
// defending against with anything cleverer.
std::filesystem::path reminders_file() { return paths::data_file("reminders.json"); }

nlohmann::json read_reminders() {
    std::ifstream in(reminders_file());
    if (!in) return nlohmann::json::array();
    try {
        auto parsed = nlohmann::json::parse(in);
        return parsed.is_array() ? parsed : nlohmann::json::array();
    } catch (const std::exception&) {
        return nlohmann::json::array();
    }
}

void write_reminders(const nlohmann::json& list) {
    std::ofstream out(reminders_file(), std::ios::trunc);
    if (out) out << list.dump(2) << "\n";
}

std::int64_t epoch_now() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

// Drops a reminder once it has fired, matched on when and what.
void forget_reminder(std::int64_t due, const std::string& text) {
    auto list = read_reminders();
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& item : list) {
        if (item.value("due", std::int64_t{0}) == due && item.value("text", "") == text) {
            continue;
        }
        kept.push_back(item);
    }
    write_reminders(kept);
}

// The timer itself. Shared by a fresh reminder and a restored one.
// True while the reminder is still on disk. A sleeping thread cannot be woken
// to be told it was called off, so the check happens where it matters: cancel
// removes the record, and the timer that eventually wakes finds it gone and
// says nothing.
bool still_wanted(std::int64_t due, const std::string& text) {
    for (const auto& item : read_reminders()) {
        if (item.value("due", std::int64_t{0}) == due && item.value("text", "") == text) {
            return true;
        }
    }
    return false;
}

void arm(std::chrono::seconds delay, std::int64_t due, std::string text,
         std::function<void(const std::string&)> on_fire) {
    std::thread([delay, due, text = std::move(text), on_fire = std::move(on_fire)] {
        if (delay.count() > 0) std::this_thread::sleep_for(delay);
        if (!still_wanted(due, text)) return;  // cancelled while it slept
        if (on_fire) on_fire(text);
        forget_reminder(due, text);
    }).detach();
}

}  // namespace

void schedule(std::chrono::seconds delay, std::string text,
              std::function<void(const std::string&)> on_fire) {
    if (g_rehearsing) return;  // no timer, no file, nothing to go off later
    const std::int64_t due = epoch_now() + delay.count();

    auto list = read_reminders();
    list.push_back({{"due", due}, {"text", text}});
    write_reminders(list);

    arm(delay, due, std::move(text), std::move(on_fire));
}

std::vector<Pending> pending_reminders() {
    std::vector<Pending> out;
    const std::int64_t now = epoch_now();
    for (const auto& item : read_reminders()) {
        Pending pending;
        pending.due = item.value("due", std::int64_t{0});
        pending.what = item.value("text", std::string{});
        if (pending.due == 0 || pending.what.empty()) continue;

        const std::int64_t left = pending.due - now;
        if (left <= 0) {
            pending.when = "まもなく";
        } else if (left < 60) {
            pending.when = std::to_string(left) + "秒後";
        } else if (left < 3600) {
            pending.when = std::to_string(left / 60) + "分後";
        } else {
            pending.when = std::to_string(left / 3600) + "時間後";
        }
        out.push_back(std::move(pending));
    }
    std::sort(out.begin(), out.end(),
              [](const Pending& a, const Pending& b) { return a.due < b.due; });
    return out;
}

std::string cancel_reminder(const std::string& what) {
    const auto list = read_reminders();
    if (list.empty()) return {};

    const std::string wanted = lowercase(trim(what));
    nlohmann::json kept = nlohmann::json::array();
    std::string cancelled;

    // Soonest first, so "cancel that reminder" with no subject drops the next
    // one rather than an arbitrary entry.
    auto pending = pending_reminders();
    std::int64_t target_due = 0;
    std::string target_text;
    for (const auto& item : pending) {
        if (wanted.empty() ||
            lowercase(item.what).find(wanted) != std::string::npos) {
            target_due = item.due;
            target_text = item.what;
            break;
        }
    }
    if (target_text.empty()) return {};

    for (const auto& item : list) {
        if (item.value("due", std::int64_t{0}) == target_due &&
            item.value("text", "") == target_text && cancelled.empty()) {
            cancelled = target_text;
            continue;
        }
        kept.push_back(item);
    }
    if (!cancelled.empty()) write_reminders(kept);
    return cancelled;
}

int restore_reminders(std::function<void(const std::string&)> on_fire) {
    const auto list = read_reminders();
    const std::int64_t now = epoch_now();
    int restored = 0;
    for (const auto& item : list) {
        const auto due = item.value("due", std::int64_t{0});
        const auto text = item.value("text", std::string{});
        if (due == 0 || text.empty()) continue;
        // Past due while the app was closed: fire now rather than drop it.
        const auto remaining = std::chrono::seconds{std::max<std::int64_t>(0, due - now)};
        arm(remaining, due, text, on_fire);
        ++restored;
    }
    if (restored > 0) log::info(kTag, "restored {} reminder(s)", restored);
    return restored;
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

    // Only ASCII can be guessed at as a domain. "スポティファイ" is a spoken app
    // name, not a hostname, and turning it into https://スポティファイ.com sent
    // the browser somewhere that does not exist -- which is what "open Spotify"
    // did on a Mac without Spotify. An unknown non-ASCII name means "not a
    // site", so the caller can say it could not find it instead.
    const bool ascii = std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return c < 0x80;
    });
    if (!ascii) return {};
    return "https://" + name + ".com";
}

}  // namespace mimi::brain::tools
