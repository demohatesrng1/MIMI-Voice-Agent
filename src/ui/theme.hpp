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

// Graphite ramp, darkest (the void behind everything) to lightest (raised
// glass). Each step is one layer of elevation, not a different material.
inline const QColor kVoid{0x04, 0x05, 0x08};      // ambient base
inline const QColor kLayer0{0x0a, 0x0c, 0x11};    // window surface
inline const QColor kLayer1{0x10, 0x13, 0x1a};    // panels
inline const QColor kLayer2{0x16, 0x1a, 0x23};    // raised glass, cards

// Type, three steps only.
inline const QColor kInk{0xf2, 0xf5, 0xf9};       // primary
inline const QColor kDim{0x8a, 0x94, 0xa6};       // secondary
inline const QColor kFaint{0x51, 0x5b, 0x6e};     // tertiary

// The one accent.
inline const QColor kAccent{0x3d, 0x8b, 0xff};      // electric blue
inline const QColor kAccentSoft{0x8f, 0xc0, 0xff};  // highlights
inline const QColor kAccentDeep{0x1d, 0x4f, 0x94};  // resting states
inline const QColor kAccentGlow{0x4d, 0x9f, 0xff};  // ambient light

// Reserved. Never decorative -- only for things that genuinely went wrong.
inline const QColor kWarn{0xf5, 0xa5, 0x24};
inline const QColor kError{0xff, 0x45, 0x60};

// Motion: one duration, one curve, everywhere. Nothing snaps.
inline constexpr int kMotionMs = 180;
inline const QEasingCurve kMotion{QEasingCurve::OutCubic};

}  // namespace mimi::ui::theme
