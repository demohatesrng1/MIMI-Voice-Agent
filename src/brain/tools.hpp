#pragma once

#include <chrono>
#include <cstdint>
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

// --- rehearsal --------------------------------------------------------------

// Makes every action below a no-op that still reports success.
//
// For tests, and for anything that needs to know *what* she would do without
// her doing it. The routing tests are the reason this exists: checking that
// "ディスコードを開いて" resolves to launch_app meant actually launching
// Discord, so running the suite threw windows open across the machine.
//
// Reading the machine is unaffected -- the time and the battery cost nothing.
void set_rehearsing(bool rehearsing);
bool rehearsing();

// --- changing the machine ---------------------------------------------------

void set_clipboard(const std::string& text);
bool set_volume(int percent);       // clamped to 0..100
bool nudge_volume(int delta);
bool set_muted(bool muted);
bool set_brightness(int percent);

bool open_app(const std::string& name);
// A spoken app name -> the installed .app name, or empty when nothing matches.
// Aliases ("chrome", "vs code", "ライン") plus a scan of /Applications.
std::string resolve_app_name(const std::string& spoken);
bool quit_app(const std::string& name);
bool open_url(const std::string& url);
bool reveal_in_finder(const std::string& path);
// Opens a file or folder in whatever app owns it. Unlike reveal_in_finder this
// actually opens the thing rather than selecting it in Finder.
bool open_path(const std::string& path);
// A spoken folder name -> a real path. Handles the standard home folders in
// both languages ("ダウンロード", "downloads") plus literal and ~ paths.
std::string folder_for_name(const std::string& spoken);
// Finds a folder by name and opens it. Falls back to a Spotlight search.
bool open_folder(const std::string& spoken);
// Finds a file by name and opens it. Returns the path opened, or empty.
std::string open_file(const std::string& spoken);

// --- calls ------------------------------------------------------------------

// Places a call. `who` is a phone number or a name from Contacts. FaceTime
// audio by default; `video` switches to a FaceTime video call.
// Note this hands off to FaceTime and cannot confirm the call connected.
bool place_call(const std::string& who, bool video = false);
// Looks a name up in Contacts and returns the first phone number, or empty.
std::string phone_for_contact(const std::string& name);

// Media keys, so "next track" works in whatever app is playing.
bool media_playpause();
bool media_next();
bool media_previous();

std::string screenshot();               // full screen -> path on the Desktop
std::string screenshot_interactive();    // user picks a region

bool notify(const std::string& title, const std::string& text);
// A notification with a subtitle, which is where macOS puts the thing the
// alert is *about*. Without it a reminder banner reads as a bare fragment with
// no indication of why it appeared.
bool notify(const std::string& title, const std::string& subtitle,
            const std::string& text);

// What she should say and show when a reminder comes due.
//
// The stored text is only the fragment the user said -- "休憩", "薬" -- and
// announcing that alone tells them a word, not that a reminder fired or what
// they asked for. This turns it back into a sentence.
std::string reminder_announcement(const std::string& what);

// The spoken cue a reminder plays before it is read out.
//
// Rendered once, when the name is set, rather than synthesised on every
// reminder: a fixed greeting that never changes should not cost a TTS round
// trip each time, and pre-rendering means the sound is identical every time it
// fires. Written as m4a/AAC -- macOS ships no MP3 *encoder*, so asking for .mp3
// produces nothing at all; m4a is the same idea in the format the system can
// actually write.
//
// Returns the path, or empty if it could not be made.
std::string record_notification_cue(const std::string& name);

// The cue's path if it has been recorded, else empty.
std::string notification_cue_path();

// Plays the cue and returns immediately.
void play_notification_cue();
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
// Opens the query in the default browser, rather than only reading results.
bool search_in_browser(const std::string& query);
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

// Fires `on_fire` after the delay, and writes the reminder to disk first.
//
// A detached thread alone loses everything the moment the process ends: someone
// who says "remind me in 20 minutes" and then quits Mimi -- or is quit by a
// crash, or a restart -- gets nothing, and never finds out why. The pending set
// is persisted so it can be restored.
void schedule(std::chrono::seconds delay, std::string text,
              std::function<void(const std::string&)> on_fire);

// A reminder waiting to go off.
struct Pending {
    std::int64_t due = 0;  // unix seconds
    std::string what;
    std::string when;      // "in 12 minutes", for reading back
};

// Everything still waiting, soonest first.
//
// Setting a reminder and then having no way to see it is the gap that makes
// the feature untrustworthy: there is no way to confirm it registered, and no
// way to call one off.
std::vector<Pending> pending_reminders();

// Cancels the reminder whose text best matches, or the soonest one when `what`
// is empty. Returns what was cancelled, or empty if nothing matched.
std::string cancel_reminder(const std::string& what = {});

// Re-arms every reminder saved by schedule() that has not fired yet.
//
// Anything already past due when this runs fires immediately: a reminder that
// came due while the app was closed is late, but delivering it late is much
// better than dropping it. Call once at startup.
int restore_reminders(std::function<void(const std::string&)> on_fire);

// A site name spoken aloud -> a URL. "youtube" -> https://youtube.com
std::string url_for_site(const std::string& spoken);

}  // namespace tools
}  // namespace mimi::brain
