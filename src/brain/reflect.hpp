#pragma once

#include "brain/journal.hpp"
#include "brain/ollama.hpp"

#include <optional>
#include <string>

namespace mimi::brain {

// The end-of-day pass: turns one day's journal into a short note, folds that
// into a running profile of the user, then clears the day.
//
// Everything stays on the machine. The profile is what lets Mimi answer with
// some idea of who she is talking to.
class Reflection {
public:
    Reflection(Ollama& ollama, Journal& journal);

    // Reflects on `day` (default today). Returns the note, or nothing when the
    // day was empty. Clearing the day is deliberately the caller's decision --
    // running this twice should not be able to destroy anything.
    std::optional<std::string> run(const std::string& day = {}, bool clear_after = true);

    std::string profile() const;
    std::vector<std::string> recent_digests(int limit = 14) const;

private:
    void save_digest(const std::string& day, const std::string& note) const;
    void update_profile(const std::string& note);

    Ollama& ollama_;
    Journal& journal_;
};

}  // namespace mimi::brain
