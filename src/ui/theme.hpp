#pragma once

#include <QColor>

// The palette from static/index.html, so the Qt app and the old web UI are
// recognisably the same product. Kept in C++ as well as QSS because the voice
// orb is custom-painted and stylesheets cannot reach it.
namespace mimi::ui::theme {

inline const QColor kBg{0x0b, 0x0e, 0x1a};
inline const QColor kBg2{0x11, 0x15, 0x27};
inline const QColor kPanel{0x16, 0x1b, 0x30};
inline const QColor kPanel2{0x1b, 0x21, 0x40};
inline const QColor kLine{0x26, 0x2d, 0x52};
inline const QColor kInk{0xe8, 0xea, 0xf6};
inline const QColor kDim{0x88, 0x90, 0xb5};
inline const QColor kViolet{0x8b, 0x7c, 0xf8};
inline const QColor kCyan{0x4d, 0xd8, 0xe6};
inline const QColor kGreen{0x58, 0xe2, 0x8b};
inline const QColor kAmber{0xf5, 0xc4, 0x5e};

}  // namespace mimi::ui::theme
