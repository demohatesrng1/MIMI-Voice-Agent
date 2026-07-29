#pragma once

#include <QIcon>
#include <QPainterPath>

class QColor;

namespace mimi::ui::icons {

// Vector icons, drawn rather than typed.
//
// The emoji and box-drawing characters this replaces were the single biggest
// reason the app looked unfinished: 🎙 renders as a full-colour Apple glyph
// that matches nothing else on screen, and ◉ ≡ ✦ ⚙ are different weights from
// different type designers. These are stroked paths on a 24x24 grid with one
// consistent weight, so a row of them reads as a set.
enum class Glyph {
    Mic,
    Home,
    Activity,
    Skills,
    Settings,
    Send,
    Power,
    Clock,
    Battery,
    Display,
    Camera,
    VolumeDown,
    Lock,
};

// Rendered at the requested colour, cached per (glyph, colour, size).
QIcon icon(Glyph glyph, const QColor& colour, int size = 20);

// The raw path on a 24x24 grid, for widgets that paint their own content.
QPainterPath path(Glyph glyph);

}  // namespace mimi::ui::icons
