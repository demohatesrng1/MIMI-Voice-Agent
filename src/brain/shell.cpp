#include "brain/shell.hpp"

#include "core/log.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <cstring>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace mimi::brain {
namespace {

constexpr std::string_view kTag = "shell";

std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

// Reads both pipes until EOF or the deadline, so a chatty child cannot fill a
// pipe buffer and deadlock while we wait on the other one.
void drain(int out_fd, int err_fd, std::string& out, std::string& err,
           std::chrono::steady_clock::time_point deadline) {
    std::array<pollfd, 2> fds{pollfd{out_fd, POLLIN, 0}, pollfd{err_fd, POLLIN, 0}};
    std::array<char, 4096> buffer{};
    int open_count = 2;

    while (open_count > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

        const int ready = ::poll(fds.data(), fds.size(), static_cast<int>(remaining));
        if (ready <= 0) break;

        for (std::size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].fd < 0 || (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;
            const auto got = ::read(fds[i].fd, buffer.data(), buffer.size());
            if (got > 0) {
                (i == 0 ? out : err).append(buffer.data(), static_cast<std::size_t>(got));
            } else {
                fds[i].fd = -1;
                --open_count;
            }
        }
    }
}

}  // namespace

ProcessResult run(const std::string& program, const std::vector<std::string>& args,
                  int timeout_seconds) {
    ProcessResult result;

    int out_pipe[2];
    int err_pipe[2];
    if (::pipe(out_pipe) != 0) return result;
    if (::pipe(err_pipe) != 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[0]);

    // argv is built as an array; nothing is ever concatenated into a command
    // line, so nothing can be reinterpreted as syntax.
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawned =
        ::posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);

    posix_spawn_file_actions_destroy(&actions);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    if (spawned != 0) {
        log::warn(kTag, "cannot run {}: {}", program, std::strerror(spawned));
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        return result;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    drain(out_pipe[0], err_pipe[0], result.out, result.err, deadline);
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    int status = 0;
    for (;;) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0) {
            result.exit_code = -1;
            return result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            log::warn(kTag, "{} timed out after {}s, killing it", program, timeout_seconds);
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            result.exit_code = -1;
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

bool spawn_detached(const std::string& program, const std::vector<std::string>& args) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // Detach from our stdio, or the child keeps our pipes open and inherits a
    // terminal it will happily write to.
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawned =
        ::posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawned != 0) {
        log::debug(kTag, "could not launch {}: {}", program, std::strerror(spawned));
        return false;
    }
    // Reap it if it dies immediately; otherwise leave it running. Without this
    // a failed launch would sit as a zombie for the life of the app.
    int status = 0;
    ::waitpid(pid, &status, WNOHANG);
    log::info(kTag, "started {} (pid {})", program, pid);
    return true;
}

std::string applescript_quote(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            // A literal newline ends the statement; the escape sequence does not.
            case '\n': out += "\\n"; break;
            case '\r': break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string osascript(const std::string& script, int timeout_seconds) {
    const auto result = run("osascript", {"-e", script}, timeout_seconds);
    if (!result.ok() && !result.err.empty()) {
        const std::string error = trim(result.err);
        // -1743 is macOS refusing the Apple event because Automation has not
        // been granted. It looks identical to "the app isn't running" from the
        // caller's side, so say plainly what actually needs to happen.
        if (error.find("-1743") != std::string::npos ||
            error.find("Not authorized") != std::string::npos) {
            log::warn(kTag,
                      "automation is blocked -- allow it in System Settings > Privacy & "
                      "Security > Automation, then try again");
        } else {
            log::debug(kTag, "osascript: {}", error);
        }
    }
    return trim(result.out);
}

}  // namespace mimi::brain
