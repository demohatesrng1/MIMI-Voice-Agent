#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace mimi::brain {

struct Message {
    std::string role;  // "system" | "user" | "assistant"
    std::string content;
};

struct ChatOptions {
    // Structured output. When set, Ollama constrains decoding to this JSON
    // schema, which is what makes intent classification reliable instead of a
    // parsing exercise.
    std::optional<nlohmann::json> schema;
    std::optional<int> max_tokens;
    std::optional<float> temperature;
    std::vector<Message> history;
};

struct ChatResult {
    std::string text;
    bool ok = false;
    std::string error;
    std::chrono::milliseconds took{0};

    explicit operator bool() const noexcept { return ok; }
};

// Client for a local Ollama server.
//
// Talks plain HTTP to 127.0.0.1, so no TLS and no auth. Everything here is
// blocking and must be called off the GUI thread -- a reply from a 7B model
// takes seconds, not milliseconds.
class Ollama {
public:
    struct Config {
        std::string host = "http://localhost:11434";
        std::string model = "gemma3n:e4b";
        std::string embed_model = "nomic-embed-text";
        // Holds the model in RAM between turns. Without it Ollama unloads after
        // ~5 minutes and the next question pays the whole load cost again.
        std::string keep_alive = "1h";
        std::chrono::seconds timeout{120};
    };

    explicit Ollama(Config config);
    ~Ollama();

    // True when the server answers and `model` is actually pulled.
    bool reachable() const;

    // Makes sure there is a server to talk to: returns straight away if one is
    // already up, otherwise launches `ollama serve` and waits for it to answer.
    // The app depends on Ollama but nothing on a stock machine starts it, so
    // without this the brain is dead until the user runs it by hand.
    bool ensure_running(std::chrono::seconds wait = std::chrono::seconds{20});

    // False when the configured model is not among the pulled ones.
    bool model_available() const;
    std::vector<std::string> models() const;

    ChatResult chat(const std::string& system, const std::string& user,
                    const ChatOptions& options = {}) const;

    // Streams tokens as they arrive. `on_token` is called from this thread.
    // Return false from it to abort the generation early.
    ChatResult chat_stream(const std::string& system, const std::string& user,
                           const std::function<bool(std::string_view)>& on_token,
                           const ChatOptions& options = {}) const;

    std::vector<float> embed(const std::string& text) const;

    // Loads the model now so the first real question is not slowed by it.
    void warmup() const;

    const Config& config() const noexcept { return config_; }
    void set_model(std::string model) { config_.model = std::move(model); }

private:
    struct Impl;
    Config config_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mimi::brain
