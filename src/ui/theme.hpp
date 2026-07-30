#pragma once

#include <QColor>
#include <QEasingCurve>

// The design system: graphite and one electric blue.
//
// Depth comes from layering -- ambient light, glass, shadow -- never from
// outlines. The accent is spent sparingly: focus, state, and the assistant
// herself. Everything else is a step on one graphite ramp, which is what
// makes the single blue read as intelligence rather than decoration.
namespace mimi::ui::theme {

// Neutral grey ramp. Not black, and not tinted.
//
// The previous ramp was near-black (#040508) with a blue cast, paired with a
// blue accent -- so the "neutral" greys and the highlight were the same hue and
// nothing separated them. Everything read as one wash.
//
// This is the range the editors and documents people use all day actually sit
// in: lifted well off black, so borders and elevation are visible at all, and
// hue-free, so the one accent has somewhere to stand out from.
inline const QColor kVoid{0x17, 0x17, 0x18};      // ambient base
inline const QColor kLayer0{0x1c, 0x1c, 0x1e};    // window surface
inline const QColor kLayer1{0x23, 0x23, 0x25};    // panels
inline const QColor kLayer2{0x2b, 0x2b, 0x2e};    // raised surfaces, cards

// Structure comes from hairlines, not from glow. Without a visible border a
// dark UI has to signal edges with light, which is where the haze came from.
inline const QColor kLine{0x35, 0x35, 0x38};      // divider, card edge
inline const QColor kLineSoft{0x2a, 0x2a, 0x2d};  // the quietest separation

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

// One accent, spent sparingly -- the active tab, a focused field, the thing
// being talked about. Not decoration.
inline const QColor kAccent{0x3b, 0x82, 0xf6};      // the active thing
inline const QColor kAccentSoft{0x93, 0xb8, 0xf8};  // on-accent detail
inline const QColor kAccentDeep{0x1e, 0x4e, 0xa8};  // pressed, filled buttons
inline const QColor kAccentGlow{0x60, 0x9c, 0xf8};  // ambient light

// Reserved. Never decorative -- only for things that genuinely went wrong.
inline const QColor kWarn{0xf5, 0xa5, 0x24};
inline const QColor kError{0xff, 0x45, 0x60};

// Motion: one duration, one curve, everywhere. Nothing snaps.
inline constexpr int kMotionMs = 180;
inline const QEasingCurve kMotion{QEasingCurve::OutCubic};

}  // namespace mimi::ui::theme
