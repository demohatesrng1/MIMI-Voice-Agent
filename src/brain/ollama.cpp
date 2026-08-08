#include "brain/ollama.hpp"

#include "brain/shell.hpp"
#include "core/log.hpp"

#include <httplib.h>

#include <algorithm>
#include <cstdlib>
#include <thread>
#include <stdexcept>

namespace mimi::brain {
namespace {

constexpr std::string_view kTag = "ollama";
using json = nlohmann::json;

// Splits "http://host:port" into the pieces httplib wants.
struct Endpoint {
    std::string host;
    int port = 11434;
};

Endpoint parse(const std::string& url) {
    Endpoint out;
    std::string rest = url;
    if (const auto at = rest.find("://"); at != std::string::npos) rest = rest.substr(at + 3);
    if (const auto slash = rest.find('/'); slash != std::string::npos) rest = rest.substr(0, slash);
    if (const auto colon = rest.rfind(':'); colon != std::string::npos) {
        out.host = rest.substr(0, colon);
        try {
            out.port = std::stoi(rest.substr(colon + 1));
        } catch (...) {
            out.port = 11434;
        }
    } else {
        out.host = rest;
    }
    if (out.host.empty()) out.host = "localhost";
    return out;
}

}  // namespace

struct Ollama::Impl {
    Endpoint endpoint;

    std::unique_ptr<httplib::Client> connect(std::chrono::seconds timeout) const {
        auto client = std::make_unique<httplib::Client>(endpoint.host, endpoint.port);
        client->set_connection_timeout(3, 0);
        client->set_read_timeout(static_cast<time_t>(timeout.count()), 0);
        client->set_write_timeout(10, 0);
        return client;
    }
};

Ollama::Ollama(Config config) : config_(std::move(config)), impl_(std::make_unique<Impl>()) {
    // Environment overrides, as the Python original had. Useful for pointing at
    // a remote Ollama, and for testing what happens when there isn't one.
    if (const char* host = std::getenv("MIMI_OLLAMA_HOST")) config_.host = host;
    if (const char* model = std::getenv("MIMI_OLLAMA_MODEL")) config_.model = model;
    // Deliberately accepts the empty string: MIMI_FAST_MODEL="" turns the small
    // classifier off, which is how the speed/accuracy trade-off gets measured
    // rather than assumed.
    if (const char* fast = std::getenv("MIMI_FAST_MODEL")) config_.fast_model = fast;
    impl_->endpoint = parse(config_.host);
    log::debug(kTag, "{}:{} model={}", impl_->endpoint.host, impl_->endpoint.port,
               config_.model);
}

Ollama::~Ollama() = default;

bool Ollama::reachable() const {
    auto client = impl_->connect(std::chrono::seconds{5});
    auto response = client->Get("/api/version");
    return response && response->status == 200;
}

namespace {

// Ollama reports "gemma3n:e4b"; a config may reasonably omit the tag.
bool pulled_matches(const std::vector<std::string>& pulled, const std::string& wanted) {
    if (wanted.empty()) return false;
    return std::any_of(pulled.begin(), pulled.end(), [&](const std::string& name) {
        return name == wanted || name.rfind(wanted + ":", 0) == 0 ||
               wanted.rfind(name, 0) == 0;
    });
}

}  // namespace

bool Ollama::model_available() const { return pulled_matches(models(), config_.model); }

// The classifier is optional. When it is missing everything still works, just
// with the big model doing the small job, so this is a question and never an
// error.
bool Ollama::fast_model_available() const {
    if (config_.fast_model.empty() || config_.fast_model == config_.model) return false;
    return pulled_matches(models(), config_.fast_model);
}

std::string Ollama::classifier() const {
    return fast_model_available() ? config_.fast_model : config_.model;
}

bool Ollama::ensure_running(std::chrono::seconds wait) {
    if (reachable()) return true;

    log::info(kTag, "no server on {}, starting one", config_.host);
    if (!spawn_detached("ollama", {"serve"})) {
        log::warn(kTag, "could not start ollama -- is it installed?");
        return false;
    }

    // It needs a moment to bind the port. Poll rather than sleeping a fixed
    // amount, so a fast machine is not made to wait.
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{400});
        if (reachable()) {
            log::info(kTag, "server is up");
            return true;
        }
    }
    log::warn(kTag, "ollama did not come up within {}s", wait.count());
    return false;
}

std::vector<std::string> Ollama::models() const {
    std::vector<std::string> out;
    auto client = impl_->connect(std::chrono::seconds{10});
    auto response = client->Get("/api/tags");
    if (!response || response->status != 200) return out;

    try {
        const auto parsed = json::parse(response->body);
        for (const auto& model : parsed.value("models", json::array())) {
            out.push_back(model.value("name", ""));
        }
    } catch (const std::exception& e) {
        log::warn(kTag, "could not read model list: {}", e.what());
    }
    return out;
}

namespace {

json build_request(const Ollama::Config& config, const std::string& system,
                   const std::string& user, const ChatOptions& options, bool stream) {
    json messages = json::array();
    if (!system.empty()) messages.push_back({{"role", "system"}, {"content", system}});
    for (const auto& message : options.history) {
        messages.push_back({{"role", message.role}, {"content", message.content}});
    }
    messages.push_back({{"role", "user"}, {"content", user}});

    json request{
        {"model", options.model.empty() ? config.model : options.model},
        {"messages", std::move(messages)},
        {"stream", stream},
        {"keep_alive", config.keep_alive},
    };

    json opts = json::object();
    if (options.max_tokens) opts["num_predict"] = *options.max_tokens;
    if (options.temperature) opts["temperature"] = *options.temperature;

    if (options.schema) {
        request["format"] = *options.schema;
        // Structured output only behaves if decoding is deterministic;
        // sampling produces schema-valid but semantically random fields.
        if (!options.temperature) opts["temperature"] = 0;
    }
    if (!opts.empty()) request["options"] = std::move(opts);
    return request;
}

}  // namespace

ChatResult Ollama::chat(const std::string& system, const std::string& user,
                        const ChatOptions& options) const {
    ChatResult result;
    const auto started = std::chrono::steady_clock::now();

    auto client = impl_->connect(config_.timeout);
    const json request = build_request(config_, system, user, options, false);

    auto response = client->Post("/api/chat", request.dump(), "application/json");
    result.took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    if (!response) {
        result.error = "Ollama is not answering on " + config_.host;
        log::warn(kTag, "{}", result.error);
        return result;
    }
    if (response->status != 200) {
        result.error = "Ollama returned " + std::to_string(response->status) + ": " +
                       response->body.substr(0, 200);
        log::warn(kTag, "{}", result.error);
        return result;
    }

    try {
        const auto parsed = json::parse(response->body);
        result.text = parsed.value("message", json::object()).value("content", "");
        result.ok = true;
    } catch (const std::exception& e) {
        result.error = std::string("could not parse the reply: ") + e.what();
        log::warn(kTag, "{}", result.error);
        return result;
    }

    log::debug(kTag, "{} chars in {} ms", result.text.size(), result.took.count());
    return result;
}

ChatResult Ollama::chat_stream(const std::string& system, const std::string& user,
                               const std::function<bool(std::string_view)>& on_token,
                               const ChatOptions& options) const {
    ChatResult result;
    const auto started = std::chrono::steady_clock::now();

    auto client = impl_->connect(config_.timeout);
    const json request = build_request(config_, system, user, options, true);

    // Ollama streams newline-delimited JSON, and a chunk can split mid-object,
    // so hold a buffer and only parse on a complete line.
    std::string pending;
    bool aborted = false;

    // cpp-httplib only offers a content receiver on Get(), so a streamed POST
    // has to go through the low-level send() with the receiver on the Request.
    httplib::Request http_request;
    http_request.method = "POST";
    http_request.path = "/api/chat";
    http_request.set_header("Content-Type", "application/json");
    http_request.body = request.dump();
    http_request.content_receiver = [&](const char* data, std::size_t length,
                                        std::uint64_t /*offset*/,
                                        std::uint64_t /*total*/) -> bool {
        pending.append(data, length);
        std::size_t start = 0;
        for (;;) {
            const auto newline = pending.find('\n', start);
            if (newline == std::string::npos) break;
            const std::string line = pending.substr(start, newline - start);
            start = newline + 1;
            if (line.empty()) continue;
            try {
                const auto parsed = json::parse(line);
                const std::string piece =
                    parsed.value("message", json::object()).value("content", "");
                if (!piece.empty()) {
                    result.text += piece;
                    if (on_token && !on_token(piece)) {
                        aborted = true;
                        return false;
                    }
                }
            } catch (const std::exception&) {
                // A partial or malformed line; wait for more bytes.
            }
        }
        pending.erase(0, start);
        return true;
    };

    httplib::Response http_response;
    httplib::Error error = httplib::Error::Success;
    const bool sent = client->send(http_request, http_response, error);

    result.took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    if (aborted) {
        result.ok = true;  // stopped on purpose, not a failure
        return result;
    }
    if (!sent) {
        result.error = "Ollama is not answering on " + config_.host + " (" +
                       httplib::to_string(error) + ")";
        log::warn(kTag, "{}", result.error);
        return result;
    }
    if (http_response.status != 200) {
        result.error = "Ollama returned " + std::to_string(http_response.status);
        log::warn(kTag, "{}", result.error);
        return result;
    }

    // Depending on how httplib routes the response it may hand the body to the
    // receiver above or buffer it whole. Cover the second case so the caller
    // always gets the text, even if it arrives in one piece rather than as a
    // stream of tokens.
    if (result.text.empty() && !http_response.body.empty()) {
        log::debug(kTag, "response was buffered, not streamed");
        std::size_t start = 0;
        const std::string& body = http_response.body;
        while (start < body.size()) {
            auto newline = body.find('\n', start);
            if (newline == std::string::npos) newline = body.size();
            const std::string line = body.substr(start, newline - start);
            start = newline + 1;
            if (line.empty()) continue;
            try {
                const auto parsed = json::parse(line);
                const std::string piece =
                    parsed.value("message", json::object()).value("content", "");
                if (!piece.empty()) {
                    result.text += piece;
                    if (on_token && !on_token(piece)) break;
                }
            } catch (const std::exception&) {
                // Not a complete JSON line; skip it.
            }
        }
    }

    result.ok = true;
    return result;
}

std::vector<float> Ollama::embed(const std::string& text) const {
    std::vector<float> out;
    auto client = impl_->connect(config_.timeout);

    const json request{{"model", config_.embed_model}, {"input", text}};
    auto response = client->Post("/api/embed", request.dump(), "application/json");
    if (!response || response->status != 200) {
        log::warn(kTag, "embed failed{}",
                  response ? " (" + std::to_string(response->status) + ")" : "");
        return out;
    }

    try {
        const auto parsed = json::parse(response->body);
        // /api/embed returns a batch; /api/embeddings returned a single vector.
        if (parsed.contains("embeddings") && !parsed["embeddings"].empty()) {
            out = parsed["embeddings"][0].get<std::vector<float>>();
        } else if (parsed.contains("embedding")) {
            out = parsed["embedding"].get<std::vector<float>>();
        }
    } catch (const std::exception& e) {
        log::warn(kTag, "could not parse embedding: {}", e.what());
    }
    return out;
}

void Ollama::warmup() const {
    ChatOptions options;
    options.max_tokens = 1;
    const auto result = chat("", "hi", options);
    log::info(kTag, "{} {}", config_.model,
              result ? "ready" : "unavailable: " + result.error);

    // Warm the classifier too, and warm it *second* so it is the most recently
    // touched: it is on the path of every single utterance, and paying its load
    // cost on the first thing the user says would undo the point of it.
    if (fast_model_available()) {
        ChatOptions fast;
        fast.max_tokens = 1;
        fast.model = config_.fast_model;
        const auto quick = chat("", "hi", fast);
        log::info(kTag, "{} {} (classifier)", config_.fast_model,
                  quick ? "ready" : "unavailable: " + quick.error);
    } else if (!config_.fast_model.empty()) {
        log::info(kTag, "no {} -- classifying with {} instead (slower; `ollama pull {}`)",
                  config_.fast_model, config_.model, config_.fast_model);
    }
}

}  // namespace mimi::brain
