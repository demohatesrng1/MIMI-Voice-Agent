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
        log::warn(kTag, "no native window yet; call this after winId()");
        return;
    }

    // Content extends under the title bar, and the title bar itself paints
    // nothing. The traffic lights stay -- they are the part users actually
    // need, and reimplementing them is how apps end up feeling wrong.
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;

    // AppKit draws its own hairline under the title bar once content sits
    // behind it. The design is one unified surface, so: no line.
    window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;

    // The empty toolbar is not decoration -- unified-compact style is what
    // grows the title-bar strip to toolbar height and vertically centres the
    // traffic lights in it. Without it the lights hug the top-left corner of
    // the taller custom bar. This is the same mechanism behind Electron's
    // "hiddenInset" look. It draws nothing because the titlebar is
    // transparent and it has no items.
    if (window.toolbar == nil) {
        window.toolbar = [[NSToolbar alloc] initWithIdentifier:@"mimi.window.chrome"];
        window.toolbarStyle = NSWindowToolbarStyleUnifiedCompact;
    }

    // Dragging by the background is what replaces the missing title bar. Text
    // fields and buttons still get their events first. (Qt's view swallows
    // most of these drags; the title bar widget also starts a native move
    // explicitly, so both paths are covered.)
    window.movableByWindowBackground = YES;

    // Single-window app; the system's window-tab menu items only confuse.
    window.tabbingMode = NSWindowTabbingModeDisallowed;

    // Dark chrome, so the traffic lights, menus and vibrancy match the
    // black-and-pink design rather than fighting it. Deliberately pinned:
    // the palette has no light variant, and grey traffic lights on near-black
    // is exactly the mismatch this file exists to avoid.
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];

    log::info(kTag, "titlebar adopted (backing scale {}x)",
              window.backingScaleFactor);
}

void add_window_vibrancy(QWidget* widget) {
    NSWindow* window = native_window(widget);
    if (window == nil) return;

    // Qt's QNSView is the content view; its superview is the frame view that
    // owns all window chrome. The effect view goes there, underneath Qt, so
    // the blur shows through wherever the stylesheet paints with alpha.
    NSView* content = window.contentView;
    NSView* frame = content.superview;
    if (frame == nil) {
        log::warn(kTag, "no frame view; vibrancy skipped");
        return;
    }
    // Checked by identifier, not by class: modern AppKit keeps effect views
    // of its own inside the frame, and matching on class mistakes those for
    // ours and never attaches anything.
    NSString* const fx_id = @"mimi.vibrancy";
    for (NSView* sibling in frame.subviews) {
        if ([sibling.identifier isEqualToString:fx_id]) return;  // already attached
    }

    NSVisualEffectView* fx = [[NSVisualEffectView alloc] initWithFrame:frame.bounds];
    fx.identifier = fx_id;
    fx.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    // The material Apple designates for exactly this: the window's own
    // backdrop, as opposed to sidebar/menu/popover materials.
    fx.material = NSVisualEffectMaterialUnderWindowBackground;
    fx.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    // Dims when the window deactivates, like every native window backdrop.
    fx.state = NSVisualEffectStateFollowsWindowActiveState;
    [frame addSubview:fx positioned:NSWindowBelow relativeTo:content];

    log::info(kTag, "vibrancy attached");
}

int traffic_light_inset(QWidget* widget) {
    NSWindow* window = native_window(widget);
    NSButton* zoom =
        window != nil ? [window standardWindowButton:NSWindowZoomButton] : nil;
    if (zoom == nil) return 78;  // no window to measure; the classic value
    // Measured rather than guessed, from this window's real buttons, so the
    // value tracks the toolbar style and whatever Apple changes next.
    const NSRect in_window = [zoom convertRect:zoom.bounds toView:nil];
    return static_cast<int>(NSMaxX(in_window)) + 12;
}

void pin_overlay_window(QWidget* widget) {
    NSWindow* window = native_window(widget);
    if (window == nil) return;

    // hidesOnDeactivate is the actual cause of the orb disappearing: NSPanel
    // defaults it to YES, so AppKit orders the window out the moment another
    // application comes forward.
    window.hidesOnDeactivate = NO;
    window.level = NSFloatingWindowLevel;
    window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                NSWindowCollectionBehaviorStationary |
                                NSWindowCollectionBehaviorFullScreenAuxiliary |
                                NSWindowCollectionBehaviorIgnoresCycle;
    // Clicking the orb must not steal focus from whatever the user was typing
    // in, and the panel must not be a candidate for becoming the key window
    // when the app is activated.
    if ([window isKindOfClass:[NSPanel class]]) {
        NSPanel* panel = (NSPanel*)window;
        panel.floatingPanel = YES;
        panel.becomesKeyOnlyIfNeeded = YES;
        panel.worksWhenModal = YES;
    }
    // Survives the window being ordered out by something else.
    [window setCanHide:NO];
}

void titlebar_double_clicked(QWidget* widget) {
    NSWindow* window = native_window(widget);
    if (window == nil) return;
    // Honour the user's "double-click a window's title bar to..." setting
    // instead of hardcoding zoom.
    NSString* action = [[NSUserDefaults standardUserDefaults]
        stringForKey:@"AppleActionOnDoubleClick"];
    if ([action isEqualToString:@"Minimize"]) {
        [window miniaturize:nil];
    } else if (![action isEqualToString:@"None"]) {
        [window zoom:nil];
    }
}

}  // namespace mimi::ui
