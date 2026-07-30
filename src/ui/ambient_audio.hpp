#pragma once

#include "ui/presence.hpp"

namespace mimi::ui {

// Ambient Audio: tiny, Apple-quality sounds as she changes state -- a soft cue
// for listening, thinking, responding, remembering. Plays macOS system sounds
// so there are no assets to ship. Implemented in Objective-C++.
class AmbientAudio {
public:
    void cue(Presence presence);

private:
    Presence last_ = Presence::Observing;
};

}  // namespace mimi::ui
