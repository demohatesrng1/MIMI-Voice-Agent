#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace mimi::log {

enum class Level { Trace = 0, Debug, Info, Warn, Error, Off };

void set_level(Level level) noexcept;
Level level() noexcept;

// Reads MIMI_LOG (trace|debug|info|warn|error|off). Safe to call repeatedly.
void configure_from_env() noexcept;

// The one sink. Everything below funnels here.
void write(Level level, std::string_view tag, std::string_view message) noexcept;

namespace detail {
// Named dispatch(), not emit(): Qt defines `emit` as a preprocessor macro, so
// any function with that name breaks the moment a translation unit includes
// both this header and a Qt one.
template <class... Args>
inline void dispatch(Level lv, std::string_view tag,
                 std::format_string<Args...> fmt, Args&&... args) noexcept {
    if (lv < level()) return;
    try {
        write(lv, tag, std::format(fmt, std::forward<Args>(args)...));
    } catch (...) {
        write(Level::Error, tag, "<log formatting failed>");
    }
}
}  // namespace detail

// Usage: mimi::log::info("audio", "capture started at {} Hz", rate);
template <class... Args>
inline void trace(std::string_view tag, std::format_string<Args...> fmt, Args&&... a) noexcept {
    detail::dispatch(Level::Trace, tag, fmt, std::forward<Args>(a)...);
}
template <class... Args>
inline void debug(std::string_view tag, std::format_string<Args...> fmt, Args&&... a) noexcept {
    detail::dispatch(Level::Debug, tag, fmt, std::forward<Args>(a)...);
}
template <class... Args>
inline void info(std::string_view tag, std::format_string<Args...> fmt, Args&&... a) noexcept {
    detail::dispatch(Level::Info, tag, fmt, std::forward<Args>(a)...);
}
template <class... Args>
inline void warn(std::string_view tag, std::format_string<Args...> fmt, Args&&... a) noexcept {
    detail::dispatch(Level::Warn, tag, fmt, std::forward<Args>(a)...);
}
template <class... Args>
inline void error(std::string_view tag, std::format_string<Args...> fmt, Args&&... a) noexcept {
    detail::dispatch(Level::Error, tag, fmt, std::forward<Args>(a)...);
}

}  // namespace mimi::log
