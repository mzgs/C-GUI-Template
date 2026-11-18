#import <Cocoa/Cocoa.h>
#include "platform.h"
#include <math.h>
#include <string.h>

bool macos_settings_path(char *path, size_t capacity) {
    @autoreleasepool {
        NSURL *support = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                               inDomains:NSUserDomainMask].firstObject;
        NSString *name = NSBundle.mainBundle.infoDictionary[@"CFBundleName"];
        if (!name.length) name = @APP_NAME;
        if (!support || !name.length) return false;
        NSURL *folder = [support URLByAppendingPathComponent:name isDirectory:YES];
        NSURL *file = [folder URLByAppendingPathComponent:@"settings.json" isDirectory:NO];
        return [file getFileSystemRepresentation:path maxLength:capacity];
    }
}

int app_system_dark(void) {
    if (@available(macOS 10.14, *)) {
        @autoreleasepool {
            NSAppearance *appearance = NSApp ? [NSApp effectiveAppearance] : nil;
            if (appearance) {
                NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:@[
                    NSAppearanceNameAqua, NSAppearanceNameDarkAqua
                ]];
                return [match isEqualToString:NSAppearanceNameDarkAqua] ? 1 : 0;
            }
            NSString *style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
            return [style isEqualToString:@"Dark"] ? 1 : 0;
        }
    }
    return -1;
}

static bool macos_rect_visible_on_any_screen(NSRect frame) {
    /* Ensure the top titlebar band is reachable on at least one active display. */
    CGFloat bar_h = frame.size.height < 32.0 ? frame.size.height : 32.0;
    NSRect titlebar = NSMakeRect(frame.origin.x, NSMaxY(frame) - bar_h, frame.size.width, bar_h);
    for (NSScreen *screen in [NSScreen screens]) {
        NSRect visible = screen.visibleFrame;
        NSRect overlap = NSIntersectionRect(titlebar, visible);
        if (overlap.size.width >= 64.0 && overlap.size.height >= 16.0) return true;
    }
    return false;
}

bool app_window_get_pos(void *native_window, const char *backend, int *x, int *y) {
    if (!native_window || !backend || !x || !y || strcmp(backend, "cocoa") != 0) return false;
    @autoreleasepool {
        NSWindow *win = (__bridge NSWindow *)native_window;
        NSRect frame = win.frame;
        *x = (int)llround(frame.origin.x);
        *y = (int)llround(frame.origin.y);
        return true;
    }
}

bool app_window_set_pos(void *native_window, const char *backend, int x, int y) {
    if (!native_window || !backend || strcmp(backend, "cocoa") != 0) return false;
    if (x < APP_WINDOW_POS_MIN || x > APP_WINDOW_POS_MAX ||
        y < APP_WINDOW_POS_MIN || y > APP_WINDOW_POS_MAX)
        return false;
    @autoreleasepool {
        NSWindow *win = (__bridge NSWindow *)native_window;
        NSRect frame = win.frame;
        frame.origin = NSMakePoint((CGFloat)x, (CGFloat)y);
        if (!macos_rect_visible_on_any_screen(frame)) return false;
        [win setFrameOrigin:frame.origin];
        return true;
    }
}

void app_window_center(void *native_window, const char *backend) {
    if (!native_window || !backend || strcmp(backend, "cocoa") != 0) return;
    @autoreleasepool {
        NSWindow *win = (__bridge NSWindow *)native_window;
        [win center];
    }
}
