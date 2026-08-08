#pragma once

#include <QColor>
#include <QEasingCurve>

// The design system: a dark stage, and watermelon.
//
// She is the subject now, not a status light in a page of panels. That changes
// what the surface around her is for: it is a stage, so it drops to near-black
// and gets out of the way, and the chrome becomes glass floating on top of it
// rather than boxes carved into it.
//
// Two accents, from the fruit: watermelon (#ff4d6d) carries everything active,
// and rind green means one thing only -- she is hearing you right now.
namespace mimi::ui::theme {

// The stage. Near-black and very slightly cool, because a warm character on a
// dead-neutral ground reads as a cutout: the small hue separation is what makes
// her sit *in* the picture.
//
// The previous ramp sat well off black (#171718) so hairlines would read on a
// page of panels. That was right for a page of panels. With a lit figure as the
// subject, the same ramp is a grey box around her -- so the ground drops and
// the panels come back up as glass instead.
inline const QColor kVoid{0x0a, 0x0a, 0x12};      // the cosmic void. static, always
inline const QColor kLayer0{0x0a, 0x0a, 0x12};    // window surface -- the same ground
inline const QColor kLayer1{0x1a, 0x1b, 0x1e};    // panels
inline const QColor kLayer2{0x21, 0x23, 0x28};    // raised surfaces, cards

// Structure comes from hairlines, not from glow. Without a visible border a
// dark UI has to signal edges with light, which is where the haze came from.
inline const QColor kLine{0x2a, 0x2c, 0x31};      // divider, card edge
inline const QColor kLineSoft{0x20, 0x22, 0x27};  // the quietest separation

// Glass. What panels are made of on the stage: a lift of white at very low
// alpha, with a brighter hairline on top. Painted, not blurred -- Qt Widgets
// has no backdrop filter, and at these alphas over a near-black ground the
// difference is not one you can see.
inline const QColor kGlass{0xff, 0xff, 0xff, 0x0d};       // ~5%
inline const QColor kGlassRaised{0xff, 0xff, 0xff, 0x14};  // ~8%
inline const QColor kGlassEdge{0xff, 0xff, 0xff, 0x1f};    // ~12%
// One radius, everywhere. Cards, fields, pills.
inline constexpr int kRadius = 22;
inline constexpr int kRadiusSmall = 14;

// Type, three steps -- but all three are readable.
//
// The lower two used to be genuine greys (#949499, #6c6c72), and everything
// written in them read as disabled: hints, captions and secondary copy are
// still meant to be *read*, and a grey that only whispers means the reader
// skips them. These sit much closer to white, with just enough of a step left
// to keep a hierarchy.
inline const QColor kInk{0xf2, 0xf2, 0xf4};       // primary
inline const QColor kDim{0xcf, 0xcf, 0xd4};       // secondary -- still reads as white
inline const QColor kFaint{0xa6, 0xa6, 0xad};     // tertiary -- quiet, not faint

// The flesh. The active thing, a focused field, the thing being talked about,
// and her own light. Not decoration.
inline const QColor kAccent{0xff, 0x4d, 0x6d};      // watermelon -- the active thing
inline const QColor kAccentSoft{0xff, 0x8f, 0xa3};  // on-accent detail
inline const QColor kAccentDeep{0xc2, 0x18, 0x3c};  // pressed, filled buttons
inline const QColor kAccentGlow{0xff, 0x6b, 0x85};  // ambient light

// The rind. One meaning only: she is hearing you right now. Nothing else in
// the app is allowed to be green, which is what lets it be read at a glance
// from across a desk.
inline const QColor kLive{0x3d, 0xdc, 0x84};   // rind green -- she is hearing you
inline const QColor kLiveDeep{0x1f, 0x8a, 0x50};

// Reserved. Never decorative -- only for things that genuinely went wrong.
// Deliberately pulled toward orange-red: the accent is now a red-pink, and an
// error colour that reads as "the accent, slightly angrier" is not a warning.
inline const QColor kWarn{0xf5, 0xa5, 0x24};
inline const QColor kError{0xe5, 0x48, 0x4d};

// Motion: one duration, one curve, everywhere. Nothing snaps.
inline constexpr int kMotionMs = 180;
inline const QEasingCurve kMotion{QEasingCurve::OutCubic};

}  // namespace mimi::ui::theme
