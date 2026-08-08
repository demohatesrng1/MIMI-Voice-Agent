// mimi_test -- assertions over the deterministic half of the router.
//
// Everything checked here runs without the language model: these are the rules
// that match on phrasing alone, which is most of what Mimi does and all of what
// has to keep working when the model underneath her changes. Each case is a bug
// that was found by hand and would otherwise be found by hand again.
//
//   mimi_test          run them
//   mimi_test -v       print every case, not just failures
//
// Deliberately not a framework. One file, no dependencies, exits non-zero on
// failure so it can gate a commit.

#include "brain/account.hpp"
#include "brain/notes.hpp"
#include "core/paths.hpp"
#include "brain/router.hpp"
#include "brain/tools.hpp"
#include "core/log.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace mimi::brain;

namespace {

int checks = 0;
int failures = 0;
bool verbose = false;

void check(bool ok, const std::string& what, const std::string& detail = {}) {
    ++checks;
    if (ok) {
        if (verbose) std::printf("  ok    %s\n", what.c_str());
        return;
    }
    ++failures;
    std::printf("  FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  -- ",
                detail.c_str());
}

void equal(const std::string& got, const std::string& want, const std::string& what) {
    check(got == want, what, got == want ? "" : "got '" + got + "', want '" + want + "'");
}

// --- app names --------------------------------------------------------------

void test_app_resolution() {
    std::puts("\napp names");

    // Aliases only count when the app is really installed. Returning "Google
    // Chrome" on a Mac without it made open_app fail, which sent the caller to
    // its website fallback and opened a fabricated https://クローム.com.
    for (const char* spoken : {"discord", "ディスコード", "line", "ライン"}) {
        const std::string app = tools::resolve_app_name(spoken);
        check(!app.empty(), std::string("resolves ") + spoken, "got nothing");
    }
    check(tools::resolve_app_name("definitelynotaninstalledapp").empty(),
          "unknown app resolves to nothing");

    // Filler words around the name must not defeat the match.
    equal(tools::resolve_app_name("ディスコードを起動して"), "Discord",
          "strips Japanese filler");
    equal(tools::resolve_app_name("open discord"), "Discord", "strips English filler");
    equal(tools::resolve_app_name("OPEN Discord"), "Discord", "ignores ASCII case");
}

// --- sites ------------------------------------------------------------------

void test_site_resolution() {
    std::puts("\nsite names");

    equal(tools::url_for_site("youtube"), "https://youtube.com", "known site");
    equal(tools::url_for_site("github.com"), "https://github.com", "explicit domain");
    // A spoken Japanese app name is not a hostname. Guessing one produced
    // https://スポティファイ.com, which is what "open Spotify" used to do.
    check(tools::url_for_site("スポティファイ").empty(),
          "does not invent a domain from Japanese");
}

// --- folders ----------------------------------------------------------------

void test_folders() {
    std::puts("\nfolders");
    for (const char* spoken : {"ダウンロード", "downloads", "デスクトップ", "書類"}) {
        check(!tools::folder_for_name(spoken).empty(), std::string("finds ") + spoken);
    }
    // The particle left behind when フォルダ is stripped used to defeat this.
    check(!tools::folder_for_name("デスクトップ").empty(), "bare folder name");
}

// --- routing ----------------------------------------------------------------

// The rules layer needs an Ollama to construct a Router, but none of the cases
// below should reach the model. Any that does is itself the bug.
void test_routing(Router& router) {
    std::puts("\nrouting");

    // "会議は何時からだっけ" is only a question for the notes if a note answers
    // it. Without this the case passed on whatever an earlier run happened to
    // leave behind, and failed the moment the suite ran against a clean
    // directory.
    Notes notes;
    const Note fixture = notes.add("会議は午後3時から、資料は木曜まで");

    struct Case {
        const char* utterance;
        const char* action;
        const char* why;
    };

    const std::vector<Case> cases{
        // Single commands take the free deterministic path.
        {"今何時", "time", "bare clock question"},
        {"バッテリーはどのくらい", "battery", "battery"},
        {"音量を上げて", "volume_up", "volume up"},
        {"ミュートして", "mute", "mute"},

        // 何時 with a topic in front is a question about that topic, not the
        // wall clock: "会議は何時から" used to be answered with the time.
        {"会議は何時からだっけ", "ask_notes", "topic before 何時 is not the clock"},

        // メモ is three different requests depending on the verb. Any utterance
        // mentioning it used to become a new note.
        {"テストとメモして", "take_note", "explicit verb writes a note"},
        {"メモを読んで", "read_notes", "reads them back"},
        {"メモを要約して", "ask_notes", "summarises through the model"},
        // メモ帳 is a notepad, and hers is the one being asked for.
        {"メモ帳を開いて", "open_notes", "メモ帳 opens her own notes"},

        // Apps, sites and files share one phrasing.
        {"ディスコードを開いて", "launch_app", "installed app"},
        {"open youtube", "open_site", "site, not an app"},
        {"ダウンロードフォルダを開いて", "open_folder", "folder before app"},

        // Questions must not be captured by the search rule.
        {"富士山について教えて", "chat", "a question is answered, not searched"},
    };

    for (const Case& c : cases) {
        const Reply reply = router.route(c.utterance);
        equal(reply.action, c.action, c.why);
    }

    // Two commands in one breath, which the rules layer cannot split -- this
    // one really does reach the model, unlike everything above.
    //
    // It is checked against the *main* model on purpose. Decomposing 「Aして
    // Bして」 into two steps is the one thing the small classifier measurably
    // cannot do: llama3.2:3b collapses this to a single media_playpause, where
    // gemma3n:e4b returns both steps in order. Asserting it through the default
    // path would only encode whichever classifier happens to be configured.
    {
        Ollama::Config accurate;
        accurate.fast_model.clear();
        Ollama big(accurate);
        Router precise(big);
        const Reply reply = precise.route("音量を上げて次の曲にして");
        equal(reply.action, "volume_up+media_next", "two commands run in order");
    }

    if (fixture.valid()) notes.remove(fixture.id);
}

// --- notes ------------------------------------------------------------------

void test_notes() {
    std::puts("\nnotes");
    Notes notes;

    const Note written = notes.add("会議は午後3時から、資料は木曜まで");
    check(written.valid(), "writes a note");
    check(!notes.all(1).empty(), "reads it back");

    // Substring search cannot answer a paraphrase; bigram relevance is what
    // lets "会議は何時からだっけ" find the note that answers it.
    check(!notes.relevant("会議は何時からだっけ", 3).empty(),
          "finds a note by meaning, not substring");
    check(notes.relevant("まったく無関係な話題です", 3).empty(),
          "does not match an unrelated question");

    // Deleting by voice needs the newest note when nothing is named, since
    // that is the only one a speaker can refer to from memory.
    const Note second = notes.add("消される予定のメモ");
    check(notes.all(1).front().id == second.id, "newest note is first");
    check(notes.remove(second.id), "removes a note");

    if (!written.id.empty()) notes.remove(written.id);
}

// --- reminders --------------------------------------------------------------

void test_reminders() {
    std::puts("\nreminders");
    // A stored reminder is only the fragment that was said. Announcing that
    // bare word tells the user nothing about why their Mac just spoke.
    equal(tools::reminder_announcement("休憩"), "休憩の時間です。", "a noun takes の");
    equal(tools::reminder_announcement("薬を飲む"), "薬を飲む時間です。",
          "a plain-form verb does not");
    equal(tools::reminder_announcement("ゴミを出して"), "ゴミを出して、という時間です。",
          "an instruction is announced as one");
    check(!tools::reminder_announcement("").empty(), "an empty reminder still says something");

    // Setting one and never being able to see it is what made the feature
    // untrustworthy; listing and cancelling are the other half of it.
    tools::set_rehearsing(false);  // these need to actually touch the file
    while (!tools::cancel_reminder().empty()) {}  // start clean

    tools::schedule(std::chrono::seconds{600}, "テスト予定", {});
    auto pending = tools::pending_reminders();
    check(pending.size() == 1, "a scheduled reminder shows as pending");
    if (!pending.empty()) {
        equal(pending.front().what, "テスト予定", "keeps what it is about");
        check(!pending.front().when.empty(), "says how long is left");
    }

    equal(tools::cancel_reminder("テスト"), "テスト予定", "cancels by partial match");
    check(tools::pending_reminders().empty(), "cancelling removes it");
    check(tools::cancel_reminder("anything").empty(), "cancelling nothing is safe");

    // Moving an existing reminder to a new time, rather than stacking a second.
    check(tools::parse_duration("10分後にして").value_or(std::chrono::seconds{0}) ==
              std::chrono::seconds{600}, "reads a bare duration from Japanese");
    check(tools::parse_duration("snooze 5 minutes").value_or(std::chrono::seconds{0}) ==
              std::chrono::seconds{300}, "reads a bare duration from English");
    check(!tools::parse_duration("cancel it").has_value(), "no number, no duration");

    tools::schedule(std::chrono::seconds{600}, "会議", {});
    const auto moved = tools::reschedule_reminder("会議", std::chrono::seconds{1800}, {});
    equal(moved.what, "会議", "reschedule finds it by name");
    check(!moved.when.empty(), "reschedule reports the new time");
    {
        const auto still = tools::pending_reminders();
        check(still.size() == 1, "reschedule moves, it does not duplicate");
        if (!still.empty()) check(still.front().due > time(nullptr) + 60,
                                  "the reminder now sits at the later time");
    }
    check(tools::reschedule_reminder("会議", std::chrono::seconds{60}, {}).what == "会議",
          "reschedule again is fine");
    check(tools::reschedule_reminder("nothing here", std::chrono::seconds{60}, {}).what.empty(),
          "rescheduling a miss is safe");
    while (!tools::cancel_reminder().empty()) {}  // leave the file clean
    tools::set_rehearsing(true);
}

// --- accounts ---------------------------------------------------------------

void test_accounts() {
    std::puts("\naccounts");
    Accounts accounts;
    accounts.forget();  // start from nothing

    check(!accounts.exists(), "no account to begin with");
    // A missing account must never read as a successful login.
    check(!accounts.verify("someone", "anything"),
          "cannot sign in when no account exists");

    const bool made =
        accounts.sign_up("Demo", "correct horse battery", "Demo Person", "Demo");
    check(made, "signs up");
    check(accounts.exists(), "account now exists");
    check(!accounts.sign_up("other", "x", "O", "O"),
          "will not overwrite an existing account");

    check(accounts.verify("Demo", "correct horse battery"),
          "correct password verifies");
    check(accounts.verify("DEMO", "correct horse battery"),
          "the username is case-insensitive");
    check(!accounts.verify("Demo", "wrong password"),
          "wrong password is rejected");
    check(!accounts.verify("nobody", "correct horse battery"),
          "wrong username is rejected");

    // Changing the password must invalidate the old one, not merely add a new.
    check(accounts.set_password("a different secret"), "changes the password");
    check(accounts.verify("Demo", "a different secret"), "the new password works");
    check(!accounts.verify("Demo", "correct horse battery"),
          "the old password stops working");

    const Account loaded = accounts.load();
    equal(loaded.username, "Demo", "keeps the username");
    equal(loaded.preferred, "Demo", "keeps what to call them");

    // The password itself must not be recoverable from the file.
    std::ifstream file(mimi::paths::data_file("account.json"));
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    check(contents.find("correct horse battery") == std::string::npos,
          "the password is not written to disk");
    check(contents.find("digest") != std::string::npos, "a digest is stored instead");

    accounts.forget();
    check(!accounts.exists(), "forgets on request");
}

}  // namespace

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    // Run against a throwaway data directory, before anything can call
    // paths::data_dir() -- it caches on first use, so this has to be the first
    // thing main does.
    //
    // Not hygiene: the account tests begin by calling forget(), and the suite
    // ran against ~/Library/Application Support/Mimi. Running the tests deleted
    // the real account and locked the user out of the app. Anything that writes
    // belongs in here, not in the account, notes and journal someone is using.
    const std::string sandbox =
        (std::filesystem::temp_directory_path() / "mimi-test-data").string();
    std::filesystem::remove_all(sandbox);
    ::setenv("MIMI_DATA_DIR", sandbox.c_str(), 1);

    // Nothing in this suite may touch the machine it runs on. Without this the
    // routing cases genuinely launched Discord, opened youtube.com and threw a
    // Finder window up, every single run.
    tools::set_rehearsing(true);
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-v") verbose = true;
    }

    test_app_resolution();
    test_site_resolution();
    test_folders();
    test_notes();
    test_accounts();
    test_reminders();

    Ollama ollama({});
    if (ollama.ensure_running()) {
        Router router(ollama);
        test_routing(router);
    } else {
        std::puts("\nrouting\n  SKIPPED -- Ollama is not running");
    }

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
