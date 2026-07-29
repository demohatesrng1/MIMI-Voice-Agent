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
// the system still owns the chrome.
//
// Call after the widget has a window handle, i.e. after show().
void adopt_native_titlebar(QWidget* widget);

// Horizontal space to leave clear at the top left for the traffic lights, in
// logical points. Hardcoding this is how title bars end up with buttons sitting
// on top of a label.
int traffic_light_inset();

// Lets the window be dragged by an arbitrary widget, since with a hidden title
// bar there is no longer a bar to grab.
void make_draggable_background(QWidget* widget);

}  // namespace mimi::ui
