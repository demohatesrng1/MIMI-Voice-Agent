#include "voice/tts.hpp"

#include "core/log.hpp"

#import <AVFoundation/AVFoundation.h>

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

    std::mutex mutex;
    std::function<void(bool)> on_finished;

    void finish(bool completed) {
        std::function<void(bool)> callback;
        {
            std::lock_guard lock(mutex);
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
    if (impl) impl->finish(true);
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer
    didCancelSpeechUtterance:(AVSpeechUtterance*)utterance {
    auto* impl = static_cast<mimi::voice::Speaker::Impl*>(self.owner);
    if (impl) impl->finish(false);
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

void Speaker::speak(const std::string& text, std::function<void(bool)> on_finished) {
    if (text.empty()) {
        if (on_finished) on_finished(true);
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->on_finished = std::move(on_finished);
    }

    @autoreleasepool {
        AVSpeechUtterance* utterance = [AVSpeechUtterance
            speechUtteranceWithString:[NSString stringWithUTF8String:text.c_str()]];
        utterance.voice = impl_->voice;
        utterance.rate = config_.rate;
        utterance.pitchMultiplier = config_.pitch;
        utterance.volume = config_.volume;
        [impl_->synth speakUtterance:utterance];
    }
}

void Speaker::stop() {
    @autoreleasepool {
        if (impl_ && impl_->synth != nil && impl_->synth.isSpeaking) {
            // Immediate, not AVSpeechBoundaryWord: barge-in has to feel like an
            // interruption, not like waiting for her to finish the word.
            [impl_->synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        }
    }
}

bool Speaker::speaking() const {
    return impl_ != nullptr && impl_->synth != nil && impl_->synth.isSpeaking;
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
