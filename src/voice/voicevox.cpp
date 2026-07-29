#include "voice/voicevox.hpp"

#include "brain/shell.hpp"
#include "core/log.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <thread>

namespace mimi::voice {
namespace {

constexpr std::string_view kTag = "voicevox";
using json = nlohmann::json;

constexpr const char* kAppPath = "/Applications/VOICEVOX.app";

struct Endpoint {
    std::string host = "localhost";
    int port = 50021;
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
        }
    } else if (!rest.empty()) {
        out.host = rest;
    }
    return out;
}

std::string url_encode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

}  // namespace

struct Voicevox::Impl {
    Endpoint endpoint;

    std::unique_ptr<httplib::Client> connect(std::chrono::seconds timeout) const {
        auto client = std::make_unique<httplib::Client>(endpoint.host, endpoint.port);
        client->set_connection_timeout(2, 0);
        client->set_read_timeout(static_cast<time_t>(timeout.count()), 0);
        return client;
    }
};

Voicevox::Voicevox(Config config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {
    impl_->endpoint = parse(config_.host);
}

Voicevox::~Voicevox() = default;

std::string Voicevox::install_hint() {
    return "VOICEVOX is not installed. Download it from https://voicevox.hiroshiba.jp "
           "and drag it to /Applications.";
}

bool Voicevox::available() const {
    auto client = impl_->connect(std::chrono::seconds{3});
    auto response = client->Get("/version");
    return response && response->status == 200;
}

bool Voicevox::ensure_running(std::chrono::seconds wait) {
    if (available()) return true;

    std::error_code ec;
    if (!std::filesystem::exists(kAppPath, ec)) {
        log::info(kTag, "{}", install_hint());
        return false;
    }

    // Opening the app starts its bundled engine. -j keeps it from stealing
    // focus, which matters for something launched at startup.
    log::info(kTag, "starting VOICEVOX");
    if (!brain::run("open", {"-g", "-j", "-a", kAppPath}, 15).ok()) {
        log::warn(kTag, "could not launch {}", kAppPath);
        return false;
    }

    // The engine loads its models before binding, so this is slow the first
    // time -- tens of seconds, not the sub-second Ollama takes.
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{700});
        if (available()) {
            log::info(kTag, "engine ready");
            return true;
        }
    }
    log::warn(kTag, "engine did not answer within {}s", wait.count());
    return false;
}

std::vector<VoicevoxStyle> Voicevox::styles() const {
    std::vector<VoicevoxStyle> out;
    auto client = impl_->connect(std::chrono::seconds{10});
    auto response = client->Get("/speakers");
    if (!response || response->status != 200) return out;

    try {
        for (const auto& speaker : json::parse(response->body)) {
            const std::string name = speaker.value("name", "");
            for (const auto& style : speaker.value("styles", json::array())) {
                out.push_back({name, style.value("name", ""), style.value("id", 0)});
            }
        }
    } catch (const std::exception& e) {
        log::warn(kTag, "could not read the speaker list: {}", e.what());
    }
    return out;
}

std::vector<std::uint8_t> Voicevox::synthesize(const std::string& text) const {
    std::vector<std::uint8_t> audio;
    if (text.empty()) return audio;

    auto client = impl_->connect(config_.timeout);
    const std::string speaker = std::to_string(config_.style_id);

    // Two steps by design: /audio_query returns an editable prosody plan, and
    // /synthesis renders it. Going straight to synthesis is not possible, and
    // the intermediate is where speed and intonation are set.
    auto query = client->Post(
        "/audio_query?text=" + url_encode(text) + "&speaker=" + speaker, "", "application/json");
    if (!query || query->status != 200) {
        log::warn(kTag, "audio_query failed{}",
                  query ? " (" + std::to_string(query->status) + ")" : "");
        return audio;
    }

    json plan;
    try {
        plan = json::parse(query->body);
    } catch (const std::exception& e) {
        log::warn(kTag, "could not parse audio_query: {}", e.what());
        return audio;
    }

    plan["speedScale"] = config_.speed;
    plan["pitchScale"] = config_.pitch;
    // Flat intonation is most of what makes synthetic speech sound synthetic.
    plan["intonationScale"] = config_.intonation;
    plan["outputStereo"] = false;

    auto rendered =
        client->Post("/synthesis?speaker=" + speaker, plan.dump(), "application/json");
    if (!rendered || rendered->status != 200) {
        log::warn(kTag, "synthesis failed{}",
                  rendered ? " (" + std::to_string(rendered->status) + ")" : "");
        return audio;
    }

    audio.assign(rendered->body.begin(), rendered->body.end());
    log::debug(kTag, "{} chars -> {} KB of audio", text.size(), audio.size() / 1024);
    return audio;
}

}  // namespace mimi::voice
