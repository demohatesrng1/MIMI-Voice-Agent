#include "brain/accessibility.hpp"

#include "brain/shell.hpp"
#include "core/log.hpp"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <Contacts/Contacts.h>
#import <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace mimi::brain::ax {
namespace {

constexpr std::string_view kTag = "ax";

std::string to_string(CFStringRef text) {
    if (text == nullptr) return {};
    const CFIndex length = CFStringGetLength(text);
    const CFIndex bytes = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(bytes), '\0');
    if (!CFStringGetCString(text, out.data(), bytes, kCFStringEncodingUTF8)) return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

// A CFTypeRef attribute off an element, or nullptr. The caller owns the result.
CFTypeRef copy_attribute(AXUIElementRef element, CFStringRef name) {
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, name, &value) != kAXErrorSuccess) return nullptr;
    return value;
}

std::string string_attribute(AXUIElementRef element, CFStringRef name) {
    CFTypeRef value = copy_attribute(element, name);
    if (value == nullptr) return {};
    std::string out;
    if (CFGetTypeID(value) == CFStringGetTypeID()) out = to_string((CFStringRef)value);
    CFRelease(value);
    return out;
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The AXUIElement for the frontmost application, or nullptr. The caller owns it.
AXUIElementRef copy_frontmost_element() {
    NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (front == nil) return nullptr;
    return AXUIElementCreateApplication(front.processIdentifier);
}

// The frontmost window of an application element. The caller owns it.
AXUIElementRef copy_focused_window(AXUIElementRef app) {
    if (app == nullptr) return nullptr;
    CFTypeRef window = copy_attribute(app, kAXFocusedWindowAttribute);
    if (window == nullptr) window = copy_attribute(app, kAXMainWindowAttribute);
    if (window == nullptr) return nullptr;
    if (CFGetTypeID(window) != AXUIElementGetTypeID()) {
        CFRelease(window);
        return nullptr;
    }
    return (AXUIElementRef)window;
}

// The label a user would call this control by. Several attributes carry it
// depending on how the app was built, so they are tried in order of how close
// each is to what is actually painted on screen.
std::string label_of(AXUIElementRef element) {
    for (CFStringRef attribute : {kAXTitleAttribute, kAXDescriptionAttribute,
                                  kAXHelpAttribute, kAXValueAttribute}) {
        std::string found = string_attribute(element, attribute);
        if (!found.empty()) return found;
    }
    return {};
}

bool has_press_action(AXUIElementRef element) {
    CFArrayRef actions = nullptr;
    if (AXUIElementCopyActionNames(element, &actions) != kAXErrorSuccess) return false;
    if (actions == nullptr) return false;
    bool pressable = false;
    for (CFIndex i = 0; i < CFArrayGetCount(actions); ++i) {
        const auto name = to_string((CFStringRef)CFArrayGetValueAtIndex(actions, i));
        if (name == "AXPress") { pressable = true; break; }
    }
    CFRelease(actions);
    return pressable;
}

void element_position(AXUIElementRef element, double* x, double* y) {
    CFTypeRef position = copy_attribute(element, kAXPositionAttribute);
    CFTypeRef size = copy_attribute(element, kAXSizeAttribute);
    CGPoint origin{0, 0};
    CGSize extent{0, 0};
    if (position != nullptr) {
        AXValueGetValue((AXValueRef)position, (AXValueType)kAXValueCGPointType, &origin);
        CFRelease(position);
    }
    if (size != nullptr) {
        AXValueGetValue((AXValueRef)size, (AXValueType)kAXValueCGSizeType, &extent);
        CFRelease(size);
    }
    *x = origin.x + extent.width / 2;
    *y = origin.y + extent.height / 2;
}

// Depth-first walk, collecting controls. Stops at `limit` because a full
// traversal of a large app is slow enough for a user to notice.
void walk(AXUIElementRef element, std::vector<Element>& out, int limit, int depth) {
    if (element == nullptr || static_cast<int>(out.size()) >= limit || depth > 12) return;

    Element found;
    found.role = string_attribute(element, kAXRoleAttribute);
    found.title = label_of(element);
    found.value = string_attribute(element, kAXValueAttribute);
    found.actionable = has_press_action(element);
    if (found.actionable && !found.title.empty()) {
        element_position(element, &found.x, &found.y);
        out.push_back(found);
    }

    CFTypeRef children = copy_attribute(element, kAXChildrenAttribute);
    if (children == nullptr) return;
    if (CFGetTypeID(children) == CFArrayGetTypeID()) {
        CFArrayRef list = (CFArrayRef)children;
        for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
            if (static_cast<int>(out.size()) >= limit) break;
            walk((AXUIElementRef)CFArrayGetValueAtIndex(list, i), out, limit, depth + 1);
        }
    }
    CFRelease(children);
}

// Finds the control whose label best matches. An exact match beats a prefix,
// which beats a substring -- "save" should find "Save" rather than "Save As…"
// when both are present.
AXUIElementRef copy_matching_control(AXUIElementRef root, const std::string& label,
                                     int depth = 0) {
    if (root == nullptr || depth > 12) return nullptr;
    const std::string wanted = lowercase(label);

    AXUIElementRef exact = nullptr;
    AXUIElementRef partial = nullptr;

    CFTypeRef children = copy_attribute(root, kAXChildrenAttribute);
    if (children != nullptr && CFGetTypeID(children) == CFArrayGetTypeID()) {
        CFArrayRef list = (CFArrayRef)children;
        for (CFIndex i = 0; i < CFArrayGetCount(list) && exact == nullptr; ++i) {
            AXUIElementRef child = (AXUIElementRef)CFArrayGetValueAtIndex(list, i);
            const std::string found = lowercase(label_of(child));
            if (!found.empty() && has_press_action(child)) {
                if (found == wanted) {
                    exact = (AXUIElementRef)CFRetain(child);
                    break;
                }
                if (partial == nullptr && found.find(wanted) != std::string::npos) {
                    partial = (AXUIElementRef)CFRetain(child);
                }
            }
            if (exact == nullptr) {
                AXUIElementRef deeper = copy_matching_control(child, label, depth + 1);
                if (deeper != nullptr) {
                    if (lowercase(label_of(deeper)) == wanted) {
                        exact = deeper;
                    } else if (partial == nullptr) {
                        partial = deeper;
                    } else {
                        CFRelease(deeper);
                    }
                }
            }
        }
    }
    if (children != nullptr) CFRelease(children);

    if (exact != nullptr) {
        if (partial != nullptr) CFRelease(partial);
        return exact;
    }
    return partial;
}

// Menu lookup is its own search: a menu item exposes no AXPress action until
// its parent menu has been opened, so the actionable test used for ordinary
// controls would reject every item in a closed menu. Matching is by label
// alone, one level down, following the AXMenu wrapper AppKit puts between an
// item and its children.
AXUIElementRef copy_menu_child(AXUIElementRef parent, const std::string& label) {
    if (parent == nullptr) return nullptr;
    const std::string wanted = lowercase(label);

    CFTypeRef children = copy_attribute(parent, kAXChildrenAttribute);
    if (children == nullptr) return nullptr;
    AXUIElementRef found = nullptr;
    if (CFGetTypeID(children) == CFArrayGetTypeID()) {
        CFArrayRef list = (CFArrayRef)children;
        for (CFIndex i = 0; i < CFArrayGetCount(list) && found == nullptr; ++i) {
            AXUIElementRef child = (AXUIElementRef)CFArrayGetValueAtIndex(list, i);
            const std::string role = string_attribute(child, kAXRoleAttribute);
            // An AXMenu is a container, not an item: look straight through it.
            if (role == "AXMenu") {
                found = copy_menu_child(child, label);
                continue;
            }
            const std::string name = lowercase(label_of(child));
            if (name == wanted || (!name.empty() && name.find(wanted) != std::string::npos)) {
                found = (AXUIElementRef)CFRetain(child);
            }
        }
    }
    CFRelease(children);
    return found;
}

CGEventFlags flags_for(const std::vector<std::string>& modifiers) {
    CGEventFlags flags = 0;
    for (const auto& modifier : modifiers) {
        const std::string name = lowercase(modifier);
        if (name == "command" || name == "cmd") flags |= kCGEventFlagMaskCommand;
        else if (name == "shift") flags |= kCGEventFlagMaskShift;
        else if (name == "option" || name == "alt") flags |= kCGEventFlagMaskAlternate;
        else if (name == "control" || name == "ctrl") flags |= kCGEventFlagMaskControl;
    }
    return flags;
}

// The virtual key code for a single character. Only the keys a shortcut is
// plausibly built from; anything else goes through type_text instead.
CGKeyCode key_code_for(const std::string& key) {
    static const std::vector<std::pair<const char*, CGKeyCode>> kKeys{
        {"a", 0},  {"b", 11}, {"c", 8},  {"d", 2},  {"e", 14}, {"f", 3},  {"g", 5},
        {"h", 4},  {"i", 34}, {"j", 38}, {"k", 40}, {"l", 37}, {"m", 46}, {"n", 45},
        {"o", 31}, {"p", 35}, {"q", 12}, {"r", 15}, {"s", 1},  {"t", 17}, {"u", 32},
        {"v", 9},  {"w", 13}, {"x", 7},  {"y", 16}, {"z", 6},
        {"return", 36}, {"enter", 36}, {"tab", 48}, {"space", 49}, {"delete", 51},
        {"escape", 53}, {"esc", 53}, {"left", 123}, {"right", 124}, {"down", 125},
        {"up", 126},
    };
    const std::string wanted = lowercase(key);
    for (const auto& [name, code] : kKeys) {
        if (wanted == name) return code;
    }
    return 0xFFFF;
}

}  // namespace

bool has_permission() { return AXIsProcessTrusted(); }

bool request_permission() {
    // Passing the prompt option is what makes macOS show the dialog at all.
    NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

void open_permission_settings() {
    NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference."
                                      @"security?Privacy_Accessibility"];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

namespace {

Access from_av(AVAuthorizationStatus status) {
    switch (status) {
        case AVAuthorizationStatusAuthorized:    return Access::Granted;
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:    return Access::Denied;
        case AVAuthorizationStatusNotDetermined: return Access::NotAsked;
    }
    return Access::Unknown;
}

void open_settings_pane(NSString* pane) {
    NSURL* url = [NSURL URLWithString:pane];
    if (url != nil) [[NSWorkspace sharedWorkspace] openURL:url];
}

}  // namespace

Access microphone_access() {
    return from_av([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]);
}

Access contacts_access() {
    switch ([CNContactStore authorizationStatusForEntityType:CNEntityTypeContacts]) {
        case CNAuthorizationStatusAuthorized:    return Access::Granted;
        case CNAuthorizationStatusDenied:
        case CNAuthorizationStatusRestricted:    return Access::Denied;
        case CNAuthorizationStatusNotDetermined: return Access::NotAsked;
        default:                                 return Access::Unknown;
    }
}

Access screen_recording_access() {
    // Preflight asks without prompting, which is what a status page wants.
    return CGPreflightScreenCaptureAccess() ? Access::Granted : Access::Denied;
}

void open_microphone_settings() {
    open_settings_pane(@"x-apple.systempreferences:com.apple.preference.security"
                       @"?Privacy_Microphone");
}

void open_contacts_settings() {
    open_settings_pane(@"x-apple.systempreferences:com.apple.preference.security"
                       @"?Privacy_Contacts");
}

void open_screen_recording_settings() {
    open_settings_pane(@"x-apple.systempreferences:com.apple.preference.security"
                       @"?Privacy_ScreenCapture");
}

std::string frontmost_app() {
    NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (front == nil || front.localizedName == nil) return {};
    return front.localizedName.UTF8String;
}

std::string focused_window_title() {
    if (!has_permission()) return {};
    AXUIElementRef app = copy_frontmost_element();
    if (app == nullptr) return {};
    AXUIElementRef window = copy_focused_window(app);
    std::string title;
    if (window != nullptr) {
        title = string_attribute(window, kAXTitleAttribute);
        CFRelease(window);
    }
    CFRelease(app);
    return title;
}

std::vector<Element> controls(int limit) {
    std::vector<Element> found;
    if (!has_permission()) {
        log::debug(kTag, "no accessibility permission; cannot read the screen");
        return found;
    }
    AXUIElementRef app = copy_frontmost_element();
    if (app == nullptr) return found;
    AXUIElementRef window = copy_focused_window(app);
    if (window != nullptr) {
        walk(window, found, limit, 0);
        CFRelease(window);
    }
    CFRelease(app);
    return found;
}

std::string focused_text() {
    if (!has_permission()) return {};
    AXUIElementRef system = AXUIElementCreateSystemWide();
    if (system == nullptr) return {};
    CFTypeRef focused = copy_attribute(system, kAXFocusedUIElementAttribute);
    std::string text;
    if (focused != nullptr) {
        if (CFGetTypeID(focused) == AXUIElementGetTypeID()) {
            text = string_attribute((AXUIElementRef)focused, kAXValueAttribute);
        }
        CFRelease(focused);
    }
    CFRelease(system);
    return text;
}

bool press(const std::string& label) {
    if (!has_permission() || label.empty()) return false;
    AXUIElementRef app = copy_frontmost_element();
    if (app == nullptr) return false;
    AXUIElementRef window = copy_focused_window(app);
    bool pressed = false;
    if (window != nullptr) {
        AXUIElementRef control = copy_matching_control(window, label);
        if (control != nullptr) {
            pressed = AXUIElementPerformAction(control, kAXPressAction) == kAXErrorSuccess;
            CFRelease(control);
        }
        CFRelease(window);
    }
    CFRelease(app);
    log::debug(kTag, "press '{}' -> {}", label, pressed);
    return pressed;
}

bool set_focused_text(const std::string& text) {
    if (!has_permission()) return false;
    AXUIElementRef system = AXUIElementCreateSystemWide();
    if (system == nullptr) return false;
    CFTypeRef focused = copy_attribute(system, kAXFocusedUIElementAttribute);
    bool ok = false;
    if (focused != nullptr) {
        if (CFGetTypeID(focused) == AXUIElementGetTypeID()) {
            CFStringRef value = CFStringCreateWithCString(nullptr, text.c_str(),
                                                          kCFStringEncodingUTF8);
            ok = AXUIElementSetAttributeValue((AXUIElementRef)focused, kAXValueAttribute,
                                              value) == kAXErrorSuccess;
            CFRelease(value);
        }
        CFRelease(focused);
    }
    CFRelease(system);
    return ok;
}

bool type_text(const std::string& text) {
    if (!has_permission() || text.empty()) return false;

    // Posted as a unicode string rather than as key codes, so Japanese and
    // anything else outside the US layout arrives intact.
    NSString* native = [NSString stringWithUTF8String:text.c_str()];
    if (native == nil) return false;
    const NSUInteger length = native.length;
    std::vector<UniChar> buffer(length);
    [native getCharacters:buffer.data() range:NSMakeRange(0, length)];

    // A nullptr source posts as the system rather than as a synthetic HID
    // device. Events built on an explicit HID source are dropped by a number of
    // apps -- TextEdit among them -- which is why this looked like it worked
    // while typing nothing at all.
    //
    // A single event carries a limited payload; long text goes in chunks.
    constexpr NSUInteger kChunk = 20;
    for (NSUInteger start = 0; start < length; start += kChunk) {
        const NSUInteger count = std::min(kChunk, length - start);
        CGEventRef down = CGEventCreateKeyboardEvent(nullptr, 0, true);
        CGEventKeyboardSetUnicodeString(down, count, buffer.data() + start);
        CGEventPost(kCGHIDEventTap, down);
        CFRelease(down);

        CGEventRef up = CGEventCreateKeyboardEvent(nullptr, 0, false);
        CGEventKeyboardSetUnicodeString(up, count, buffer.data() + start);
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(up);

        // Without a gap the receiving app coalesces the chunks and loses most
        // of them.
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    return true;
}

bool key_stroke(const std::string& key, const std::vector<std::string>& modifiers) {
    if (!has_permission()) return false;
    const CGKeyCode code = key_code_for(key);
    if (code == 0xFFFF) {
        log::debug(kTag, "no key code for '{}'", key);
        return false;
    }
    const CGEventFlags flags = flags_for(modifiers);

    CGEventRef down = CGEventCreateKeyboardEvent(nullptr, code, true);
    CGEventSetFlags(down, flags);
    CGEventPost(kCGHIDEventTap, down);
    CFRelease(down);

    std::this_thread::sleep_for(std::chrono::milliseconds(8));

    CGEventRef up = CGEventCreateKeyboardEvent(nullptr, code, false);
    CGEventSetFlags(up, flags);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(up);

    return true;
}

bool menu_click(const std::vector<std::string>& path) {
    if (!has_permission() || path.empty()) return false;
    AXUIElementRef app = copy_frontmost_element();
    if (app == nullptr) return false;

    CFTypeRef bar = copy_attribute(app, kAXMenuBarAttribute);
    bool clicked = false;
    if (bar != nullptr && CFGetTypeID(bar) == AXUIElementGetTypeID()) {
        // Walk the path one level at a time: each item's submenu is its own
        // child, so "File > Export" is two lookups rather than one search.
        AXUIElementRef current = (AXUIElementRef)CFRetain(bar);
        for (std::size_t i = 0; i < path.size() && current != nullptr; ++i) {
            AXUIElementRef next = copy_menu_child(current, path[i]);
            CFRelease(current);
            current = next;
            if (current != nullptr && i + 1 == path.size()) {
                clicked =
                    AXUIElementPerformAction(current, kAXPressAction) == kAXErrorSuccess;
            }
        }
        if (current != nullptr) CFRelease(current);
    }
    if (bar != nullptr) CFRelease(bar);
    CFRelease(app);
    log::debug(kTag, "menu click -> {}", clicked);
    return clicked;
}

}  // namespace mimi::brain::ax
