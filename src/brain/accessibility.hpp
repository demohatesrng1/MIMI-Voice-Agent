#pragma once

#include <string>
#include <vector>

namespace mimi::brain::ax {

// Driving other applications' interfaces through the macOS Accessibility API.
//
// Everything else in tools.hpp asks an app to do something it already offers --
// `open -a`, a media key, an AppleScript verb. This is the layer that reaches
// into an arbitrary app and operates its actual controls, which is what makes
// "reply to that email" or "click the save button" possible at all.
//
// Two things make this different from the rest of the toolbox:
//
//   * It needs the user's explicit permission (System Settings > Privacy &
//     Security > Accessibility), granted per-application and revocable. Every
//     call here fails cleanly when it has not been granted.
//   * It can operate any control in any app, so it is genuinely powerful. The
//     functions are still named actions rather than a general "send these
//     keystrokes", for the same reason the rest of tools.hpp is: the caller is
//     a speech transcript, and a misheard sentence should not be able to click
//     anything it likes.

// --- permission -------------------------------------------------------------

// True when this application is trusted for Accessibility. Cheap; safe to call
// before every operation.
bool has_permission();

// Asks the system to show the permission prompt. Returns the state *now*, which
// is almost always false: macOS shows the dialog and the user then has to open
// System Settings, so the answer arrives on a later launch rather than from
// this call. Callers should say so rather than waiting on it.
bool request_permission();

// Opens System Settings on the Accessibility pane, for when the user has to be
// walked there.
void open_permission_settings();

// The other privacy gates Mimi depends on. macOS keeps each of these separate,
// and none of them reports back when the user changes it, so these are read
// live rather than cached.
enum class Access { Granted, Denied, NotAsked, Unknown };

Access microphone_access();
Access contacts_access();
Access screen_recording_access();

// Opens System Settings at the pane that grants a given permission.
void open_microphone_settings();
void open_contacts_settings();
void open_screen_recording_settings();

// --- reading the screen -----------------------------------------------------

// One control in some application's window.
struct Element {
    std::string role;         // AXButton, AXTextField, AXMenuItem…
    std::string title;        // the visible label
    std::string value;        // current contents, for fields
    double x = 0, y = 0;      // screen position of its centre
    bool actionable = false;  // has an AXPress action
    bool valid() const noexcept { return !role.empty(); }
};

// The frontmost application's name ("Safari"), or empty.
std::string frontmost_app();

// The title of the frontmost window, or empty.
std::string focused_window_title();

// Every actionable control in the frontmost window, depth-first. `limit` caps
// the walk: an app like Xcode has thousands of elements and a full traversal is
// slow enough to be felt.
std::vector<Element> controls(int limit = 200);

// The text of whatever field currently has focus, or empty.
std::string focused_text();

// --- acting -----------------------------------------------------------------

// Presses the control whose visible label best matches `label`, in the
// frontmost window. Matching is case-insensitive and accepts a partial name, so
// "save" finds "Save As…". Returns false when nothing matched.
bool press(const std::string& label);

// Types text into the focused field, as if the user had typed it.
bool type_text(const std::string& text);

// Replaces the focused field's contents outright.
bool set_focused_text(const std::string& text);

// A keyboard shortcut, e.g. key_stroke("s", {"command"}) for Cmd-S. Modifiers
// are any of "command", "shift", "option", "control".
bool key_stroke(const std::string& key, const std::vector<std::string>& modifiers);

// Picks a menu item from the frontmost app's menu bar, e.g.
// menu_click({"File", "Export…"}). The path is followed literally.
bool menu_click(const std::vector<std::string>& path);

}  // namespace mimi::brain::ax
