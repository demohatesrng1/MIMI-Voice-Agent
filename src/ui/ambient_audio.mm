#include "ui/ambient_audio.hpp"

#import <AppKit/AppKit.h>

namespace mimi::ui {
namespace {
void play(NSString* name) {
    NSSound* s = [NSSound soundNamed:name];
    [s play];
}
}  // namespace

void AmbientAudio::cue(Presence presence) {
    if (presence == last_) return;
    last_ = presence;
    switch (presence) {
        case Presence::Listening:   play(@"Tink");   break;  // a soft attention tick
        case Presence::Thinking:    play(@"Morse");  break;
        case Presence::Speaking:    play(@"Pop");    break;
        case Presence::Remembering: play(@"Glass");  break;
        case Presence::Observing:
        case Presence::Muted:       break;  // silence at rest
    }
}

}  // namespace mimi::ui
