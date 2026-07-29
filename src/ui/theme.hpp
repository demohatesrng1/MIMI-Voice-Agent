#pragma once

#include <QColor>

// Black and pink, held to strictly.
//
// State is carried by the *intensity* of one hue rather than by a different
// colour per state. A palette that answers every question with a new colour
// stops meaning anything -- and the moment an app has five accent hues it
// reads as a hobby project regardless of how carefully each one was picked.
namespace mimi::ui::theme {

// Surfaces, darkest to lightest.
inline const QColor kBg{0x08, 0x08, 0x0c};        // window
inline const QColor kSurface{0x10, 0x10, 0x17};   // panels, rail, chrome
inline const QColor kSurface2{0x18, 0x18, 0x22};  // cards, raised
inline const QColor kLine{0x24, 0x24, 0x31};      // hairlines

// Type.
inline const QColor kInk{0xf3, 0xf3, 0xf8};       // primary
inline const QColor kDim{0x87, 0x87, 0x9c};       // secondary
inline const QColor kFaint{0x4d, 0x4d, 0x60};     // tertiary, timestamps

// The accent, in four weights.
inline const QColor kPink{0xff, 0x2d, 0x87};      // primary
inline const QColor kPinkSoft{0xff, 0x7a, 0xb4};  // highlights, hover
inline const QColor kPinkDeep{0x9e, 0x18, 0x53};  // resting, dim states
inline const QColor kPinkGlow{0xff, 0x4d, 0x9c};  // glow

// Reserved. Never decorative -- only for things that genuinely went wrong.
inline const QColor kWarn{0xf5, 0xa5, 0x24};
inline const QColor kError{0xff, 0x45, 0x60};

}  // namespace mimi::ui::theme
