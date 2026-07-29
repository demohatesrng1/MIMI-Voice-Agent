#include "core/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace mimi::log {
namespace {

std::atomic<Level> g_level{Level::Info};
std::mutex g_write_mutex;

constexpr std::string_view name_of(Level lv) noexcept {
    switch (lv) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        case Level::Off:   return "OFF  ";
    }
    return "?????";
}

// Dim the routine levels, colour the ones that need attention. Only when the
// sink is a terminal -- redirected logs stay clean.
constexpr std::string_view colour_of(Level lv) noexcept {
    switch (lv) {
        case Level::Trace: return "\033[2;37m";
        case Level::Debug: return "\033[36m";
        case Level::Info:  return "\033[32m";
        case Level::Warn:  return "\033[33m";
        case Level::Error: return "\033[1;31m";
        case Level::Off:   return "";
    }
    return "";
}

bool stderr_is_tty() noexcept {
    static const bool tty = ::isatty(fileno(stderr)) != 0;
    return tty;
}

}  // namespace

void set_level(Level lv) noexcept { g_level.store(lv, std::memory_order_relaxed); }

Level level() noexcept { return g_level.load(std::memory_order_relaxed); }

void configure_from_env() noexcept {
    const char* env = std::getenv("MIMI_LOG");
    if (env == nullptr) return;
    if (std::strcmp(env, "trace") == 0)      set_level(Level::Trace);
    else if (std::strcmp(env, "debug") == 0) set_level(Level::Debug);
    else if (std::strcmp(env, "info") == 0)  set_level(Level::Info);
    else if (std::strcmp(env, "warn") == 0)  set_level(Level::Warn);
    else if (std::strcmp(env, "error") == 0) set_level(Level::Error);
    else if (std::strcmp(env, "off") == 0)   set_level(Level::Off);
}

void write(Level lv, std::string_view tag, std::string_view message) noexcept {
    if (lv < level()) return;

    const auto now = std::chrono::system_clock::now();
    const auto secs = std::chrono::floor<std::chrono::seconds>(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - secs);

    std::lock_guard lock(g_write_mutex);
    if (stderr_is_tty()) {
        std::fprintf(stderr, "%s%s\033[0m \033[2m%s.%03d\033[0m \033[35m%-8.*s\033[0m %.*s\n",
                     colour_of(lv).data(), name_of(lv).data(),
                     std::format("{:%H:%M:%S}", secs).c_str(),
                     static_cast<int>(ms.count()),
                     static_cast<int>(tag.size()), tag.data(),
                     static_cast<int>(message.size()), message.data());
    } else {
        std::fprintf(stderr, "%s %s.%03d %-8.*s %.*s\n",
                     name_of(lv).data(),
                     std::format("{:%H:%M:%S}", secs).c_str(),
                     static_cast<int>(ms.count()),
                     static_cast<int>(tag.size()), tag.data(),
                     static_cast<int>(message.size()), message.data());
    }
    std::fflush(stderr);
}

}  // namespace mimi::log
