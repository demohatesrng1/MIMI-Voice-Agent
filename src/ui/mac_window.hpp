#pragma once

class QWidget;

namespace mimi::ui {

// Native window treatment that Qt does not expose.
//
// Qt can make a window frameless, but that throws away everything AppKit does
// for free: the traffic lights, snapping, full-screen, the window menu. What
// VS Code, Arc and Linear actually do on macOS is keep the real window and make
// its title bar transparent with a full-size content view, so the application
// draws across the entire surface -- including behind the title bar -- while
// the system still owns the chrome. An empty unified-compact toolbar is what
// vertically centres the traffic lights in that taller strip; without it they
// hug the top-left corner.
//
// Call after the widget has a native handle, i.e. after winId(). Idempotent,
// so it can be re-applied on every show.
void adopt_native_titlebar(QWidget* widget);

// Adds a native NSVisualEffectView behind the window's content so that macOS
// vibrancy (blur + saturation of whatever is behind the window) shows through
// wherever the stylesheet paints with an alpha colour. The widget needs
// Qt::WA_TranslucentBackground, set before its native window is created.
void add_window_vibrancy(QWidget* widget);

// Horizontal space to leave clear at the top left for the traffic lights, in
// logical points, measured from this widget's own window. Hardcoding this is
// how title bars end up with buttons sitting on top of a label.
int traffic_light_inset(QWidget* widget);

// Runs the system double-click-on-titlebar action (zoom or minimise,
// following the user's Desk & Dock preference) on the widget's window.
void titlebar_double_clicked(QWidget* widget);

}  // namespace mimi::ui
