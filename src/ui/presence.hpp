#pragma once

#include "ui/theme.hpp"
#include "voice/listener.hpp"

#include <QColor>
#include <QString>

// Presence: what Mimi is, in the language the interface speaks.
//
// A chatbot is "Online". A presence is *doing something* -- observing,
// building context, remembering. This enum is the single source of truth the
// whole UI reads from: the status capsule, the orb, and the living background
// all derive their behaviour from it, so the app never contradicts itself
// about what she is up to.
//
// It is a superset of voice::State. The five voice states map straight in;
// Observing and Remembering exist only here, driven by what the app is doing
// rather than by the microphone -- Observing is her resting, aware state, and
// Remembering is the beat after an answer where the exchange is written to the
// journal. Naming those moments is most of what turns a status light into a
// presence.
namespace mimi::ui {

enum class Presence {
    Observing,    // resting, aware -- "Observing workspace"
    Listening,    // hearing you -- "Listening"
    Thinking,     // transcribing and reasoning -- "Building context"
    Speaking,     // answer playing -- "Responding"
    Remembering,  // persisting the exchange -- "Updating memory"
    Muted,        // switched off -- "Muted"
};

// The five voice states, promoted into the richer vocabulary. Remembering has
// no voice state to come from -- the app raises it directly around the journal
// write -- so it is not produced here.
inline Presence presence_for(voice::State state) noexcept {
    switch (state) {
        case voice::State::Idle:      return Presence::Observing;
        case voice::State::Listening: return Presence::Listening;
        case voice::State::Thinking:  return Presence::Thinking;
        case voice::State::Speaking:  return Presence::Speaking;
        case voice::State::Paused:    return Presence::Muted;
    }
    return Presence::Observing;
}

// The phrase for the status capsule -- the "Online" replacement. Two words at
// most: long enough to read as intent, short enough to sit in a pill.
inline QString presence_phrase(Presence p) {
    switch (p) {
        case Presence::Observing:   return QStringLiteral("Observing workspace");
        case Presence::Listening:   return QStringLiteral("Listening");
        case Presence::Thinking:    return QStringLiteral("Building context");
        case Presence::Speaking:    return QStringLiteral("Responding");
        case Presence::Remembering: return QStringLiteral("Updating memory");
        case Presence::Muted:       return QStringLiteral("Muted");
    }
    return QStringLiteral("Observing workspace");
}

// The same, spaced for the hero label. Kept to one word where the phrase has
// two, so the tracked-out caps do not wrap.
inline QString presence_headline(Presence p) {
    switch (p) {
        case Presence::Observing:   return QStringLiteral("OBSERVING");
        case Presence::Listening:   return QStringLiteral("LISTENING");
        case Presence::Thinking:    return QStringLiteral("BUILDING CONTEXT");
        case Presence::Speaking:    return QStringLiteral("RESPONDING");
        case Presence::Remembering: return QStringLiteral("REMEMBERING");
        case Presence::Muted:       return QStringLiteral("MUTED");
    }
    return QStringLiteral("OBSERVING");
}

// One hue at several weights -- resting is deep and quiet, thinking lifts,
// responding is the full accent, remembering settles back toward the soft
// highlight.
//
// Listening is the single exception, and the only green in the app: the one
// state you need to read from across the room is whether she is hearing you,
// and a weight of the same pink cannot carry that the way a different hue can.
inline QColor presence_accent(Presence p) {
    switch (p) {
        case Presence::Observing:   return theme::kAccentDeep;
        case Presence::Listening:   return theme::kLive;
        case Presence::Thinking:    return theme::kAccentSoft;
        case Presence::Speaking:    return theme::kAccent;
        case Presence::Remembering: return theme::kAccentGlow;
        case Presence::Muted:       return theme::kFaint;
    }
    return theme::kAccentDeep;
}

}  // namespace mimi::ui
