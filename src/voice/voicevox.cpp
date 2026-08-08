#include "voice/voicevox.hpp"

#include "core/log.hpp"
#include "core/paths.hpp"

#include <cstdlib>
#include <mutex>
#include <system_error>

#ifdef MIMI_HAS_VOICEVOX
#include <nlohmann/json.hpp>
#include <voicevox_core.h>
#endif

namespace mimi::voice {
namespace {

constexpr std::string_view kTag = "voicevox";
namespace fs = std::filesystem;

// The fetched tree looks like:
//   voicevox_core/c_api/lib/libvoicevox_core.dylib
//   voicevox_core/onnxruntime/lib/libvoicevox_onnxruntime.<ver>.dylib
//   voicevox_core/dict/open_jtalk_dic_utf_8-1.11/
//   voicevox_core/models/vvms/1.vvm
bool looks_like_root(const fs::path& dir) {
    std::error_code ec;
    return !dir.empty() && fs::is_directory(dir / "models" / "vvms", ec);
}

// Discovers the voicevox_core/ tree: an explicit override, then the repo
// checkout when running from a build tree, then the app bundle, then the data
// dir. Empty if none has it.
fs::path find_root(const fs::path& configured) {
    if (looks_like_root(configured)) return configured;

    if (const char* env = std::getenv("MIMI_VOICEVOX_DIR"); env != nullptr && *env != '\0') {
        if (looks_like_root(env)) return env;
    }

    std::vector<fs::path> candidates;
    // Walk up from the executable looking for <repo>/voicevox_core.
    fs::path dir = paths::exe_dir();
    for (int depth = 0; depth < 6 && !dir.empty() && dir != dir.root_path(); ++depth) {
        candidates.push_back(dir / "voicevox_core");
        dir = dir.parent_path();
    }
    // Inside a bundle: Mimi.app/Contents/Resources/voicevox_core.
    candidates.push_back(paths::exe_dir() / ".." / "Resources" / "voicevox_core");
    candidates.push_back(paths::data_dir() / "voicevox_core");

    for (const auto& c : candidates) {
        if (looks_like_root(c)) return fs::weakly_canonical(c);
    }
    return {};
}

// First file under `dir` whose name matches the prefix/suffix, or empty.
fs::path first_matching(const fs::path& dir, std::string_view prefix, std::string_view suffix) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return entry.path();
        }
    }
    return {};
}

}  // namespace

#ifdef MIMI_HAS_VOICEVOX

using json = nlohmann::json;

namespace {

const char* code_message(VoicevoxResultCode code) {
    return voicevox_error_result_to_message(code);
}

}  // namespace

struct Voicevox::Impl {
    fs::path root;
    std::mutex mutex;
    bool initialized = false;

    const VoicevoxOnnxruntime* onnxruntime = nullptr;
    OpenJtalkRc* open_jtalk = nullptr;
    VoicevoxSynthesizer* synthesizer = nullptr;

    ~Impl() {
        if (synthesizer != nullptr) voicevox_synthesizer_delete(synthesizer);
        if (open_jtalk != nullptr) voicevox_open_jtalk_rc_delete(open_jtalk);
        // The ONNX Runtime is a process-global singleton owned by the library;
        // there is no matching free and it must outlive every synthesiser.
    }

    bool init(const Voicevox::Config& config) {
        std::lock_guard lock(mutex);
        if (initialized) return true;

        if (root.empty()) {
            log::info(kTag, "{}", Voicevox::install_hint());
            return false;
        }

        const fs::path ort = first_matching(root / "onnxruntime" / "lib",
                                            "libvoicevox_onnxruntime", ".dylib");
        const fs::path dict = first_matching(root / "dict", "open_jtalk_dic", "");
        const fs::path vvm = root / "models" / "vvms" / config.model_file;

        std::error_code ec;
        if (ort.empty() || dict.empty() || !fs::exists(vvm, ec)) {
            log::warn(kTag, "incomplete runtime under {} (ort={}, dict={}, vvm={})",
                      root.string(), !ort.empty(), !dict.empty(), fs::exists(vvm, ec));
            return false;
        }

        VoicevoxLoadOnnxruntimeOptions oopt = voicevox_make_default_load_onnxruntime_options();
        const std::string ort_str = ort.string();
        oopt.filename = ort_str.c_str();
        if (auto r = voicevox_onnxruntime_load_once(oopt, &onnxruntime); r != VOICEVOX_RESULT_OK) {
            log::warn(kTag, "could not load ONNX Runtime: {}", code_message(r));
            return false;
        }

        const std::string dict_str = dict.string();
        if (auto r = voicevox_open_jtalk_rc_new(dict_str.c_str(), &open_jtalk);
            r != VOICEVOX_RESULT_OK) {
            log::warn(kTag, "could not load the Open JTalk dictionary: {}", code_message(r));
            return false;
        }

        VoicevoxInitializeOptions iopt = voicevox_make_default_initialize_options();
        iopt.acceleration_mode = VOICEVOX_ACCELERATION_MODE_CPU;
        if (auto r = voicevox_synthesizer_new(onnxruntime, open_jtalk, iopt, &synthesizer);
            r != VOICEVOX_RESULT_OK) {
            log::warn(kTag, "could not create the synthesiser: {}", code_message(r));
            return false;
        }

        VoicevoxVoiceModelFile* model = nullptr;
        const std::string vvm_str = vvm.string();
        if (auto r = voicevox_voice_model_file_open(vvm_str.c_str(), &model);
            r != VOICEVOX_RESULT_OK) {
            log::warn(kTag, "could not open {}: {}", vvm_str, code_message(r));
            return false;
        }
        const auto load = voicevox_synthesizer_load_voice_model(synthesizer, model);
        voicevox_voice_model_file_delete(model);
        if (load != VOICEVOX_RESULT_OK) {
            log::warn(kTag, "could not load the voice model: {}", code_message(load));
            return false;
        }

        initialized = true;
        log::info(kTag, "embedded engine ready (style {} from {})", config.style_id,
                  vvm.filename().string());
        return true;
    }
};

Voicevox::Voicevox(Config config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {
    impl_->root = find_root(config_.root);
}

Voicevox::~Voicevox() = default;

bool Voicevox::available() const { return impl_->initialized; }

bool Voicevox::ensure_running(std::chrono::seconds) { return impl_->init(config_); }

std::vector<VoicevoxStyle> Voicevox::styles() const {
    std::vector<VoicevoxStyle> out;
    if (!impl_->initialized) return out;
    char* metas = voicevox_synthesizer_create_metas_json(impl_->synthesizer);
    if (metas == nullptr) return out;
    try {
        for (const auto& speaker : json::parse(metas)) {
            const std::string name = speaker.value("name", "");
            for (const auto& style : speaker.value("styles", json::array())) {
                out.push_back({name, style.value("name", ""), style.value("id", 0)});
            }
        }
    } catch (const std::exception& e) {
        log::warn(kTag, "could not read the style list: {}", e.what());
    }
    voicevox_json_free(metas);
    return out;
}

namespace {

// Walks the accent phrases of an audio query and flattens them into one
// timeline of moras.
//
// The plan is in *unscaled* seconds: speedScale is applied by the synthesiser
// afterwards, so every duration here has to be divided by it or the mouth
// drifts further behind the further into a sentence she gets. Unvoiced vowels
// come back upper-case (A/I/U/E/O), and ん, っ and pauses come back as N, cl
// and pau -- none of which is an open mouth shape.
std::vector<Mora> timeline_from(const json& plan) {
    // json::value() returns the fallback for a *missing* key but still throws
    // on one that is present and null -- and VOICEVOX writes consonant and
    // consonant_length as null for every mora that has no consonant (あ, い,
    // う…), which is most sentences. Reading them with value() threw inside
    // synthesize()'s try block and took the whole clip down with it, falling
    // silently back to the system voice.
    const auto number_or = [](const json& obj, const char* key, double fallback) {
        const auto it = obj.find(key);
        if (it == obj.end() || it->is_null()) return fallback;
        return it->get<double>();
    };

    std::vector<Mora> moras;
    const double raw_speed = number_or(plan, "speedScale", 1.0);
    const double speed = raw_speed > 0 ? raw_speed : 1.0;
    double t = number_or(plan, "prePhonemeLength", 0.0) / speed;

    const auto push = [&](const json& mora) {
        const double consonant = number_or(mora, "consonant_length", 0.0) / speed;
        const double vowel_len = number_or(mora, "vowel_length", 0.0) / speed;
        // The consonant is the closed part of the mora; the mouth opens for
        // the vowel that follows it.
        t += consonant;
        const auto vowel_it = mora.find("vowel");
        const std::string vowel =
            (vowel_it != mora.end() && vowel_it->is_string()) ? vowel_it->get<std::string>()
                                                              : std::string{};
        char shape = '\0';
        if (vowel.size() == 1) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(vowel[0])));
            if (c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o') shape = c;
        }
        moras.push_back({t, vowel_len, shape});
        t += vowel_len;
    };

    for (const auto& phrase : plan.value("accent_phrases", json::array())) {
        for (const auto& mora : phrase.value("moras", json::array())) push(mora);
        // A phrase can end in a pause, which is a mora with no consonant and a
        // silent vowel. It still advances time, so it cannot be skipped.
        if (const auto pause = phrase.find("pause_mora");
            pause != phrase.end() && !pause->is_null()) {
            push(*pause);
        }
    }
    return moras;
}

}  // namespace

std::vector<std::uint8_t> Voicevox::synthesize(const std::string& text) const {
    std::vector<Mora> ignored;
    return synthesize(text, ignored);
}

std::vector<std::uint8_t> Voicevox::synthesize(const std::string& text,
                                               std::vector<Mora>& timeline) const {
    std::vector<std::uint8_t> audio;
    if (text.empty() || !impl_->initialized) return audio;

    const auto style = static_cast<std::uint32_t>(config_.style_id);

    // Two steps, mirroring the HTTP engine: create_audio_query returns an
    // editable prosody plan and synthesis renders it. The intermediate is where
    // speed and intonation are set -- flat intonation is most of what makes
    // synthetic speech sound synthetic.
    char* query = nullptr;
    if (auto r = voicevox_synthesizer_create_audio_query(impl_->synthesizer, text.c_str(), style,
                                                         &query);
        r != VOICEVOX_RESULT_OK) {
        log::warn(kTag, "audio_query failed: {}", code_message(r));
        return audio;
    }

    std::string plan_str;
    try {
        json plan = json::parse(query);
        plan["speedScale"] = config_.speed;
        plan["pitchScale"] = config_.pitch;
        plan["intonationScale"] = config_.intonation;
        plan["outputStereo"] = false;
        plan_str = plan.dump();
        // After the edits, so the timeline is scaled by the speed actually
        // rendered rather than the one the query came back with.
        timeline = timeline_from(plan);
    } catch (const std::exception& e) {
        log::warn(kTag, "could not edit audio_query: {}", e.what());
        voicevox_json_free(query);
        return audio;
    }
    voicevox_json_free(query);

    VoicevoxSynthesisOptions sopt = voicevox_make_default_synthesis_options();
    std::uint8_t* wav = nullptr;
    std::uintptr_t wav_len = 0;
    if (auto r = voicevox_synthesizer_synthesis(impl_->synthesizer, plan_str.c_str(), style, sopt,
                                                &wav_len, &wav);
        r != VOICEVOX_RESULT_OK) {
        log::warn(kTag, "synthesis failed: {}", code_message(r));
        return audio;
    }

    audio.assign(wav, wav + wav_len);
    voicevox_wav_free(wav);
    log::debug(kTag, "{} chars -> {} KB of audio", text.size(), audio.size() / 1024);
    return audio;
}

std::string Voicevox::install_hint() {
    return "VOICEVOX CORE is not installed. Run scripts/fetch_voicevox.sh to download the "
           "engine and the 冥鳴ひまり voice model.";
}

#else  // MIMI_HAS_VOICEVOX not defined -- built without the core library.

struct Voicevox::Impl {
    fs::path root;
    bool initialized = false;
};

Voicevox::Voicevox(Config config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}
Voicevox::~Voicevox() = default;
bool Voicevox::available() const { return false; }
bool Voicevox::ensure_running(std::chrono::seconds) {
    log::info(kTag, "built without VOICEVOX CORE; using the system voice");
    return false;
}
std::vector<VoicevoxStyle> Voicevox::styles() const { return {}; }
std::vector<std::uint8_t> Voicevox::synthesize(const std::string&) const { return {}; }
std::string Voicevox::install_hint() {
    return "This build does not include VOICEVOX CORE.";
}

#endif  // MIMI_HAS_VOICEVOX

}  // namespace mimi::voice
