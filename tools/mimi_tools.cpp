// mimi_tools -- exercise each laptop control on its own.
//
//   mimi_tools              read-only probes (safe, changes nothing)
//   mimi_tools --url NAME   what URL a spoken site name resolves to
//   mimi_tools --app NAME   which installed app a spoken name resolves to
//   mimi_tools --search Q   DuckDuckGo results
//   mimi_tools --remind S   what the reminder parser sees

#include "brain/tools.hpp"
#include "core/log.hpp"

#include <cstdio>
#include <chrono>
#include <string>
#include <thread>

using namespace mimi::brain;

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    if (argc > 2 && std::string(argv[1]) == "--url") {
        std::printf("%s -> %s\n", argv[2], tools::url_for_site(argv[2]).c_str());
        return 0;
    }
    // Resolution only: this never launches anything, so it is safe to sweep.
    if (argc > 2 && std::string(argv[1]) == "--app") {
        for (int i = 2; i < argc; ++i) {
            const std::string app = tools::resolve_app_name(argv[i]);
            std::printf("  %-28s -> %s\n", argv[i], app.empty() ? "(no match)" : app.c_str());
        }
        return 0;
    }
    // Resolution only, nothing is opened.
    if (argc > 2 && std::string(argv[1]) == "--folder") {
        for (int i = 2; i < argc; ++i) {
            const std::string path = tools::folder_for_name(argv[i]);
            std::printf("  %-30s -> %s\n", argv[i], path.empty() ? "(no match)" : path.c_str());
        }
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--contact") {
        for (int i = 2; i < argc; ++i) {
            const std::string number = tools::phone_for_contact(argv[i]);
            std::printf("  %-30s -> %s\n", argv[i], number.empty() ? "(not found)" : number.c_str());
        }
        return 0;
    }
    // Re-arms what is on disk and reports what fired, so persistence across a
    // restart can be checked without restarting the app.
    if (argc > 1 && std::string(argv[1]) == "--reminders") {
        const int restored = tools::restore_reminders([](const std::string& text) {
            std::printf("  fired: %s\n", text.c_str());
        });
        std::printf("  restored %d reminder(s)\n", restored);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--search") {
        for (const auto& [title, url] : tools::web_search(argv[2], 5)) {
            std::printf("  %-52s %s\n", title.substr(0, 52).c_str(), url.c_str());
        }
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--remind") {
        const auto reminder = tools::parse_reminder(argv[2]);
        if (!reminder) { std::puts("  no reminder found"); return 1; }
        std::printf("  in %llds -> \"%s\"\n",
                    static_cast<long long>(reminder->delay.count()), reminder->what.c_str());
        return 0;
    }

    std::puts("\n--- reading the machine (nothing is changed) ---\n");
    std::printf("  time (ja)  : %s\n", tools::what_time_ja().c_str());
    std::printf("  time (en)  : %s\n", tools::what_time_en().c_str());

    const auto batt = tools::battery();
    std::printf("  battery    : %d%% %s %s\n", batt.percent, batt.state.c_str(),
                batt.remaining.empty() ? "" : ("(" + batt.remaining + " left)").c_str());

    const auto info = tools::system_info();
    std::printf("  host       : %s (macOS %s)\n", info.host.c_str(), info.macos.c_str());
    std::printf("  memory     : %.1f / %.1f GB\n", info.memory_used_gb, info.memory_total_gb);
    std::printf("  disk       : %.0f GB free of %.0f GB\n", info.disk_free_gb, info.disk_total_gb);
    std::printf("  uptime     : %s\n", info.uptime.c_str());
    std::printf("  volume     : %d%%\n", tools::volume());

    const auto tab = tools::current_tab();
    std::printf("  browser    : %s\n", tab.valid()
        ? (tab.app + ": " + tab.title.substr(0, 48)).c_str() : "(no browser tab)");

    const auto apps = tools::running_apps();
    std::printf("  apps (%zu)  : ", apps.size());
    for (std::size_t i = 0; i < apps.size() && i < 6; ++i) std::printf("%s ", apps[i].c_str());
    std::printf("\n");

    const auto clip = tools::clipboard();
    std::printf("  clipboard  : %zu chars\n", clip.size());

    std::puts("\n--- site name resolution ---\n");
    for (const char* name : {"youtube", "ユーチューブ", "github", "amazon", "example.org"}) {
        std::printf("  %-16s -> %s\n", name, tools::url_for_site(name).c_str());
    }

    std::puts("\n--- reminder parsing ---\n");
    for (const char* phrase : {"20分後に休憩して", "5秒後にお茶",
                               "remind me in 10 minutes to stretch", "こんにちは"}) {
        const auto r = tools::parse_reminder(phrase);
        std::printf("  %-36s -> %s\n", phrase,
                    r ? (std::to_string(r->delay.count()) + "s: " + r->what).c_str()
                      : "(not a reminder)");
    }
    std::puts("");
    return 0;
}
