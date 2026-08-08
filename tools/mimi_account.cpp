// The local account, from the terminal.
//
//   mimi_account                 who is signed up on this Mac
//   mimi_account --password      set a new password, keeping everything else
//   mimi_account --forget        delete the account (the journal is untouched)
//
// The account gate is the one thing that can lock you out of your own machine's
// assistant, and a GUI with no way back is a trap. --password is the way out
// that does not cost you the journal; --forget is the way out that starts over.
//
// There is no "recover the old password" here and there cannot be: only a
// PBKDF2 digest is stored, which is the point of storing it that way.

#include "brain/account.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace {

// Reads a line with the terminal's echo turned off, so the password does not
// end up on screen or in a screen recording.
std::string read_secret(const char* prompt) {
    std::fputs(prompt, stderr);
    std::fflush(stderr);

    termios original{};
    const bool is_tty = ::tcgetattr(STDIN_FILENO, &original) == 0;
    if (is_tty) {
        termios quiet = original;
        quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
    }

    std::string line;
    if (!std::getline(std::cin, line)) line.clear();

    if (is_tty) {
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
        std::fputc('\n', stderr);
    }
    return line;
}

}  // namespace

int main(int argc, char** argv) {
    mimi::log::configure_from_env();
    mimi::brain::Accounts accounts;

    const std::string command = argc > 1 ? argv[1] : "";

    if (command == "--forget") {
        if (!accounts.exists()) {
            std::fprintf(stderr, "No account on this Mac; nothing to forget.\n");
            return 1;
        }
        const auto account = accounts.load();
        std::fprintf(stderr, "This deletes the account for '%s'. The journal, notes and\n"
                             "digests are left alone. Type the username to confirm: ",
                     account.username.c_str());
        std::string typed;
        std::getline(std::cin, typed);
        if (typed != account.username) {
            std::fprintf(stderr, "Not confirmed; nothing changed.\n");
            return 1;
        }
        if (!accounts.forget()) {
            std::fprintf(stderr, "Could not delete the account file.\n");
            return 1;
        }
        std::fprintf(stderr, "Done. Mimi will ask you to sign up next time she starts.\n");
        return 0;
    }

    if (command == "--password") {
        if (!accounts.exists()) {
            std::fprintf(stderr, "No account on this Mac. Start Mimi and sign up first.\n");
            return 1;
        }
        const std::string first = read_secret("New password: ");
        if (first.size() < 8) {
            std::fprintf(stderr, "Use at least 8 characters.\n");
            return 1;
        }
        if (read_secret("Again: ") != first) {
            std::fprintf(stderr, "Those did not match; nothing changed.\n");
            return 1;
        }
        if (!accounts.set_password(first)) {
            std::fprintf(stderr, "Could not write the account file.\n");
            return 1;
        }
        std::fprintf(stderr, "Password changed. Sign in as '%s'.\n",
                     accounts.load().username.c_str());
        return 0;
    }

    if (!command.empty() && command != "--show") {
        std::fprintf(stderr, "usage: mimi_account [--show | --password | --forget]\n");
        return 2;
    }

    if (!accounts.exists()) {
        std::printf("No account yet. Start Mimi to sign up.\n");
        return 0;
    }
    const auto account = accounts.load();
    std::printf("username  %s\n", account.username.c_str());
    std::printf("name      %s\n", account.name.c_str());
    std::printf("calls you %s\n", account.preferred.c_str());
    std::printf("file      %s\n",
                mimi::paths::data_file("account.json").string().c_str());
    return 0;
}
