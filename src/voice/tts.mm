#include "voice/tts.hpp"

#include "core/log.hpp"

#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

#include "core/paths.hpp"

#include <cstdio>
#include <fstream>
#include <mutex>
#include <string_view>

namespace {
constexpr std::string_view kTag = "tts";
}

// Bridges AVSpeechSynthesizer's completion callbacks back into C++.
@interface MimiSpeechDelegate : NSObject <AVSpeechSynthesizerDelegate>
@property(nonatomic, assign) void* owner;
@end

namespace mimi::voice {

struct Speaker::Impl {
    AVSpeechSynthesizer* synth = nil;
    MimiSpeechDelegate* delegate = nil;
    AVSpeechSynthesisVoice* voice = nil;

    std::unique_ptr<Voicevox> voicevox;
    bool voicevox_ready = false;
    // afplay pid for the VOICEVOX path; the system synthesiser has its own stop.
    std::atomic<pid_t> player{0};

    // Each speak() claims the next generation. A completion only counts if it is
    // still the current one, so a clip that was replaced (or killed) by a newer
    // utterance can never fire the newer utterance's callback. Without this,
    // overlapping replies -- a reminder firing mid-answer, a typed question over
    // her voice -- crossed their callbacks and cut each other to silence.
    std::atomic<std::uint64_t> generation{0};
    // The generation and utterance currently owning the system synthesiser, so
    // its delegate can tell a live completion from a stale one.
    std::atomic<std::uint64_t> av_generation{0};
    AVSpeechUtterance* av_current = nil;

    std::mutex mutex;
    std::uint64_t cb_generation = 0;
    std::function<void(bool)> on_finished;

    void finish(bool completed, std::uint64_t gen) {
        std::function<void(bool)> callback;
        {
            std::lock_guard lock(mutex);
            if (gen != cb_generation) return;  // a newer utterance has taken over
            callback.swap(on_finished);
        }
        if (callback) callback(completed);
    }
};

}  // namespace mimi::voice

@implementation MimiSpeechDelegate

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer
    didFinishSpeechUtterance:(AVSpeechUtterance*)utterance {
    auto* impl = static_cast<mimi::voice::Speaker::Impl*>(self.owner);
    // Ignore the tail of an utterance a newer speak() has already replaced.
    if (impl && utterance == impl->av_current) impl->finish(true, impl->av_generation.load());
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer
    didCancelSpeechUtterance:(AVSpeechUtterance*)utterance {
    auto* impl = static_cast<mimi::voice::Speaker::Impl*>(self.owner);
    if (impl && utterance == impl->av_current) impl->finish(false, impl->av_generation.load());
}

@end

namespace mimi::voice {
namespace {

VoiceInfo describe(AVSpeechSynthesisVoice* voice) {
    VoiceInfo info;
    info.identifier = voice.identifier.UTF8String;
    info.name = voice.name.UTF8String;
    info.language = voice.language.UTF8String;
    info.enhanced = voice.quality != AVSpeechSynthesisVoiceQualityDefault;
    return info;
}

AVSpeechSynthesisVoice* find_voice(const std::string& name, const std::string& language) {
    if (!name.empty()) {
        NSString* wanted = [NSString stringWithUTF8String:name.c_str()];
        AVSpeechSynthesisVoice* best = nil;
        for (AVSpeechSynthesisVoice* v in [AVSpeechSynthesisVoice speechVoices]) {
            if (![v.name isEqualToString:wanted] &&
                ![v.identifier isEqualToString:wanted]) {
                continue;
            }
            // Prefer an enhanced variant of the same name when one is installed.
            if (best == nil || (v.quality > best.quality)) best = v;
        }
        if (best != nil) return best;
        log::warn(kTag, "no voice named '{}', falling back to the {} default", name, language);
    }
    if (language.empty()) return [AVSpeechSynthesisVoice voiceWithLanguage:nil];
    return [AVSpeechSynthesisVoice
        voiceWithLanguage:[NSString stringWithUTF8String:language.c_str()]];
}

}  // namespace

Speaker::Speaker(Config config) : config_(std::move(config)), impl_(std::make_unique<Impl>()) {
    @autoreleasepool {
        impl_->synth = [[AVSpeechSynthesizer alloc] init];
        impl_->delegate = [[MimiSpeechDelegate alloc] init];
        impl_->delegate.owner = impl_.get();
        impl_->synth.delegate = impl_->delegate;
        impl_->voice = find_voice(config_.voice_name, config_.language);

        if (config_.prefer_voicevox) {
            impl_->voicevox = std::make_unique<Voicevox>(config_.voicevox);
            impl_->voicevox_ready = impl_->voicevox->available();
            if (impl_->voicevox_ready) log::info(kTag, "using VOICEVOX");
        }

        if (impl_->voice != nil) {
            log::info(kTag, "speaking as {} ({}{})", impl_->voice.name.UTF8String,
                      impl_->voice.language.UTF8String,
                      impl_->voice.quality != AVSpeechSynthesisVoiceQualityDefault
                          ? ", enhanced"
                          : "");
        } else {
            log::warn(kTag, "no voice available for {}", config_.language);
        }
    }
}

Speaker::~Speaker() {
    stop();
    @autoreleasepool {
        if (impl_ && impl_->synth != nil) impl_->synth.delegate = nil;
        if (impl_) impl_->delegate.owner = nullptr;
    }
}

bool Speaker::using_voicevox() const {
    return impl_ != nullptr && impl_->voicevox_ready;
}

bool Speaker::start_voicevox() {
    if (impl_ == nullptr) return false;
    if (!impl_->voicevox) impl_->voicevox = std::make_unique<Voicevox>(config_.voicevox);
    impl_->voicevox_ready = impl_->voicevox->ensure_running();
    if (impl_->voicevox_ready) log::info(kTag, "using VOICEVOX");
    return impl_->voicevox_ready;
}

// Renders through VOICEVOX and plays the result. Returns false if anything
// fails, so speak() can fall through to the system synthesiser rather than
// going silent.
bool Speaker::speak_voicevox(const std::string& text, std::uint64_t generation) {
    if (!impl_->voicevox_ready || !impl_->voicevox) return false;

    auto wav = impl_->voicevox->synthesize(text);
    if (wav.empty()) return false;

    // afplay wants a file. Writing one costs a few ms against speech that takes
    // seconds, and it keeps playback in a process we can kill for barge-in. One
    // file per utterance: a player that has not yet been reaped can never read a
    // clip the next utterance is busy overwriting.
    const auto path =
        paths::data_subdir("cache") / ("say-" + std::to_string(generation) + ".wav");
    {
        std::ofstream file(path, std::ios::binary);
        if (!file) return false;
        file.write(reinterpret_cast<const char*>(wav.data()),
                   static_cast<std::streamsize>(wav.size()));
    }

    const std::string file_path = path.string();
    char* argv[] = {const_cast<char*>("afplay"), const_cast<char*>(file_path.c_str()),
                    nullptr};
    pid_t pid = 0;
    if (::posix_spawnp(&pid, "afplay", nullptr, nullptr, argv, environ) != 0) {
        std::remove(file_path.c_str());
        return false;
    }
    impl_->player.store(pid);

    std::thread([this, pid, generation, file_path] {
        int status = 0;
        ::waitpid(pid, &status, 0);
        // Only clear the slot if it still holds this player; a newer utterance
        // may already own it.
        pid_t expected = pid;
        impl_->player.compare_exchange_strong(expected, 0);
        std::remove(file_path.c_str());
        // Killed by stop()/barge-in reads as "not completed", so the reply is
        // not filed away as if she had finished saying it.
        const bool completed = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        impl_->finish(completed, generation);
    }).detach();
    return true;
}

void Speaker::speak(const std::string& text, std::function<void(bool)> on_finished) {
    if (text.empty()) {
        if (on_finished) on_finished(true);
        return;
    }
    const std::uint64_t generation =
        impl_->generation.fetch_add(1, std::memory_order_relaxed) + 1;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->cb_generation = generation;
        impl_->on_finished = std::move(on_finished);
    }

    // Replace whatever is playing. Claiming the generation first makes the old
    // clip's completion stale, so stopping it cannot fire this utterance's
    // callback; killing it before writing the new clip means two players never
    // overlap or read a half-written file.
    stop();

    if (speak_voicevox(text, generation)) return;

    @autoreleasepool {
        AVSpeechUtterance* utterance = [AVSpeechUtterance
            speechUtteranceWithString:[NSString stringWithUTF8String:text.c_str()]];
        utterance.voice = impl_->voice;
        utterance.rate = config_.rate;
        utterance.pitchMultiplier = config_.pitch;
        utterance.volume = config_.volume;
        impl_->av_generation.store(generation, std::memory_order_relaxed);
        impl_->av_current = utterance;
        [impl_->synth speakUtterance:utterance];
    }
}

void Speaker::stop() {
    if (const pid_t pid = impl_ ? impl_->player.exchange(0) : 0; pid > 0) {
        ::kill(pid, SIGKILL);
    }
    @autoreleasepool {
        if (impl_ && impl_->synth != nil && impl_->synth.isSpeaking) {
            // Immediate, not AVSpeechBoundaryWord: barge-in has to feel like an
            // interruption, not like waiting for her to finish the word.
            [impl_->synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        }
    }
}

bool Speaker::speaking() const {
    if (impl_ == nullptr) return false;
    if (impl_->player.load() > 0) return true;
    return impl_->synth != nil && impl_->synth.isSpeaking;
}

void Speaker::set_voice(const std::string& name_or_identifier) {
    @autoreleasepool {
        AVSpeechSynthesisVoice* voice = find_voice(name_or_identifier, config_.language);
        if (voice != nil) {
            impl_->voice = voice;
            config_.voice_name = voice.name.UTF8String;
        }
    }
}

void Speaker::set_rate(float rate) { config_.rate = rate; }

std::vector<VoiceInfo> Speaker::voices() {
    std::vector<VoiceInfo> out;
    @autoreleasepool {
        for (AVSpeechSynthesisVoice* v in [AVSpeechSynthesisVoice speechVoices]) {
            out.push_back(describe(v));
        }
    }
    return out;
}

std::vector<VoiceInfo> Speaker::voices_for(const std::string& language_prefix) {
    std::vector<VoiceInfo> out;
    for (auto& info : voices()) {
        if (info.language.rfind(language_prefix, 0) == 0) out.push_back(std::move(info));
    }
    return out;
}

}  // namespace mimi::voice
