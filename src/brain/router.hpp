#pragma once

#include "brain/ollama.hpp"
#include "brain/tools.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace mimi::brain {

// What one utterance turned into.
struct Reply {
    std::string text;         // what Mimi says back, in Japanese
    std::string action;       // the action taken, for the journal ("open_site", …)
    std::string detail;       // the argument, a URL, a file path…
    bool acted = false;       // did she touch the machine, or only talk?
};

// Turns an utterance into an action, then into something to say.
//
// Same shape as route() in the Python: cheap deterministic matching first, and
// only utterances that match nothing pay for a model round-trip to classify
// them. Two reasons that ordering matters here more than it did before --
// "電気を消して" should not wait two seconds behind a 7B model, and the phrases
// people actually use for these commands are a small, closed set.
class Router {
public:
    struct Config {
        std::string language = "ja";
        // Turns of conversation kept for context. Each turn is roughly 60
        // tokens, and gemma3n starts wandering with much more than this.
        std::size_t history_turns = 8;
    };

    // Called when a reminder fires, from a background thread.
    using ReminderHandler = std::function<void(const std::string& text)>;

    explicit Router(Ollama& ollama);
    Router(Ollama& ollama, Config config);

    Reply route(const std::string& utterance);

    void on_reminder(ReminderHandler handler) { on_reminder_ = std::move(handler); }
    void clear_history() { history_.clear(); }

private:
    Reply rules(const std::string& utterance, const std::string& lower);
    Reply classify_then_route(const std::string& utterance);
    Reply converse(const std::string& utterance);
    void remember(const std::string& user, const std::string& assistant);

    Ollama& ollama_;
    Config config_;
    std::deque<Message> history_;
    ReminderHandler on_reminder_;

    // The last search, so "2番目を開いて" ("open the second one") can work.
    std::string last_search_;
    std::vector<std::pair<std::string, std::string>> last_results_;
};

}  // namespace mimi::brain
