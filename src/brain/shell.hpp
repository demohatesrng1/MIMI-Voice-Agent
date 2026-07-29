#pragma once

#include <initializer_list>
#include <string>
#include <vector>

namespace mimi::brain {

struct ProcessResult {
    int exit_code = -1;
    std::string out;
    std::string err;
    bool ok() const noexcept { return exit_code == 0; }
};

// Runs a program directly -- fork/exec with an argv array, never a shell.
//
// This matters more here than in most places: the arguments come from a
// language model interpreting speech, so a shell would turn every misheard
// sentence into a potential command injection. With no shell there is nothing
// to inject into; a stray "; rm -rf ~" is just a filename that does not exist.
ProcessResult run(const std::string& program, const std::vector<std::string>& args,
                  int timeout_seconds = 20);

inline ProcessResult run(const std::string& program,
                         std::initializer_list<std::string> args,
                         int timeout_seconds = 20) {
    return run(program, std::vector<std::string>(args), timeout_seconds);
}

// Escapes a string for embedding in an AppleScript literal.
//
// AppleScript is still a *language* being assembled from text, so the no-shell
// argument above does not cover it: an unescaped quote in a reminder closes the
// string and the rest is executed. Everything user- or model-supplied that ends
// up inside a script has to go through here.
std::string applescript_quote(std::string_view text);

// Runs one AppleScript via osascript and returns its stdout, trimmed.
std::string osascript(const std::string& script, int timeout_seconds = 15);

}  // namespace mimi::brain
