#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mimi::brain {

// What Mimi can actually do to the machine.
//
// Deliberately a fixed set of named actions rather than "run this command".
// The arguments come from a model interpreting speech, and a general shell
// escape hatch would mean a misheard sentence could do anything. Everything
// here is individually reversible or read-only: nothing deletes files, changes
// system settings permanently, or touches credentials.
namespace tools {

struct BrowserTab {
    std::string url;
    std::string title;
    std::string app;
    bool valid() const noexcept { return !url.empty(); }
};

struct BatteryStatus {
    int percent = -1;
    std::string state;      // "on battery" | "charging" | "fully charged"
    std::string remaining;  // "2:14" when macOS offers an estimate
    bool valid() const noexcept { return percent >= 0; }
};

struct SystemInfo {
    std::string host;
    std::string macos;
    double memory_used_gb = 0;
    double memory_total_gb = 0;
    double disk_free_gb = 0;
    double disk_total_gb = 0;
    double cpu_percent = 0;
    std::string uptime;
};

// --- reading the machine ----------------------------------------------------

std::string what_time_ja();
std::string what_time_en();
BatteryStatus battery();
SystemInfo system_info();
std::string clipboard();
BrowserTab current_tab();
std::vector<std::string> running_apps();
int volume();          // 0..100, -1 if unknown
int brightness_percent();  // -1 when it cannot be read

// --- changing the machine ---------------------------------------------------

void set_clipboard(const std::string& text);
bool set_volume(int percent);       // clamped to 0..100
bool nudge_volume(int delta);
bool set_muted(bool muted);
bool set_brightness(int percent);

bool open_app(const std::string& name);
bool quit_app(const std::string& name);
bool open_url(const std::string& url);
bool reveal_in_finder(const std::string& path);

// Media keys, so "next track" works in whatever app is playing.
bool media_playpause();
bool media_next();
bool media_previous();

std::string screenshot();               // full screen -> path on the Desktop
std::string screenshot_interactive();    // user picks a region

bool notify(const std::string& title, const std::string& text);
bool lock_screen();
bool sleep_display();

// Not offered on purpose: emptying the trash, deleting files, shutting down and
// anything touching the keychain. All are irreversible, and the caller is a
// speech transcript.

// --- finding things ---------------------------------------------------------

std::vector<std::string> find_files(const std::string& name, int limit = 5);
// Scrapes DuckDuckGo's HTML endpoint. Returns (title, url).
std::vector<std::pair<std::string, std::string>> web_search(const std::string& query,
                                                            int limit = 8);
// Fetches a page and strips it to readable text.
std::string fetch_page_text(const std::string& url, std::string* title_out = nullptr);

// --- timers -----------------------------------------------------------------

// Parses "20分後に休憩", "remind me in 5 minutes to stretch". Returns the delay
// and what to say.
struct Reminder {
    std::chrono::seconds delay{0};
    std::string what;
};
std::optional<Reminder> parse_reminder(const std::string& text);

// Fires `on_fire` on a detached thread after the delay.
void schedule(std::chrono::seconds delay, std::string text,
              std::function<void(const std::string&)> on_fire);

// A site name spoken aloud -> a URL. "youtube" -> https://youtube.com
std::string url_for_site(const std::string& spoken);

}  // namespace tools
}  // namespace mimi::brain
