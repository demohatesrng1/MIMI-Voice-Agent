#include "ui/mac_window.hpp"

#include "core/log.hpp"

#import <AppKit/AppKit.h>

#include <QWidget>

#include <string_view>

namespace mimi::ui {
namespace {

constexpr std::string_view kTag = "window";

NSWindow* native_window(QWidget* widget) {
    if (widget == nullptr) return nil;
    // winId() forces creation of the native view if it does not exist yet.
    // __bridge, because ARC will not accept a plain cast from an integer handle
    // and we are borrowing the view rather than taking ownership of it.
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(widget->winId());
    return view != nil ? [view window] : nil;
}

}  // namespace

void adopt_native_titlebar(QWidget* widget) {
    NSWindow* window = native_window(widget);
    if (window == nil) {
        log::warn(kTag, "no native window yet; call this after show()");
        return;
    }

    // Content extends under the title bar, and the title bar itself paints
    // nothing. The traffic lights stay -- they are the part users actually
    // need, and reimplementing them is how apps end up feeling wrong.
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;

    // Dragging by the background is what replaces the missing title bar. Text
    // fields and buttons still get their events first.
    window.movableByWindowBackground = YES;

    // Dark chrome, so the traffic lights and any system menus match the app
    // rather than fighting it.
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];

    log::info(kTag, "titlebar adopted (backing scale {}x)",
              window.backingScaleFactor);
}

int traffic_light_inset() {
    // Measured rather than guessed: ask the window for where the buttons
    // actually are, so this stays correct if Apple moves them again.
    static int inset = 0;
    if (inset > 0) return inset;

    NSWindow* probe = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 400, 200)
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:YES];
    NSButton* zoom = [probe standardWindowButton:NSWindowZoomButton];
    inset = zoom != nil ? static_cast<int>(NSMaxX(zoom.frame)) + 12 : 78;
    [probe close];
    return inset;
}

void make_draggable_background(QWidget* widget) {
    NSWindow* window = native_window(widget);
    if (window != nil) window.movableByWindowBackground = YES;
}

}  // namespace mimi::ui
