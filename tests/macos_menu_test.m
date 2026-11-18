#import <Cocoa/Cocoa.h>
#define main template_main
#include "../main.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include <string.h>

bool macos_settings_path(char *path, size_t capacity);
int macos_system_dark(void);

@interface MenuProbe : NSObject
@property(nonatomic, assign) bool triggered;
- (void)menuAction:(id)sender;
@end

@implementation MenuProbe
- (void)menuAction:(id)sender {
    (void)sender;
    self.triggered = true;
}
@end

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

        int sys_dark = macos_system_dark();
        assert(sys_dark == 0 || sys_dark == 1);

        cui_app *app = cui_app_create(&(cui_app_desc){ .hidden = true });
        assert(app);
        assert(cui_app_frame(app));
        frame(cui_app_ui(app), NULL);
        NSMenu *menu = NSApp.mainMenu.itemArray.firstObject.submenu;
        NSInteger count = menu.numberOfItems;
        app_menu_install(&(app_menu_hooks){
            .settings_open = &settings_open,
            .settings_section = &settings_section,
            .about_section = SETTINGS_SECTION_ABOUT,
            .palette_open = &palette_open,
        });
        assert(menu.numberOfItems == count);

        /* Index 0 is About <APP_NAME>, index 2 is Settings… */
        assert([[menu itemAtIndex:0].title isEqualToString:@"About " APP_NAME]);
        assert([[menu itemAtIndex:2].title isEqualToString:@"Settings…"]);

        settings_open = false;
        settings_section = SETTINGS_SECTION_GENERAL;
        palette_open = true;
        [menu performActionForItemAtIndex:0];
        assert(settings_open);
        assert(settings_section == SETTINGS_SECTION_ABOUT);
        assert(!palette_open);

        settings_open = false;
        settings_section = SETTINGS_SECTION_GENERAL;
        [menu performActionForItemAtIndex:2];
        assert(settings_open);
        assert(settings_section == SETTINGS_SECTION_GENERAL);

        settings_open = false;
        NSEvent *shortcut = [NSEvent keyEventWithType:NSEventTypeKeyDown
            location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand
            timestamp:0 windowNumber:0 context:nil characters:@","
            charactersIgnoringModifiers:@"," isARepeat:NO keyCode:43];
        assert([NSApp.mainMenu performKeyEquivalent:shortcut]);
        assert(settings_open);

        /* Probe the menu item action instead of a second local event monitor,
         * since AppKit does not guarantee invocation order across monitors. */
        NSMenuItem *settings_item = [menu itemAtIndex:2];
        id orig_target = settings_item.target;
        SEL orig_action = settings_item.action;
        MenuProbe *probe = [[MenuProbe alloc] init];
        settings_item.target = probe;
        settings_item.action = @selector(menuAction:);

        settings_open = false;
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        [NSApp sendEvent:shortcut];
        frame(cui_app_ui(app), NULL);
        assert(settings_open);
        assert(!probe.triggered);

        /* While Settings is open, Cmd+, passes through the monitor to AppKit / CUI. */
        NSWindow *window = (__bridge NSWindow *)cui_app_native_window(app);
        NSEvent *window_shortcut = [NSEvent keyEventWithType:NSEventTypeKeyDown
            location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand
            timestamp:0 windowNumber:window.windowNumber context:nil characters:@","
            charactersIgnoringModifiers:@"," isARepeat:NO keyCode:43];
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        [NSApp sendEvent:window_shortcut];
        assert(probe.triggered);
        settings_item.target = orig_target;
        settings_item.action = orig_action;
        assert([window performKeyEquivalent:window_shortcut]);
        assert(cui_shortcut(cui_app_ui(app), CUI_MOD_PRIMARY, CUI_KEY_COMMA));

        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        cui_input_key(cui_app_ui(app), CUI_KEY_ESCAPE, true, 0);
        frame(cui_app_ui(app), NULL);
        assert(!settings_open);

        /* Window position save, off-screen rejection, and restore on macOS. */
        const char *pos_path = "/tmp/c_gui_template_pos_test.json";
        remove(pos_path);
        NSScreen *screen = window.screen ?: [NSScreen mainScreen];
        NSRect vis = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1200, 800);
        int target_x = (int)llround(vis.origin.x + 80.0);
        int target_y = (int)llround(vis.origin.y + 80.0);
        assert(app_window_set_pos((__bridge void *)window, "cocoa", target_x, target_y));
        int got_x = 0, got_y = 0;
        assert(app_window_get_pos((__bridge void *)window, "cocoa", &got_x, &got_y));
        assert(got_x == target_x && got_y == target_y);

        /* Off-screen positions are rejected so windows aren't lost on unplugged monitors. */
        assert(!app_window_set_pos((__bridge void *)window, "cocoa", -20000, -20000));
        assert(app_window_get_pos((__bridge void *)window, "cocoa", &got_x, &got_y));
        assert(got_x == target_x && got_y == target_y);

        /* Moving the window marks dirty and on_will_terminate flushes it to disk. */
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        frame(cui_app_ui(app), (void *)pos_path);
        assert(settings.window_has_pos);
        assert(settings.window_x == target_x && settings.window_y == target_y);
        assert(window_dirty);
        on_will_terminate();
        assert(!window_dirty);

        app_settings reloaded = default_settings;
        assert(app_settings_load(&reloaded, pos_path));
        assert(reloaded.window_has_pos);
        assert(reloaded.window_x == target_x && reloaded.window_y == target_y);
        cui_app_destroy(app);

        /* A new window restores the saved position on its first frame. */
        settings = reloaded;
        window_state_ui = NULL;
        window_dirty = false;
        cui_app *app2 = cui_app_create(&(cui_app_desc){
            .hidden = true,
            .width = settings.window_width,
            .height = settings.window_height,
        });
        assert(app2);
        assert(cui_app_frame(app2));
        frame(cui_app_ui(app2), (void *)pos_path);
        NSWindow *window2 = (__bridge NSWindow *)cui_app_native_window(app2);
        assert(app_window_get_pos((__bridge void *)window2, "cocoa", &got_x, &got_y));
        assert(got_x == target_x && got_y == target_y);
        assert(!window_dirty);
        cui_app_destroy(app2);
        remove(pos_path);
    }
    return 0;
}
