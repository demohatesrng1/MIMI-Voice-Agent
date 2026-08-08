#pragma once

#include <QRect>

class QWidget;

namespace mimi::ui {

// Native window treatment that Qt does not expose.
//
// Qt can make a window frameless, but that throws away everything AppKit does
// for free: the traffic lights, snapping, full-screen, the window menu. What
// VS Code, Arc and Linear actually do on macOS is keep the real window and make
// its title bar transparent with a full-size content view, so the application
// draws across the entire surface -- including behind the title bar -- while
// the system still owns the chrome.
//
// There is deliberately NO NSToolbar here. An empty unified-compact toolbar
// does centre the traffic lights in a taller strip, but it also gives AppKit a
// toolbar area to reserve, and the content view is laid out *below* it even
// with a full-size content view -- which is the "two stacked bars" defect: a
// 40pt band holding the traffic lights, and the app's own title bar starting
// underneath it. With no toolbar the content view fills the frame from y=0 and
// the lights float over our own bar, where traffic_light_rect() finds them.
//
// Call after the widget has a native handle, i.e. after winId(). Idempotent,
// so it can be re-applied on every show.
void adopt_native_titlebar(QWidget* widget);

// Adds a native NSVisualEffectView behind the window's content so that macOS
// vibrancy (blur + saturation of whatever is behind the window) shows through
// wherever the stylesheet paints with an alpha colour. The widget needs
// Qt::WA_TranslucentBackground, set before its native window is created.
void add_window_vibrancy(QWidget* widget);

// Where the traffic lights actually sit, in logical points, in the widget's
// own top-left-origin coordinate space -- measured from this window's real
// buttons rather than assumed.
//
// Both axes matter. The width is the space the title bar has to keep clear on
// the left, and the vertical centre is what the wordmark and the right-hand
// buttons align to; a bar that centres its own contents instead lands them a
// few points below the lights, which is the misalignment that reads as two
// separate bars even once they are in the same strip.
//
// Returns a null QRect when there is no window, or in full screen, where
// AppKit takes the lights away entirely and the bar should reclaim the space.
QRect traffic_light_rect(QWidget* widget);

// Keeps a borderless overlay (the orb) visible no matter which application is
// in front.
//
// Qt::Tool maps to an NSPanel that AppKit hides whenever the application
// deactivates, which is why the orb vanishes on a Cmd-Tab. Making the panel
// non-activating and stationary, giving it a floating window level and joining
// it to every Space is what pins it: it then survives app switches, Space
// changes and Mission Control without ever taking focus.
//
// Call after the widget has a native handle, i.e. after winId() or show().
void pin_overlay_window(QWidget* widget);

// Runs the system double-click-on-titlebar action (zoom or minimise,
// following the user's Desk & Dock preference) on the widget's window.
void titlebar_double_clicked(QWidget* widget);

}  // namespace mimi::ui
