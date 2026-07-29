#include "brain/router.hpp"

#include "brain/shell.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <regex>

namespace mimi::brain {
namespace {

constexpr std::string_view kTag = "router";
using json = nlohmann::json;

std::string lowercase(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// True if any of the phrases appears.
template <std::size_t N>
bool any_of(const std::string& text, const std::array<std::string_view, N>& phrases) {
    return std::any_of(phrases.begin(), phrases.end(),
                       [&](std::string_view p) { return has(text, p); });
}

const char* kSystemPrompt =
    "あなたはミミ、ユーザーのMacで動く音声アシスタントです。\n"
    "返事は音声で読み上げられます。次のことを必ず守ってください。\n"
    "・短く自然な話し言葉の日本語で、1〜3文で答える\n"
    "・記号、箇条書き、見出し、絵文字、マークダウンは一切使わない\n"
    "・落ち着いた親しみやすい口調。大げさな相づちは使わない\n"
    "・知らないことは正直に知らないと言う。作り話をしない";

// The action set the classifier may choose from. Kept identical to what the
// switch below can actually execute -- a model that can name an action nobody
// handles produces silent no-ops.
const char* kIntentPrompt =
    "ユーザーの発話をMacアシスタントのコマンドに変換してください。"
    "最も適切な action を一つ選び、必要なら argument を入れてください。"
    "迷ったら chat を選んでください。";

json intent_schema() {
    return json{
        {"type", "object"},
        {"properties",
         {{"action",
           {{"type", "string"},
            {"enum", {"open_site", "search_web", "launch_app", "quit_app", "screenshot",
                      "battery", "time", "system_info", "volume_up", "volume_down",
                      "mute", "unmute", "clipboard", "media_playpause", "media_next",
                      "media_previous", "lock_screen", "find_file", "chat"}}}},
          {"argument", {{"type", "string"}}}}},
        {"required", {"action", "argument"}}};
}

}  // namespace

Router::Router(Ollama& ollama) : Router(ollama, Config{}) {}

Router::Router(Ollama& ollama, Config config)
    : ollama_(ollama), config_(std::move(config)) {}

void Router::remember(const std::string& user, const std::string& assistant) {
    history_.push_back({"user", user});
    history_.push_back({"assistant", assistant});
    while (history_.size() > config_.history_turns * 2) history_.pop_front();
}

Reply Router::route(const std::string& utterance) {
    if (utterance.empty()) return {"すみません、聞き取れませんでした。", "none", "", false};

    const std::string lower = lowercase(utterance);
    log::debug(kTag, "routing '{}'", utterance);

    if (Reply reply = rules(utterance, lower); !reply.action.empty()) return reply;
    return classify_then_route(utterance);
}

// Deterministic matching. Everything here is a phrase people actually use, in
// both languages, and costs no model call.
Reply Router::rules(const std::string& utterance, const std::string& lower) {
    Reply reply;

    // --- reminders, first: they contain other keywords ("5分後に音楽") -------
    if (auto reminder = tools::parse_reminder(utterance)) {
        const auto seconds = reminder->delay.count();
        std::string what = reminder->what.empty() ? "お知らせ" : reminder->what;
        tools::schedule(reminder->delay, what, [this](const std::string& text) {
            tools::notify("ミミ", text);
            if (on_reminder_) on_reminder_(text);
        });
        const auto minutes = seconds / 60;
        reply.text = minutes > 0 ? "はい、" + std::to_string(minutes) + "分後に「" + what +
                                       "」とお知らせします。"
                                 : "はい、" + std::to_string(seconds) + "秒後にお知らせします。";
        reply.action = "reminder";
        reply.detail = what;
        reply.acted = true;
        return reply;
    }

    // --- time ---------------------------------------------------------------
    static constexpr std::array<std::string_view, 6> kTime{"何時", "時間を教え", "今の時刻",
                                                           "what time", "the time", "何日"};
    if (any_of(utterance, kTime) || any_of(lower, kTime)) {
        reply.text = tools::what_time_ja();
        reply.action = "time";
        return reply;
    }

    // --- battery ------------------------------------------------------------
    static constexpr std::array<std::string_view, 3> kBattery{"バッテリー", "電池", "battery"};
    if (any_of(utterance, kBattery) || any_of(lower, kBattery)) {
        const auto status = tools::battery();
        if (!status.valid()) {
            reply.text = "バッテリーの状態が読み取れませんでした。";
        } else {
            reply.text = "バッテリーは" + std::to_string(status.percent) + "パーセント、" +
                         (status.state == "charging"       ? "充電中です。"
                          : status.state == "fully charged" ? "満充電です。"
                                                            : "バッテリー駆動です。");
            if (!status.remaining.empty() && status.state == "on battery") {
                reply.text += "残り" + status.remaining + "くらいです。";
            }
        }
        reply.action = "battery";
        return reply;
    }

    // --- system info --------------------------------------------------------
    static constexpr std::array<std::string_view, 4> kSystem{"システム", "空き容量", "メモリ",
                                                             "disk space"};
    if (any_of(utterance, kSystem) || any_of(lower, kSystem)) {
        const auto info = tools::system_info();
        reply.text = "メモリは" + std::to_string(static_cast<int>(info.memory_used_gb)) + "／" +
                     std::to_string(static_cast<int>(info.memory_total_gb)) +
                     "ギガ使用中、ディスクの空きは" +
                     std::to_string(static_cast<int>(info.disk_free_gb)) + "ギガです。";
        reply.action = "system_info";
        return reply;
    }

    // --- screenshot ---------------------------------------------------------
    static constexpr std::array<std::string_view, 3> kShot{"スクリーンショット", "画面を撮",
                                                           "screenshot"};
    if (any_of(utterance, kShot) || any_of(lower, kShot)) {
        const std::string path = tools::screenshot();
        reply.text = path.empty() ? "スクリーンショットが撮れませんでした。"
                                  : "撮りました。デスクトップに保存してあります。";
        reply.action = "screenshot";
        reply.detail = path;
        reply.acted = !path.empty();
        return reply;
    }

    // --- volume -------------------------------------------------------------
    if (has(utterance, "音量") || has(lower, "volume") || has(utterance, "音を")) {
        if (has(utterance, "上げ") || has(lower, "up") || has(utterance, "大きく")) {
            tools::nudge_volume(+12);
            reply.text = "音量を上げました。";
            reply.action = "volume_up";
        } else if (has(utterance, "下げ") || has(lower, "down") || has(utterance, "小さく")) {
            tools::nudge_volume(-12);
            reply.text = "音量を下げました。";
            reply.action = "volume_down";
        } else {
            reply.text = "音量は" + std::to_string(tools::volume()) + "パーセントです。";
            reply.action = "volume";
        }
        reply.acted = reply.action != "volume";
        return reply;
    }
    if (has(utterance, "ミュート") || has(lower, "mute")) {
        const bool off = has(utterance, "解除") || has(lower, "un");
        tools::set_muted(!off);
        reply.text = off ? "ミュートを解除しました。" : "ミュートしました。";
        reply.action = off ? "unmute" : "mute";
        reply.acted = true;
        return reply;
    }

    // --- media --------------------------------------------------------------
    if (has(utterance, "次の曲") || has(lower, "next track")) {
        tools::media_next();
        reply = {"次の曲にしました。", "media_next", "", true};
        return reply;
    }
    if (has(utterance, "前の曲") || has(lower, "previous track")) {
        tools::media_previous();
        reply = {"前の曲に戻しました。", "media_previous", "", true};
        return reply;
    }
    if (has(utterance, "音楽を止め") || has(utterance, "再生") || has(lower, "play/pause") ||
        has(utterance, "一時停止")) {
        tools::media_playpause();
        reply = {"はい。", "media_playpause", "", true};
        return reply;
    }

    // --- lock ---------------------------------------------------------------
    if (has(utterance, "画面をロック") || has(utterance, "ロックして") ||
        has(lower, "lock the screen")) {
        tools::lock_screen();
        reply = {"画面をロックします。", "lock_screen", "", true};
        return reply;
    }

    // --- clipboard ----------------------------------------------------------
    if (has(utterance, "クリップボード") || has(lower, "clipboard")) {
        const std::string clip = tools::clipboard();
        if (clip.empty()) {
            reply = {"クリップボードは空です。", "clipboard", "", false};
            return reply;
        }
        ChatOptions options;
        options.max_tokens = 160;
        const auto summary = ollama_.chat(
            "次の内容を、声で読み上げる前提で日本語2文以内に要約してください。記号は使わないこと。",
            clip.substr(0, 4000), options);
        reply.text = summary ? summary.text : "うまく要約できませんでした。";
        reply.action = "clipboard";
        return reply;
    }

    // --- open the Nth search result ----------------------------------------
    if (!last_results_.empty() &&
        (has(utterance, "番目") || has(lower, "result") || has(utterance, "つ目"))) {
        int index = 1;
        std::smatch match;
        if (std::regex_search(utterance, match, std::regex(R"((\d+))"))) {
            index = std::stoi(match[1].str());
        } else if (has(utterance, "最初") || has(lower, "first")) {
            index = 1;
        } else if (has(utterance, "二") || has(utterance, "2")) {
            index = 2;
        }
        if (index < 1 || index > static_cast<int>(last_results_.size())) {
            reply = {"その番号の結果はありません。", "open_result", "", false};
            return reply;
        }
        const auto& [title, url] = last_results_[index - 1];
        tools::open_url(url);
        reply.text = std::to_string(index) + "番目、" + title + "を開きます。";
        reply.action = "open_result";
        reply.detail = url;
        reply.acted = true;
        return reply;
    }

    // --- search -------------------------------------------------------------
    if (has(utterance, "検索") || has(lower, "search for") || has(lower, "search ")) {
        std::string query = utterance;
        for (const char* strip : {"を検索して", "を検索", "検索して", "search for ", "search "}) {
            for (auto at = query.find(strip); at != std::string::npos; at = query.find(strip)) {
                query.erase(at, std::strlen(strip));
            }
        }
        query = std::regex_replace(query, std::regex(R"(^[\s、。]+|[\s、。]+$)"), "");
        if (query.empty()) {
            reply = {"何を検索しますか？", "search_web", "", false};
            return reply;
        }
        last_search_ = query;
        last_results_ = tools::web_search(query, 8);
        if (last_results_.empty()) {
            reply = {query + "の検索結果が取れませんでした。", "search_web", query, false};
            return reply;
        }
        reply.text = query + "を検索しました。" + last_results_.front().first +
                     "が一番上です。「一番目を開いて」と言ってください。";
        reply.action = "search_web";
        reply.detail = query;
        reply.acted = true;
        return reply;
    }

    // --- open a site --------------------------------------------------------
    if (has(utterance, "を開いて") || has(utterance, "開いて") || has(lower, "open ") ||
        has(lower, "go to ")) {
        std::string target = utterance;
        for (const char* strip : {"open ", "go to "}) {
            for (auto at = lowercase(target).find(strip); at != std::string::npos;
                 at = lowercase(target).find(strip)) {
                target.erase(at, std::strlen(strip));
                break;
            }
        }
        const std::string url = tools::url_for_site(target);
        if (!url.empty() && tools::open_url(url)) {
            reply.text = url.substr(url.find("//") + 2) + "を開きます。";
            reply.action = "open_site";
            reply.detail = url;
            reply.acted = true;
            return reply;
        }
    }

    // --- launch / quit an app ----------------------------------------------
    if (has(utterance, "を起動") || has(lower, "launch ")) {
        std::string name = utterance;
        for (const char* strip : {"を起動して", "を起動", "launch "}) {
            for (auto at = name.find(strip); at != std::string::npos; at = name.find(strip)) {
                name.erase(at, std::strlen(strip));
            }
        }
        name = std::regex_replace(name, std::regex(R"(^[\s、。]+|[\s、。]+$)"), "");
        if (!name.empty() && tools::open_app(name)) {
            reply.text = name + "を起動します。";
            reply.action = "launch_app";
            reply.detail = name;
            reply.acted = true;
            return reply;
        }
    }

    return reply;  // action empty -> nothing matched
}

Reply Router::classify_then_route(const std::string& utterance) {
    ChatOptions options;
    options.schema = intent_schema();
    options.max_tokens = 60;

    const auto result = ollama_.chat(kIntentPrompt, utterance, options);
    if (!result) {
        log::debug(kTag, "classifier unavailable, answering directly");
        return converse(utterance);
    }

    std::string action = "chat";
    std::string argument;
    try {
        const auto parsed = json::parse(result.text);
        action = parsed.value("action", "chat");
        argument = parsed.value("argument", "");
    } catch (const std::exception& e) {
        log::debug(kTag, "could not read the classifier's answer: {}", e.what());
        return converse(utterance);
    }

    log::debug(kTag, "classified as {} ({})", action, argument);
    if (action == "chat" || action.empty()) return converse(utterance);

    // Re-enter the deterministic path with a canonical phrasing, so an action
    // is executed by exactly one piece of code no matter how it was reached.
    Reply reply;
    if (action == "open_site" && !argument.empty()) {
        const std::string url = tools::url_for_site(argument);
        if (tools::open_url(url)) {
            reply = {argument + "を開きます。", "open_site", url, true};
            return reply;
        }
    } else if (action == "launch_app" && !argument.empty()) {
        if (tools::open_app(argument)) {
            reply = {argument + "を起動します。", "launch_app", argument, true};
            return reply;
        }
        reply = {argument + "というアプリが見つかりませんでした。", "launch_app", argument, false};
        return reply;
    } else if (action == "quit_app" && !argument.empty()) {
        tools::quit_app(argument);
        reply = {argument + "を終了します。", "quit_app", argument, true};
        return reply;
    } else if (action == "search_web" && !argument.empty()) {
        last_search_ = argument;
        last_results_ = tools::web_search(argument, 8);
        reply = {argument + "を検索しました。", "search_web", argument, true};
        return reply;
    } else if (action == "find_file" && !argument.empty()) {
        const auto hits = tools::find_files(argument);
        if (hits.empty()) {
            reply = {argument + "は見つかりませんでした。", "find_file", argument, false};
        } else {
            tools::reveal_in_finder(hits.front());
            reply = {hits.size() == 1 ? "見つけました。Finderで開きます。"
                                      : std::to_string(hits.size()) +
                                            "件見つけました。一番近いものを開きます。",
                     "find_file", hits.front(), true};
        }
        return reply;
    } else if (action == "screenshot") {
        const std::string path = tools::screenshot();
        reply = {"撮りました。", "screenshot", path, !path.empty()};
        return reply;
    } else if (action == "battery" || action == "time" || action == "system_info" ||
               action == "volume_up" || action == "volume_down" || action == "mute" ||
               action == "unmute" || action == "clipboard" || action == "media_playpause" ||
               action == "media_next" || action == "media_previous" ||
               action == "lock_screen") {
        // These all have a rule; reach it with wording the rule recognises.
        static const std::map<std::string, std::string> kCanonical{
            {"battery", "バッテリーは？"},        {"time", "今何時？"},
            {"system_info", "システムの状態は？"}, {"volume_up", "音量を上げて"},
            {"volume_down", "音量を下げて"},      {"mute", "ミュートして"},
            {"unmute", "ミュートを解除して"},     {"clipboard", "クリップボードを要約して"},
            {"media_playpause", "再生して"},      {"media_next", "次の曲"},
            {"media_previous", "前の曲"},         {"lock_screen", "画面をロックして"},
        };
        const auto found = kCanonical.find(action);
        if (found != kCanonical.end()) {
            return rules(found->second, lowercase(found->second));
        }
    }
    return converse(utterance);
}

Reply Router::converse(const std::string& utterance) {
    ChatOptions options;
    options.max_tokens = 220;
    options.history.assign(history_.begin(), history_.end());

    const auto result = ollama_.chat(kSystemPrompt, utterance, options);
    Reply reply;
    reply.action = "chat";
    if (!result) {
        reply.text = "今、頭が動いていません。オラマが起動しているか確認してください。";
        log::warn(kTag, "{}", result.error);
        return reply;
    }
    reply.text = result.text;
    remember(utterance, reply.text);
    return reply;
}

}  // namespace mimi::brain
