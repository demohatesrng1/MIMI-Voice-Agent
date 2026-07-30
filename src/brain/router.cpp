#include "brain/router.hpp"

#include "brain/accessibility.hpp"
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

// Trims the whitespace and punctuation that surrounds a spoken phrase, at both
// ends. Done by whole tokens rather than with a character class: a regex here is
// byte-oriented, so "、" inside a bracket expression matches its individual
// bytes and happily shears the first byte off a leading kana.
std::string trim_speech(std::string text) {
    static const std::array<std::string_view, 8> kJunk{"、", "。", "！", "？",
                                                       "・", "，", "　", "…"};
    for (bool changed = true; changed;) {
        changed = false;
        while (!text.empty() &&
               (std::isspace(static_cast<unsigned char>(text.front())) ||
                std::strchr(",.!?;:-", text.front()) != nullptr)) {
            text.erase(0, 1);
            changed = true;
        }
        while (!text.empty() &&
               (std::isspace(static_cast<unsigned char>(text.back())) ||
                std::strchr(",.!?;:-", text.back()) != nullptr)) {
            text.pop_back();
            changed = true;
        }
        for (std::string_view junk : kJunk) {
            if (text.compare(0, junk.size(), junk) == 0) {
                text.erase(0, junk.size());
                changed = true;
            }
            if (text.size() >= junk.size() &&
                text.compare(text.size() - junk.size(), junk.size(), junk) == 0) {
                text.erase(text.size() - junk.size());
                changed = true;
            }
        }
    }
    return text;
}

// True when the utterance probably carries two commands rather than one.
//
// Deliberately conservative: a false positive costs a model call on something
// the rules could have answered for free, but a false negative silently drops
// half of what was asked. The test is a sequencing word plus a second verb,
// because 「て」 alone is far too common -- "音量を上げて" is one command and
// ends in it.
bool looks_like_multi_command(const std::string& utterance, const std::string& lower) {
    // Words that join two instructions. 「そして」「それから」 are unambiguous;
    // 「てから」 marks "after doing X".
    static constexpr std::array<std::string_view, 10> kJoiners{
        "そして", "それから", "そのあと", "その後", "てから",
        " and then ", " then ", " after that ", "、あと", "ついでに"};
    const bool joined = std::any_of(kJoiners.begin(), kJoiners.end(), [&](std::string_view j) {
        return has(utterance, j) || has(lower, j);
    });

    // Verb endings that close an instruction. Two of them means two commands.
    static constexpr std::array<std::string_view, 6> kImperatives{
        "して", "開いて", "上げて", "下げて", "教えて", "かけて"};
    int verbs = 0;
    for (std::string_view ending : kImperatives) {
        for (auto at = utterance.find(ending); at != std::string::npos;
             at = utterance.find(ending, at + ending.size())) {
            ++verbs;
        }
    }
    if (verbs >= 2) return true;
    // A joiner on its own is enough only when something follows it.
    return joined && verbs >= 1;
}

// True when the utterance is asking something rather than ordering something.
// Japanese marks this at the end -- か, っけ, の -- and English at the start.
bool looks_like_question(const std::string& utterance, const std::string& lower) {
    static constexpr std::array<std::string_view, 14> kMarkers{
        "？", "?", "っけ", "ですか", "ますか", "かな", "何", "どこ", "誰", "いつ",
        "what", "where", "who", "when"};
    if (std::any_of(kMarkers.begin(), kMarkers.end(), [&](std::string_view m) {
            return has(utterance, m) || has(lower, m);
        })) {
        return true;
    }
    // A sentence left hanging on its topic particle -- "田中さんの番号は" -- is a
    // question in speech even though it carries no question word. Commands are
    // matched by the rules above, so this only ever sees what is left over.
    const std::string trimmed = trim_speech(utterance);
    for (std::string_view tail : {"は", "が", "って"}) {
        if (trimmed.size() > tail.size() &&
            trimmed.compare(trimmed.size() - tail.size(), tail.size(), tail) == 0) {
            return true;
        }
    }
    return false;
}

// A dotted name whose suffix is a document extension rather than a TLD.
// "report.pdf" is a file; "example.com" is a site. Anything unrecognised is
// left to the site reading, since that is the older behaviour.
bool looks_like_filename(const std::string& text) {
    const auto dot = text.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= text.size()) return false;
    const std::string suffix = lowercase(text.substr(dot + 1));
    static const std::array<std::string_view, 24> kExtensions{
        "txt",  "pdf",  "doc",  "docx", "xls", "xlsx", "ppt", "pptx",
        "png",  "jpg",  "jpeg", "gif",  "heic", "mp3", "mp4", "mov",
        "wav",  "zip",  "csv",  "json", "md",  "pages", "numbers", "key"};
    return std::any_of(kExtensions.begin(), kExtensions.end(),
                       [&](std::string_view e) { return suffix == e; });
}

// Drops a case particle left dangling at the end once a noun has been stripped
// off: "デスクトップのフォルダ" minus フォルダ is "デスクトップの", which matches
// no folder name.
std::string strip_trailing_particle(std::string text) {
    static const std::array<std::string_view, 5> kParticles{"の", "を", "に", "へ", "と"};
    for (std::string_view particle : kParticles) {
        if (text.size() > particle.size() &&
            text.compare(text.size() - particle.size(), particle.size(), particle) == 0) {
            text.erase(text.size() - particle.size());
            break;
        }
    }
    return text;
}

// Removes every listed phrase and trims what is left. Longer phrases must come
// before their prefixes ("を検索して" before "検索"), or the shorter one wins and
// leaves fragments behind.
std::string strip_words(std::string text, std::initializer_list<const char*> words) {
    for (const char* word : words) {
        const std::size_t length = std::strlen(word);
        // Case-insensitive matching is done on a lowercased copy, but only for
        // ASCII phrases. `lowercase` is byte-wise, which mangles UTF-8
        // continuation bytes, so an offset found in the copy of a Japanese
        // string would land mid-character and cut a kana in half.
        const bool ascii = std::all_of(word, word + length, [](unsigned char c) {
            return c < 0x80;
        });
        for (;;) {
            const auto at = ascii ? lowercase(text).find(word) : text.find(word);
            if (at == std::string::npos) break;
            text.erase(at, length);
        }
    }
    return trim_speech(text);
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
// Each action is described, not just named. A bare list of enum values leaves
// the model guessing what "screenshot" covers, and it falls back to search_web
// for anything phrased indirectly -- "画面の写真がほしい" was classified as a
// web search until these descriptions were added.
const char* kIntentPromptText =
    "ユーザーの発話をMacアシスタントのコマンドに変換してください。"
    "最も適切な action を一つだけ選び、必要なら argument を入れてください。\n"
    "open_site      ウェブサイトを開きたい。argument はサイト名\n"
    "search_web     ブラウザを開いて検索してほしいと明示的に頼まれたとき。"
    "argument は検索語。「検索して」「調べて」「ググって」と言われた場合だけ。"
    "単に事実を知りたい質問は chat であって search_web ではない\n"
    "launch_app     Macのアプリを起動したい。argument はアプリ名\n"
    "quit_app       アプリを終了したい。argument はアプリ名\n"
    "screenshot     画面を撮りたい、画面の写真や画像がほしい\n"
    "battery        バッテリーや電池の残量を知りたい\n"
    "time           時刻や日付を知りたい\n"
    "system_info    メモリやディスクの空き、Macの状態を知りたい\n"
    "volume_up      音を大きくしたい\n"
    "volume_down    音を小さくしたい、うるさい\n"
    "mute           音を消したい\n"
    "unmute         ミュートを解除したい\n"
    "clipboard      コピーした内容を読みたい、要約してほしい\n"
    "media_playpause 音楽や動画の再生・一時停止\n"
    "media_next     次の曲へ\n"
    "media_previous 前の曲へ\n"
    "lock_screen    画面をロックしたい\n"
    "find_file      ファイルを探したい。argument はファイル名\n"
    "open_folder    フォルダを開きたい。argument はフォルダ名（ダウンロード等）\n"
    "open_file      ファイルを開きたい。argument はファイル名\n"
    "call           電話をかけたい。argument は相手の名前か電話番号\n"
    "video_call     ビデオ通話、フェイスタイムをかけたい。argument は相手\n"
    "click_button   今開いているアプリのボタンを押したい。argument はボタンの名前\n"
    "type_text      今の入力欄に文字を入力したい。argument は入力する文字\n"
    "menu_item      メニューバーの項目を選びたい。argument は「ファイル > 保存」の形式\n"
    "what_is_on_screen 今の画面に何があるか、どんなボタンがあるか知りたい\n"
    "take_note      メモを取りたい、書き留めてほしい。argument はメモの内容\n"
    "read_notes     最近のメモを読み上げてほしい\n"
    "open_notes     メモ帳やメモの画面を開きたい\n"
    "search_notes   メモを探したい。argument は探す言葉\n"
    "ask_notes      メモの内容について質問したい、要約してほしい、まとめてほしい。"
    "argument は質問。要約だけなら空文字\n"
    "chat           それ以外すべて。知識を問う質問、雑談、説明の依頼。"
    "「〜について教えて」「〜とは何」「〜はどこ」など、答えを知りたいだけの質問は"
    "すべて chat\n"
    "迷ったら chat を選んでください。argument が不要なときは空文字にしてください。\n"
    "\n"
    "ひとつの発話にふたつの命令が含まれるときは、ふたつ目を action2 と argument2 に"
    "入れてください。命令がひとつだけのときは action2 を空文字にします。\n"
    "例「クロームを開いて猫を検索して」→ action=launch_app, argument=クローム, "
    "action2=search_web, argument2=猫\n"
    "例「音量を上げて次の曲」→ action=volume_up, action2=media_next\n"
    "例「今何時」→ action=time, action2は空文字";

const std::vector<std::string>& action_names() {
    static const std::vector<std::string> kNames{
        "open_site", "search_web", "launch_app", "quit_app", "screenshot", "battery",
        "time", "system_info", "volume_up", "volume_down", "mute", "unmute",
        "clipboard", "media_playpause", "media_next", "media_previous", "lock_screen",
        "find_file", "open_folder", "open_file", "call", "video_call", "click_button",
        "type_text", "menu_item", "what_is_on_screen", "take_note", "read_notes",
        "search_notes", "ask_notes", "open_notes", "chat"};
    return kNames;
}

// Two flat slots rather than an array of steps.
//
// "クロームを開いて猫を検索して" is two commands in one breath, and the old
// one-action schema could only ever do half of it. An array of step objects is
// the obvious shape, but gemma3n:e4b fills it with whitespace and returns an
// empty list more often than not -- a nested array is past what a model this
// small reliably emits. Two named slots at the top level cost it nothing and
// cover the overwhelming majority of real utterances, which are one or two
// commands. A third would need the array back.
json build_intent_schema() {
    const json enumeration{{"type", "string"}, {"enum", action_names()}};
    return json{
        {"type", "object"},
        {"properties",
         {{"action", enumeration},
          {"argument", {{"type", "string"}}},
          {"action2", enumeration},
          {"argument2", {{"type", "string"}}}}},
        {"required", {"action", "argument"}}};
}

}  // namespace

const char* intent_prompt() { return kIntentPromptText; }
nlohmann::json intent_schema() { return build_intent_schema(); }

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

    // The rules below match a keyword anywhere in the sentence and return as
    // soon as one fires, which is wrong for "音量を上げて次の曲にして": the 音量
    // rule answers and the second half is silently dropped. When the utterance
    // looks like it carries more than one command, go straight to the
    // classifier, which can express both. Single commands -- the overwhelming
    // majority -- still take the free deterministic path.
    // Everything she does is recorded once, here, because this is the single
    // point every utterance passes through. The journal was written months ago
    // and never actually filled: the timeline had nothing real to draw, so it
    // drew a story about a marketing proposal instead.
    const auto record = [this, &utterance](const Reply& reply) {
        journal_.log(reply.action.empty() ? "chat" : reply.action,
                     {{"said", utterance},
                      {"replied", reply.text},
                      {"detail", reply.detail},
                      {"acted", reply.acted}});
        return reply;
    };

    if (!looks_like_multi_command(utterance, lower)) {
        if (Reply reply = rules(utterance, lower); !reply.action.empty()) {
            return record(reply);
        }
    } else {
        log::debug(kTag, "looks like more than one command, using the classifier");
        if (Reply reply = classify_then_route(utterance); reply.action != "chat") {
            return record(reply);
        }
        // The classifier declined to find commands; the rules may still know it.
        if (Reply reply = rules(utterance, lower); !reply.action.empty()) {
            return record(reply);
        }
        return record(converse(utterance));
    }
    // A question the notes can answer is a question for the notes, and it has to
    // be caught before the classifier: asked "田中さんの番号は", the classifier
    // picked `call` and tried to dial a contact that does not exist, while the
    // number was sitting in a note. Commands are already handled above, so what
    // reaches here is either a question or something no rule understood.
    if (looks_like_question(utterance, lower) && !notes_.relevant(utterance, 3).empty()) {
        log::debug(kTag, "a note bears on this question; answering from it");
        return record(think_about_notes(utterance));
    }
    return record(classify_then_route(utterance));
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
            // Banner, then the spoken cue, then the reminder itself. The cue is
            // what makes a reminder land when the window is behind something --
            // a silent banner in the corner is missed, which for a reminder is
            // the same as never having set it.
            //
            // The banner names what it is about in its subtitle, and what she
            // says is a sentence rather than the bare fragment that was stored:
            // hearing just 「休憩」 does not tell you a reminder went off.
            const std::string spoken = tools::reminder_announcement(text);
            tools::notify("ミミ", "リマインダー", spoken);
            tools::play_notification_cue();
            // A reminder that fired is something that happened, so it belongs
            // in the journal with everything else she remembers.
            journal_.log("reminder_fired",
                         {{"said", "リマインダー"}, {"replied", spoken}, {"acted", true}});
            if (on_reminder_) on_reminder_(spoken);
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

    // --- notes --------------------------------------------------------------
    //
    // Sits above the time rule on purpose: "会議は何時からだっけ" is a question
    // about a note, and 何時 would otherwise have her read the clock out.
    //
    // The three readings of a sentence containing メモ are kept strictly apart,
    // because conflating them was the bug: any utterance mentioning メモ that
    // fell through became a *new note*, so asking "メモに猫のことは書いてある"
    // silently wrote a note called "に猫のことは書いてある" instead of answering.
    // Taking a note now needs an explicit verb; a question is answered.
    {
    // 「メモ帳を開いて」 asks for a notepad, and she has one -- so it opens hers.
    // It used to resolve to TextEdit, which made her own notes the one place
    // the word never took you. Checked before the app rules, which would
    // otherwise match macOS's own Notes.app.
    if (has(utterance, "メモ帳") || has(lower, "notepad")) {
        reply = {"メモを開きます。", "open_notes", "", true};
        return reply;
    }

    const bool mentions_notes = has(utterance, "メモ") || has(lower, "note");

    // An explicit instruction to write something down.
    const bool asks_to_write =
        has(utterance, "メモして") || has(utterance, "メモしと") ||
        has(utterance, "メモを取") || has(utterance, "メモしておいて") ||
        has(utterance, "とメモ") || has(utterance, "書き留め") ||
        has(utterance, "記録して") || has(lower, "take a note") ||
        has(lower, "note that") || has(lower, "write this down") ||
        has(lower, "write down");

    // A question -- about the notes, or answerable from them.
    const bool asks_question =
        has(utterance, "？") || has(utterance, "?") || has(utterance, "っけ") ||
        has(utterance, "何て") || has(utterance, "何と") || has(utterance, "なんて") ||
        has(utterance, "書いてある") || has(utterance, "書いた") ||
        has(utterance, "どうなって") || has(utterance, "教えて") ||
        has(utterance, "要約") || has(utterance, "まとめ") || has(utterance, "整理") ||
        has(utterance, "ある?") || has(utterance, "あった") ||
        has(lower, "summar") || has(lower, "what did i") || has(lower, "what's in");

    if (mentions_notes && (asks_question || has(utterance, "要約") ||
                           has(utterance, "まとめ"))) {
        const std::string question = strip_words(
            utterance, {"メモを要約して", "メモをまとめて", "メモを整理して", "メモの内容",
                        "メモには", "メモから", "メモにある", "メモに", "メモを", "メモは",
                        "メモ", "summarise my notes", "summarize my notes", "my notes",
                        "in my notes", "notes", "note", "please"});
        return think_about_notes(question);
    }

    // Reading them back: titles only, no model call.
    if (mentions_notes && !asks_to_write &&
        (has(utterance, "見せ") || has(utterance, "読") || has(utterance, "開") ||
         has(lower, "read") || has(lower, "show") || has(lower, "list"))) {
        return speak_notes(notes_.all(3), "メモはまだありません。");
    }

    // Finding one.
    if (mentions_notes && !asks_to_write &&
        (has(utterance, "探して") || has(utterance, "検索") || has(lower, "find") ||
         has(lower, "search"))) {
        const std::string query = strip_trailing_particle(strip_words(
            utterance, {"に関するメモを探して", "についてのメモを探して", "メモを探して",
                        "メモを検索して", "メモから探して", "メモ", "を探して", "探して",
                        "を検索して", "検索して", "search my notes for ",
                        "search notes for ", "find in my notes ", "find my notes ",
                        "find note ", "notes", "note", "about ", "please"}));
        if (query.empty()) {
            reply = {"メモから何を探しますか？", "search_notes", "", false};
            return reply;
        }
        return speak_notes(notes_.search(query, 3),
                           query + "に関するメモは見つかりませんでした。");
    }

    // Writing one. Requires an explicit verb, so a passing mention of メモ can
    // never create a note by accident.
    if (asks_to_write) {
        const std::string body = strip_words(
            utterance, {"とメモしておいて", "というメモを取って", "とメモして", "メモしといて",
                        "メモしておいて", "メモを取って", "メモして", "とメモ", "メモ",
                        "を書き留めて", "書き留めて", "を記録して", "記録して",
                        "take a note that ", "take a note ", "note that ",
                        "write this down", "write down ", "please"});
        if (body.empty()) {
            reply = {"何をメモしますか？", "take_note", "", false};
            return reply;
        }
        const Note note = notes_.add(body);
        reply = {note.valid() ? "メモしました。" : "メモを書けませんでした。", "take_note",
                 note.id, note.valid()};
        return reply;
    }
    }

    // --- time ---------------------------------------------------------------
    //
    // 何時 only means "what o'clock is it" when nothing else is the subject.
    // "会議は何時からだっけ" is a question about a meeting, and answering it with
    // the wall clock is worse than not answering: the topic particle before 何時
    // is what separates the two.
    static constexpr std::array<std::string_view, 6> kTime{"何時", "時間を教え", "今の時刻",
                                                           "what time", "the time", "何日"};
    const auto oclock = utterance.find("何時");
    const bool about_something_else =
        oclock != std::string::npos &&
        (utterance.substr(0, oclock).find("は") != std::string::npos ||
         utterance.substr(0, oclock).find("が") != std::string::npos);
    if (!about_something_else && (any_of(utterance, kTime) || any_of(lower, kTime))) {
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
        // A bare failure is unactionable: the cause is almost always the Screen
        // Recording permission, which macOS never prompts for on its own and
        // which the user has no way to guess at from "it did not work".
        if (path.empty() && ax::screen_recording_access() != ax::Access::Granted) {
            // Told, not done: yanking System Settings open every time someone
            // asks for a screenshot is worse than saying where the switch is.
            // Mimi's own Settings page carries the button.
            reply = {"画面収録の許可がないので撮れません。"
                     "ミミの設定画面から許可してください。",
                     "screenshot", "", false};
            return reply;
        }
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
    // "search this", "google that", "調べて" all mean: put it in the browser.
    static constexpr std::array<std::string_view, 6> kSearch{
        "検索", "調べて", "search", "google ", "look up", "ググ"};
    if (any_of(utterance, kSearch) || any_of(lower, kSearch)) {
        const std::string query = strip_words(
            utterance, {"について検索して", "を検索して", "を検索", "検索して", "検索",
                        "について調べて", "を調べて", "調べて", "ググって", "ググる",
                        "search for ", "search the web for ", "search up ", "search ",
                        "google for ", "google ", "look up ", "on the web", "please"});
        if (query.empty()) {
            reply = {"何を検索しますか？", "search_web", "", false};
            return reply;
        }
        last_search_ = query;
        // Open it in the browser first: that is what "search this" asks for.
        // The scraped results are extra, so "一番目を開いて" still works.
        const bool opened = tools::search_in_browser(query);
        last_results_ = tools::web_search(query, 8);
        reply.text = opened ? query + "をブラウザで検索します。"
                            : query + "の検索が開けませんでした。";
        if (opened && !last_results_.empty()) {
            reply.text += last_results_.front().first + "が一番上です。";
        }
        reply.action = "search_web";
        reply.detail = query;
        reply.acted = opened;
        return reply;
    }

    // --- driving the front app's interface ----------------------------------
    // Ahead of the app rule: "保存ボタンを押して" is not a request to launch an
    // application called 保存.
    if (has(utterance, "ボタンを押") || has(utterance, "をクリック") ||
        has(lower, "click the") || has(lower, "press the")) {
        const std::string label = strip_words(
            utterance, {"というボタンを押して", "ボタンを押して", "ボタンを押す", "ボタン",
                        "をクリックして", "クリックして", "を押して", "押して",
                        "click the ", "click ", "press the ", "press ", "button",
                        "please"});
        if (label.empty()) {
            reply = {"どのボタンを押しますか？", "click_button", "", false};
            return reply;
        }
        if (!ax::has_permission()) {
            reply = {"操作の許可が必要です。システム設定のプライバシーとセキュリティで、"
                     "アクセシビリティにミミを追加してください。",
                     "click_button", label, false};
            return reply;
        }
        const bool pressed = ax::press(label);
        reply = {pressed ? label + "を押しました。"
                         : label + "というボタンが見つかりませんでした。",
                 "click_button", label, pressed};
        return reply;
    }

    // What is in front of me, and what can I do with it?
    if (has(utterance, "画面に何") || has(utterance, "何が表示") ||
        has(lower, "what's on screen") || has(lower, "what is on screen") ||
        has(utterance, "どんなボタン")) {
        if (!ax::has_permission()) {
            reply = {"画面を読むには、システム設定のアクセシビリティでミミを"
                     "許可してください。",
                     "what_is_on_screen", "", false};
            return reply;
        }
        const auto found = ax::controls(40);
        const std::string app = ax::frontmost_app();
        if (found.empty()) {
            reply = {app.empty() ? "今の画面が読み取れませんでした。"
                                 : app + "には操作できるものが見つかりませんでした。",
                     "what_is_on_screen", app, false};
            return reply;
        }
        std::string names;
        for (std::size_t i = 0; i < found.size() && i < 6; ++i) {
            if (!names.empty()) names += "、";
            names += found[i].title;
        }
        reply = {app + "が開いています。" + names + "などがあります。", "what_is_on_screen",
                 app, false};
        return reply;
    }

    // --- calls --------------------------------------------------------------
    static constexpr std::array<std::string_view, 5> kCall{"電話", "通話", "call ",
                                                           "facetime", "フェイスタイム"};
    if (any_of(utterance, kCall) || any_of(lower, kCall)) {
        const bool video = has(utterance, "ビデオ") || has(utterance, "テレビ電話") ||
                           has(lower, "video") || has(lower, "facetime") ||
                           has(utterance, "フェイスタイム");
        const std::string who = strip_words(
            utterance, {"にビデオ通話をかけて", "にテレビ電話をかけて", "に電話をかけて",
                        "に電話して", "にかけて", "へ電話をかけて", "へ電話して",
                        "ビデオ通話", "テレビ電話", "フェイスタイム", "電話をかけて",
                        "電話して", "電話", "通話", "video call ", "facetime ", "call ",
                        "phone ", "please"});
        if (who.empty()) {
            reply = {"誰に電話をかけますか？", "call", "", false};
            return reply;
        }
        if (tools::place_call(who, video)) {
            reply.text = who + (video ? "にビデオ通話をかけます。" : "に電話をかけます。");
            reply.action = video ? "video_call" : "call";
            reply.detail = who;
            reply.acted = true;
        } else {
            reply = {who + "の電話番号が連絡先に見つかりませんでした。",
                     video ? "video_call" : "call", who, false};
        }
        return reply;
    }

    // --- folders ------------------------------------------------------------
    // Ahead of the app rule: "ダウンロードフォルダを開いて" would otherwise be
    // taken for an application name and resolve to nothing.
    if (has(utterance, "フォルダ") || has(lower, "folder") || has(lower, "directory")) {
        const std::string spoken = strip_words(
            utterance, {"というフォルダを開いて", "フォルダを開いて", "フォルダを表示",
                        "フォルダ", "を開いて", "開いて", " folder", "folder",
                        "directory", "open ", "show me ", "the ", "my ", "please"});
        // "デスクトップのフォルダ" leaves a dangling の once フォルダ is gone.
        const std::string trimmed = strip_trailing_particle(spoken);
        const std::string path =
            tools::folder_for_name(trimmed.empty() ? utterance : trimmed);
        if (!path.empty() && tools::open_path(path)) {
            reply.text = (trimmed.empty() ? path : trimmed) + "を開きます。";
            reply.action = "open_folder";
            reply.detail = path;
            reply.acted = true;
        } else {
            reply = {"そのフォルダが見つかりませんでした。", "open_folder", spoken, false};
        }
        return reply;
    }

    // --- open an app, or a site --------------------------------------------
    // Apps come first: "open spotify" almost always means the app, and the site
    // fallback would otherwise swallow every name as a domain.
    static constexpr std::array<std::string_view, 8> kOpen{
        "を開いて", "を起動", "開いて",  "起動して",
        "open ",    "launch ", "start ", "go to "};
    if (any_of(utterance, kOpen) || any_of(lower, kOpen)) {
        const std::string target = strip_words(
            utterance, {"を開いて", "を開く", "を起動して", "を起動", "開いて", "起動して",
                        "launch ", "open up ", "open ", "start ", "go to ", "please",
                        "for me"});
        if (target.empty()) {
            reply = {"何を開きますか？", "launch_app", "", false};
            return reply;
        }

        // "report.txt" and "example.com" are both dotted names; only one of them
        // is a website. An explicit file word, or a suffix that is not a TLD,
        // means the caller meant a file.
        const bool wants_file = has(utterance, "ファイル") || has(lower, "file");
        const bool wants_site = !wants_file && (has(lowercase(target), ".com") ||
                                                has(lowercase(target), "http") ||
                                                has(lower, "website") ||
                                                has(utterance, "サイト"));
        if (wants_file || looks_like_filename(target)) {
            if (const std::string opened = tools::open_file(target); !opened.empty()) {
                reply = {target + "を開きます。", "open_file", opened, true};
                return reply;
            }
        }
        if (!wants_site) {
            const std::string app = tools::resolve_app_name(target);
            if (!app.empty() && tools::open_app(app)) {
                reply.text = app + "を起動します。";
                reply.action = "launch_app";
                reply.detail = app;
                reply.acted = true;
                return reply;
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
        // Not an app and not a site: the last reading of "open X" is a file.
        if (const std::string opened = tools::open_file(target); !opened.empty()) {
            reply = {target + "を開きます。", "open_file", opened, true};
            return reply;
        }
        reply = {target + "が見つかりませんでした。", "launch_app", target, false};
        return reply;
    }

    // --- quit an app --------------------------------------------------------
    if (has(utterance, "を終了") || has(utterance, "を閉じて") || has(lower, "quit ") ||
        has(lower, "close ")) {
        const std::string target = strip_words(
            utterance, {"を終了して", "を終了", "を閉じて", "閉じて", "quit ", "close ",
                        "please"});
        if (!target.empty()) {
            std::string app = tools::resolve_app_name(target);
            if (app.empty()) app = target;
            tools::quit_app(app);
            reply = {app + "を終了します。", "quit_app", app, true};
            return reply;
        }
    }

    return reply;  // action empty -> nothing matched
}

Reply Router::classify_then_route(const std::string& utterance) {
    ChatOptions options;
    options.schema = build_intent_schema();
    options.max_tokens = 200;  // room for several steps, not just one

    const auto result = ollama_.chat(kIntentPromptText, utterance, options);
    if (!result) {
        log::debug(kTag, "classifier unavailable, answering directly");
        return converse(utterance);
    }

    std::vector<std::pair<std::string, std::string>> steps;
    try {
        const auto parsed = json::parse(result.text);
        steps.emplace_back(parsed.value("action", ""), parsed.value("argument", ""));
        if (!parsed.value("action2", "").empty()) {
            steps.emplace_back(parsed.value("action2", ""), parsed.value("argument2", ""));
        }
    } catch (const std::exception& e) {
        log::debug(kTag, "could not read the classifier's answer: {}", e.what());
        return converse(utterance);
    }

    if (steps.empty()) return converse(utterance);
    // A lone `chat` is a conversation, not a command. Mixed in with real actions
    // it is the model padding the list, so it gets dropped below instead.
    if (steps.size() == 1 && (steps.front().first == "chat" || steps.front().first.empty())) {
        return converse(utterance);
    }

    Reply combined;
    int performed = 0;
    for (const auto& [action, argument] : steps) {
        if (action.empty() || action == "chat") continue;
        log::debug(kTag, "step {}/{}: {} ({})", performed + 1, steps.size(), action, argument);
        const Reply step = execute_step(action, argument);
        if (step.action.empty()) continue;
        // Each step speaks its own sentence; they are read out back to back.
        if (!combined.text.empty() && !step.text.empty()) combined.text += " ";
        combined.text += step.text;
        combined.action = combined.action.empty() ? step.action : combined.action + "+" + step.action;
        if (combined.detail.empty()) combined.detail = step.detail;
        combined.acted = combined.acted || step.acted;
        ++performed;
    }
    if (performed == 0) return converse(utterance);
    return combined;
}

// Runs one classified step. Re-enters the deterministic path with a canonical
// phrasing where possible, so an action is executed by exactly one piece of
// code no matter how it was reached.
Reply Router::execute_step(const std::string& action, const std::string& argument) {
    Reply reply;
    if (action == "open_site" && !argument.empty()) {
        const std::string url = tools::url_for_site(argument);
        if (tools::open_url(url)) {
            reply = {argument + "を開きます。", "open_site", url, true};
            return reply;
        }
    } else if (action == "launch_app" && !argument.empty()) {
        std::string app = tools::resolve_app_name(argument);
        if (app.empty()) app = argument;
        if (tools::open_app(app)) {
            reply = {app + "を起動します。", "launch_app", app, true};
            return reply;
        }
        // Some names are both an app and a site; try the site before giving up.
        const std::string url = tools::url_for_site(argument);
        if (!url.empty() && tools::open_url(url)) {
            reply = {argument + "を開きます。", "open_site", url, true};
            return reply;
        }
        reply = {argument + "というアプリが見つかりませんでした。", "launch_app", argument, false};
        return reply;
    } else if (action == "quit_app" && !argument.empty()) {
        std::string app = tools::resolve_app_name(argument);
        if (app.empty()) app = argument;
        tools::quit_app(app);
        reply = {app + "を終了します。", "quit_app", app, true};
        return reply;
    } else if (action == "search_web" && !argument.empty()) {
        last_search_ = argument;
        const bool opened = tools::search_in_browser(argument);
        last_results_ = tools::web_search(argument, 8);
        reply = {opened ? argument + "をブラウザで検索します。"
                        : argument + "の検索が開けませんでした。",
                 "search_web", argument, opened};
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
    } else if (action == "open_folder" && !argument.empty()) {
        const std::string path = tools::folder_for_name(argument);
        if (path.empty() || !tools::open_path(path)) {
            reply = {argument + "というフォルダが見つかりませんでした。", "open_folder",
                     argument, false};
        } else {
            reply = {argument + "を開きます。", "open_folder", path, true};
        }
        return reply;
    } else if (action == "open_file" && !argument.empty()) {
        const std::string opened = tools::open_file(argument);
        if (opened.empty()) {
            reply = {argument + "というファイルが見つかりませんでした。", "open_file",
                     argument, false};
        } else {
            reply = {argument + "を開きます。", "open_file", opened, true};
        }
        return reply;
    } else if ((action == "call" || action == "video_call") && !argument.empty()) {
        const bool video = action == "video_call";
        if (tools::place_call(argument, video)) {
            reply = {argument + (video ? "にビデオ通話をかけます。" : "に電話をかけます。"),
                     action, argument, true};
        } else {
            reply = {argument + "の電話番号が見つかりませんでした。", action, argument, false};
        }
        return reply;
    } else if (action == "take_note" && !argument.empty()) {
        const Note note = notes_.add(argument);
        reply = {note.valid() ? "メモしました。" : "メモを書けませんでした。", "take_note",
                 note.id, note.valid()};
        return reply;
    } else if (action == "open_notes") {
        reply = {"メモを開きます。", "open_notes", "", true};
        return reply;
    } else if (action == "read_notes") {
        return speak_notes(notes_.all(3), "メモはまだありません。");
    } else if (action == "search_notes" && !argument.empty()) {
        return speak_notes(notes_.search(argument, 3),
                           argument + "に関するメモは見つかりませんでした。");
    } else if (action == "ask_notes") {
        return think_about_notes(argument);
    } else if (action == "click_button" || action == "type_text" ||
               action == "menu_item" || action == "what_is_on_screen") {
        // Everything here drives another app's interface, which macOS gates
        // behind a permission the user grants once in System Settings. Say so
        // plainly rather than reporting a failure the user cannot interpret.
        if (!ax::has_permission()) {
            reply = {"操作の許可が必要です。システム設定のプライバシーとセキュリティで、"
                     "アクセシビリティにミミを追加してください。",
                     action, "", false};
            return reply;
        }
        if (action == "what_is_on_screen") {
            const auto found = ax::controls(40);
            const std::string app = ax::frontmost_app();
            if (found.empty()) {
                reply = {app.empty() ? "今の画面が読み取れませんでした。"
                                     : app + "には操作できるものが見つかりませんでした。",
                         action, app, false};
                return reply;
            }
            std::string names;
            for (std::size_t i = 0; i < found.size() && i < 6; ++i) {
                if (!names.empty()) names += "、";
                names += found[i].title;
            }
            reply = {app + "が開いています。" + names + "などがあります。", action, app, false};
            return reply;
        }
        if (action == "click_button" && !argument.empty()) {
            const bool pressed = ax::press(argument);
            reply = {pressed ? argument + "を押しました。"
                             : argument + "というボタンが見つかりませんでした。",
                     action, argument, pressed};
            return reply;
        }
        if (action == "type_text" && !argument.empty()) {
            const bool typed = ax::type_text(argument);
            reply = {typed ? "入力しました。" : "入力できませんでした。", action, argument,
                     typed};
            return reply;
        }
        if (action == "menu_item" && !argument.empty()) {
            // "ファイル > 保存" and "File > Save" both arrive as one string.
            std::vector<std::string> path;
            std::string current;
            for (std::size_t i = 0; i < argument.size(); ++i) {
                if (argument[i] == '>') {
                    path.push_back(trim_speech(current));
                    current.clear();
                } else {
                    current.push_back(argument[i]);
                }
            }
            path.push_back(trim_speech(current));
            path.erase(std::remove_if(path.begin(), path.end(),
                                      [](const std::string& s) { return s.empty(); }),
                       path.end());
            const bool clicked = !path.empty() && ax::menu_click(path);
            reply = {clicked ? argument + "を選びました。"
                             : argument + "というメニューが見つかりませんでした。",
                     action, argument, clicked};
            return reply;
        }
        return reply;
    } else if (action == "screenshot") {
        const std::string path = tools::screenshot();
        if (path.empty() && ax::screen_recording_access() != ax::Access::Granted) {
            // Told, not done: yanking System Settings open every time someone
            // asks for a screenshot is worse than saying where the switch is.
            // Mimi's own Settings page carries the button.
            reply = {"画面収録の許可がないので撮れません。"
                     "ミミの設定画面から許可してください。",
                     "screenshot", "", false};
            return reply;
        }
        reply = {path.empty() ? "スクリーンショットが撮れませんでした。" : "撮りました。",
                 "screenshot", path, !path.empty()};
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
    return reply;  // action still empty -> the caller skips this step
}

// Reads a handful of notes back. Spoken, so it is a sentence rather than a
// list: numbering them aloud ("1. … 2. …") sounds like a form being read out.
Reply Router::speak_notes(const std::vector<Note>& notes, const std::string& when_empty) {
    Reply reply;
    reply.action = "read_notes";
    if (notes.empty()) {
        reply.text = when_empty;
        return reply;
    }
    reply.text = notes.size() == 1
                     ? "メモがひとつあります。"
                     : std::to_string(notes.size()) + "件あります。新しいものから、";
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (i > 0) reply.text += "次に、";
        reply.text += notes[i].title + "。";
    }
    reply.detail = notes.front().id;
    return reply;
}

// Answers questions about the notes by handing the model the notes themselves.
//
// Nothing here is templated or keyword-matched: the notes go in as text and the
// model reads them. That is the whole point -- "what did I say about the
// meeting" cannot be answered by a rule, because the answer lives in whatever
// the user happened to write down.
Reply Router::think_about_notes(const std::string& question) {
    Reply reply;
    reply.action = "ask_notes";

    // Pull the notes the question is about. A question with content narrows by
    // search first; anything too vague to search falls back to the recent ones,
    // which is what "summarise my notes" wants anyway.
    std::vector<Note> relevant;
    if (!question.empty()) relevant = notes_.search(question, 8);
    if (relevant.empty()) relevant = notes_.all(12);
    if (relevant.empty()) {
        reply.text = "メモがまだありません。";
        return reply;
    }

    std::string corpus;
    for (const auto& note : relevant) {
        // Dates matter for "what did I write yesterday", and the body is what
        // actually answers anything.
        corpus += "- [" + note.created.substr(0, 10) + "] " + note.body + "\n";
        if (corpus.size() > 6000) break;  // keep the prompt inside the context
    }

    const std::string system =
        "あなたはユーザーのメモを読んで質問に答えるアシスタントです。\n"
        "・答えはメモに書かれている内容だけに基づくこと。推測や作り話をしない\n"
        "・メモに書かれていない話題を聞かれたら、必ず「メモにはありません」と答える。"
        "書かれていないことを、あるかのように言ってはいけない\n"
        "・音声で読み上げるので、記号や箇条書きを使わず、短い話し言葉の日本語で2〜4文\n"
        "・日付や数字はメモのとおりに正確に伝える";

    const std::string user =
        question.empty()
            ? "次のメモ全体を要約してください。\n\n" + corpus
            : "次のメモを読んで質問に答えてください。\n\n質問: " + question + "\n\nメモ:\n" +
                  corpus;

    ChatOptions options;
    options.max_tokens = 260;
    options.temperature = 0.2f;  // reading, not inventing
    const auto result = ollama_.chat(system, user, options);
    if (!result) {
        // The notes themselves were found; only the summarising failed. Read
        // them out instead of losing the answer entirely.
        log::warn(kTag, "{}", result.error);
        Reply fallback = speak_notes(relevant, "メモはありません。");
        fallback.text = "うまくまとめられませんでしたが、" + fallback.text;
        return fallback;
    }
    reply.text = result.text;
    reply.detail = std::to_string(relevant.size()) + " notes";
    remember(question.empty() ? "メモを要約して" : question, reply.text);
    return reply;
}

Reply Router::converse(const std::string& utterance) {
    // Her notes are checked before her general knowledge, because a question
    // about the user's own life is almost never answerable from a 4B model and
    // the model will confidently invent an answer anyway -- asked when a meeting
    // was, it produced a time and a room, both fabricated, while the real answer
    // sat in a note. Only a genuine overlap of subject matter diverts here, so
    // "日本の首都はどこ" still gets answered normally.
    if (const auto related = notes_.relevant(utterance, 5); !related.empty()) {
        log::debug(kTag, "{} note(s) bear on this; answering from them", related.size());
        return think_about_notes(utterance);
    }

    ChatOptions options;
    options.max_tokens = 220;
    options.history.assign(history_.begin(), history_.end());

    const auto result = ollama_.chat(kSystemPrompt, utterance, options);
    Reply reply;
    reply.action = "chat";
    if (!result) {
        // Everything rule-driven still works with no model at all -- the time,
        // the battery, apps, folders, notes, reminders. Saying only "my head
        // isn't working" made her look entirely dead when most of her was fine.
        reply.text =
            "今は考える方が動いていません。時刻やバッテリー、アプリを開く、"
            "メモを取るといったことはできます。";
        reply.action = "offline";
        log::warn(kTag, "{}", result.error);
        return reply;
    }
    reply.text = result.text;
    remember(utterance, reply.text);
    return reply;
}

}  // namespace mimi::brain
