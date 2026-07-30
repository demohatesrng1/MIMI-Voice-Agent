#pragma once

#include "brain/journal.hpp"
#include "brain/notes.hpp"
#include "brain/ollama.hpp"
#include "brain/tools.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mimi::brain {

// What one utterance turned into.
struct Reply {
    std::string text;         // what Mimi says back, in Japanese
    std::string action;       // the action taken, for the journal ("open_site", …)
    std::string detail;       // the argument, a URL, a file path…
    bool acted = false;       // did she touch the machine, or only talk?
};

// The prompt and schema the classifier actually uses. Exposed so tools can
// exercise the real thing: mimi_brain_cli previously carried its own copy and
// was quietly measuring a prompt that production never ran.
const char* intent_prompt();
nlohmann::json intent_schema();

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
    // One classified step. Empty `action` in the result means it did nothing.
    Reply execute_step(const std::string& action, const std::string& argument);
    Reply converse(const std::string& utterance);
    // Reads notes back aloud; `when_empty` is spoken when there are none.
    // Reads back the reminders still waiting.
    Reply speak_reminders();
    Reply speak_notes(const std::vector<Note>& notes, const std::string& when_empty);
    // Answers a question about what is in the notes, by giving the model the
    // notes themselves. `question` empty means "summarise them".
    Reply think_about_notes(const std::string& question);
    void remember(const std::string& user, const std::string& assistant);

    Ollama& ollama_;
    Notes notes_;
    Journal journal_;
    Config config_;
    std::deque<Message> history_;
    ReminderHandler on_reminder_;

    // The last search, so "2番目を開いて" ("open the second one") can work.
    std::string last_search_;
    std::vector<std::pair<std::string, std::string>> last_results_;
};

}  // namespace mimi::brain
