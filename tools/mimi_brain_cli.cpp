// mimi_brain_cli -- exercise the Ollama client without the app.
//
//   mimi_brain_cli --check              server reachable? which models?
//   mimi_brain_cli "今何時ですか"        one turn
//   mimi_brain_cli --stream "..."       token by token
//   mimi_brain_cli --schema "..."       structured intent classification

#include "brain/ollama.hpp"
#include "brain/router.hpp"
#include "core/log.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    mimi::log::configure_from_env();

    mimi::brain::Ollama::Config config;
    std::string prompt;
    bool check = false, stream = false, schema = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--check") check = true;
        else if (arg == "--stream") stream = true;
        else if (arg == "--schema") schema = true;
        else if (arg == "--model" && i + 1 < argc) config.model = argv[++i];
        else prompt = arg;
    }

    mimi::brain::Ollama ollama(config);

    if (check) {
        std::printf("server    : %s\n", ollama.reachable() ? "up" : "DOWN");
        std::printf("model     : %s\n", config.model.c_str());
        std::puts("available :");
        for (const auto& m : ollama.models()) std::printf("  %s\n", m.c_str());
        return ollama.reachable() ? 0 : 1;
    }

    if (prompt.empty()) prompt = "こんにちは。あなたは誰ですか？";

    const std::string system =
        "あなたはミミ、ユーザーのアシスタントです。返事は声で読み上げられるので、"
        "短く自然な日本語で、1〜2文で答えてください。記号や箇条書きは使わないこと。";

    if (schema) {
        // The real prompt and schema, not a copy that can drift from them.
        mimi::brain::ChatOptions options;
        options.schema = mimi::brain::intent_schema();
        options.max_tokens = 60;
        const auto result =
            ollama.chat(mimi::brain::intent_prompt(), prompt, options);
        std::printf("\n%s\n", result ? result.text.c_str() : result.error.c_str());
        std::printf("(%lld ms)\n", static_cast<long long>(result.took.count()));
        return result ? 0 : 1;
    }

    if (stream) {
        std::printf("\n");
        const auto result = ollama.chat_stream(system, prompt, [](std::string_view piece) {
            std::fwrite(piece.data(), 1, piece.size(), stdout);
            std::fflush(stdout);
            return true;
        });
        std::printf("\n\n(%lld ms)\n", static_cast<long long>(result.took.count()));
        return result ? 0 : 1;
    }

    const auto result = ollama.chat(system, prompt);
    std::printf("\n%s\n", result ? result.text.c_str() : result.error.c_str());
    std::printf("\n(%lld ms)\n", static_cast<long long>(result.took.count()));
    return result ? 0 : 1;
}
