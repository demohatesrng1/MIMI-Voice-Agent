// mimi_ax -- exercise the Accessibility layer without the app.
//
// Everything here drives whatever application is frontmost, so run it, switch
// to the app you want to poke at, and give yourself a few seconds.
//
//   mimi_ax --check              is permission granted? which app is in front?
//   mimi_ax --ask                trigger the permission prompt
//   mimi_ax --controls [N]       list the actionable controls in the front window
//   mimi_ax --press LABEL        press the control with that label
//   mimi_ax --focused            read the focused field
//   mimi_ax --type TEXT          type into the focused field
//   mimi_ax --key KEY [MODS...]  a shortcut, e.g. --key s command
//   mimi_ax --menu A B [C]       pick a menu item, e.g. --menu File Export
//   mimi_ax --delay N            seconds to wait first, so you can switch apps

#include "brain/accessibility.hpp"
#include "core/log.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace mimi::brain;

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::puts("see the header of tools/mimi_ax.cpp for usage");
        return 1;
    }

    // --delay first, so the caller can bring another app forward.
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--delay") {
            const int seconds = std::stoi(args[i + 1]);
            std::printf("waiting %d s -- bring the target app forward\n", seconds);
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            args.erase(args.begin() + i, args.begin() + i + 2);
            break;
        }
    }

    const std::string command = args.empty() ? "--check" : args[0];

    if (command == "--check") {
        std::printf("  permission : %s\n", ax::has_permission() ? "granted" : "NOT granted");
        std::printf("  frontmost  : %s\n", ax::frontmost_app().c_str());
        std::printf("  window     : %s\n", ax::focused_window_title().c_str());
        return ax::has_permission() ? 0 : 1;
    }
    if (command == "--ask") {
        const bool now = ax::request_permission();
        std::printf("  prompt shown; trusted right now: %s\n", now ? "yes" : "no");
        std::puts("  macOS grants this on a later launch, not from this call.");
        return 0;
    }
    if (command == "--controls") {
        const int limit = args.size() > 1 ? std::stoi(args[1]) : 60;
        const auto found = ax::controls(limit);
        std::printf("  %zu actionable controls in %s\n", found.size(),
                    ax::frontmost_app().c_str());
        for (const auto& element : found) {
            std::printf("    %-22s %-34s value='%s'\n", element.role.c_str(),
                        element.title.substr(0, 34).c_str(),
                        element.value.substr(0, 30).c_str());
        }
        return found.empty() ? 1 : 0;
    }
    if (command == "--press" && args.size() > 1) {
        const bool ok = ax::press(args[1]);
        std::printf("  press '%s' -> %s\n", args[1].c_str(), ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }
    if (command == "--focused") {
        const std::string text = ax::focused_text();
        std::printf("  focused field: '%s'\n", text.c_str());
        return text.empty() ? 1 : 0;
    }
    if (command == "--type" && args.size() > 1) {
        const bool ok = ax::type_text(args[1]);
        std::printf("  typed -> %s\n", ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }
    if (command == "--key" && args.size() > 1) {
        const std::vector<std::string> modifiers(args.begin() + 2, args.end());
        const bool ok = ax::key_stroke(args[1], modifiers);
        std::printf("  key '%s' -> %s\n", args[1].c_str(), ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }
    if (command == "--menu" && args.size() > 1) {
        const std::vector<std::string> path(args.begin() + 1, args.end());
        const bool ok = ax::menu_click(path);
        std::printf("  menu -> %s\n", ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }

    std::puts("unknown command; see the header of tools/mimi_ax.cpp");
    return 1;
}
