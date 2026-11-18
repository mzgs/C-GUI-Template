#import <Cocoa/Cocoa.h>
#define main template_main
#include "../main.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include <string.h>

bool macos_settings_path(char *path, size_t capacity);

int main(void) {
    @autoreleasepool {
        /* Default macOS path works even outside a .app bundle by falling back to APP_NAME. */
        char resolved[4096];
        assert(macos_settings_path(resolved, sizeof resolved));
        const char *expected_suffix = "/Application Support/" APP_NAME "/settings.json";
        size_t resolved_len = strlen(resolved);
        size_t suffix_len = strlen(expected_suffix);
        assert(resolved_len >= suffix_len &&
               strcmp(resolved + resolved_len - suffix_len, expected_suffix) == 0);

        cui_app *app = cui_app_create(&(cui_app_desc){ .hidden = true });
        assert(app);
        assert(cui_app_frame(app));
        frame(cui_app_ui(app), NULL);
        NSMenu *menu = NSApp.mainMenu.itemArray.firstObject.submenu;
        NSInteger count = menu.numberOfItems;
        app_menu_install(&settings_open);
        assert(menu.numberOfItems == count);
        assert([[menu itemAtIndex:0].title isEqualToString:@"Settings…"]);
        [menu performActionForItemAtIndex:0];
        assert(settings_open);

        settings_open = false;
        NSEvent *shortcut = [NSEvent keyEventWithType:NSEventTypeKeyDown
            location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand
            timestamp:0 windowNumber:0 context:nil characters:@","
            charactersIgnoringModifiers:@"," isARepeat:NO keyCode:43];
        assert([NSApp.mainMenu performKeyEquivalent:shortcut]);
        assert(settings_open);

        settings_open = false;
        __block bool passed_monitor = false;
        id probe_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *event) {
            passed_monitor = true;
            return event;
        }];
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        [NSApp sendEvent:shortcut];
        frame(cui_app_ui(app), NULL);
        assert(settings_open);
        assert(!passed_monitor);

        /* While Settings is open, Cmd+, passes through the monitor to CUI. */
        NSWindow *window = (__bridge NSWindow *)cui_app_native_window(app);
        NSEvent *window_shortcut = [NSEvent keyEventWithType:NSEventTypeKeyDown
            location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand
            timestamp:0 windowNumber:window.windowNumber context:nil characters:@","
            charactersIgnoringModifiers:@"," isARepeat:NO keyCode:43];
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        [NSApp sendEvent:window_shortcut];
        assert(passed_monitor);
        assert([window performKeyEquivalent:window_shortcut]);
        assert(cui_shortcut(cui_app_ui(app), CUI_MOD_PRIMARY, CUI_KEY_COMMA));
        [NSEvent removeMonitor:probe_monitor];

        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        cui_input_key(cui_app_ui(app), CUI_KEY_ESCAPE, true, 0);
        frame(cui_app_ui(app), NULL);
        assert(!settings_open);
        cui_app_destroy(app);
    }
    return 0;
}
