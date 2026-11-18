#import <Cocoa/Cocoa.h>
#define main template_main
#include "../main.c"
#undef main
#include "../macos_menu.h"
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

static NSMenu *find_submenu(NSString *title) {
    for (NSMenuItem *item in NSApp.mainMenu.itemArray) {
        if ([item.submenu.title isEqualToString:title]) return item.submenu;
    }
    return nil;
}

static NSMenuItem *find_item(NSMenu *menu, NSString *title) {
    for (NSMenuItem *item in menu.itemArray) {
        if ([item.title isEqualToString:title]) return item;
    }
    return nil;
}

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

        static app_state state;
        app_chrome_reset(&state.chrome, NULL);
        app_chrome_set_action_handler(&state.chrome, on_action, &state);

        cui_app *app = cui_app_create(&(cui_app_desc){ .hidden = true });
        assert(app);
        assert(cui_app_frame(app));
        frame(cui_app_ui(app), &state);

        NSMenu *bar = NSApp.mainMenu;
        NSMenu *appMenu = bar.itemArray.firstObject.submenu;
        NSInteger appCount = appMenu.numberOfItems;
        NSInteger barCount = bar.numberOfItems;

        /* Re-installing menu hooks is idempotent. */
        app_menu_install(&(app_menu_hooks){
            .settings_open = &state.chrome.settings_open,
            .settings_section = &state.chrome.settings_section,
            .about_section = SETTINGS_SECTION_ABOUT,
            .palette_open = &state.chrome.palette_open,
            .user = &state.chrome,
        });
        assert(appMenu.numberOfItems == appCount);
        assert(bar.numberOfItems == barCount);

        /* Re-sync real hooks via frame(). */
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        frame(cui_app_ui(app), &state);

        /* Menu bar parity: App, File, Edit, View, Window in standard macOS order. */
        assert(bar.numberOfItems >= 5);
        assert([[[bar itemAtIndex:1] submenu].title isEqualToString:@"File"]);
        assert([[[bar itemAtIndex:2] submenu].title isEqualToString:@"Edit"]);
        assert([[[bar itemAtIndex:3] submenu].title isEqualToString:@"View"]);
        assert([[[bar itemAtIndex:4] submenu].title isEqualToString:@"Window"]);

        NSMenu *fileMenu = find_submenu(@"File");
        NSMenu *editMenu = find_submenu(@"Edit");
        NSMenu *viewMenu = find_submenu(@"View");
        assert(fileMenu && editMenu && viewMenu);

        NSMenuItem *saveItem = find_item(fileMenu, @"Save");
        NSMenuItem *cutItem = find_item(editMenu, @"Cut");
        NSMenuItem *copyItem = find_item(editMenu, @"Copy");
        NSMenuItem *pasteItem = find_item(editMenu, @"Paste");
        NSMenuItem *selectAllItem = find_item(editMenu, @"Select All");
        NSMenuItem *findItem = find_item(editMenu, @"Find");
        NSMenuItem *paletteItem = find_item(viewMenu, @"Command Palette…");
        NSMenuItem *fullscreenItem = find_item(viewMenu, @"Enter Full Screen");
        assert(saveItem && cutItem && copyItem && pasteItem && selectAllItem && findItem);
        assert(paletteItem && fullscreenItem);

        /* Default key equivalents reflect default settings. */
        assert([saveItem.keyEquivalent isEqualToString:@"s"]);
        assert(saveItem.keyEquivalentModifierMask == NSEventModifierFlagCommand);
        assert([findItem.keyEquivalent isEqualToString:@"f"]);
        assert(findItem.keyEquivalentModifierMask == NSEventModifierFlagCommand);
        assert([paletteItem.keyEquivalent isEqualToString:@"k"]);
        assert(paletteItem.keyEquivalentModifierMask == NSEventModifierFlagCommand);
        assert([fullscreenItem.keyEquivalent isEqualToString:@"f"]);
        assert(fullscreenItem.keyEquivalentModifierMask ==
               (NSEventModifierFlagCommand | NSEventModifierFlagControl));

        /* Dynamic shortcut sync updates native menu key equivalents (and clears removed bindings). */
        state.chrome.settings.keybindings[APP_ACTION_SAVE].combo =
            (cui_key_combo){ CUI_MOD_PRIMARY | CUI_MOD_SHIFT, CUI_KEY_J };
        state.chrome.settings.keybindings[APP_ACTION_FIND].combo =
            (cui_key_combo){ 0, CUI_KEY_NONE };
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        frame(cui_app_ui(app), &state);
        assert([saveItem.keyEquivalent isEqualToString:@"j"]);
        assert(saveItem.keyEquivalentModifierMask ==
               (NSEventModifierFlagCommand | NSEventModifierFlagShift));
        assert([findItem.keyEquivalent isEqualToString:@""]);
        assert(findItem.keyEquivalentModifierMask == 0);

        /* Fullscreen label reflects fullscreen state via app_menu_sync. */
        app_menu_sync(&(app_menu_state){
            .save_combo = state.chrome.settings.keybindings[APP_ACTION_SAVE].combo,
            .find_combo = state.chrome.settings.keybindings[APP_ACTION_FIND].combo,
            .palette_combo = state.chrome.settings.keybindings[APP_ACTION_COMMAND_PALETTE].combo,
            .fullscreen = true,
        });
        assert([fullscreenItem.title isEqualToString:@"Exit Full Screen"]);
        app_menu_sync(&(app_menu_state){
            .save_combo = state.chrome.settings.keybindings[APP_ACTION_SAVE].combo,
            .find_combo = state.chrome.settings.keybindings[APP_ACTION_FIND].combo,
            .palette_combo = state.chrome.settings.keybindings[APP_ACTION_COMMAND_PALETTE].combo,
            .fullscreen = false,
        });
        assert([fullscreenItem.title isEqualToString:@"Enter Full Screen"]);

        /* Menu validation disables Save/Find when Settings or Command Palette is open. */
        id<NSMenuItemValidation> validator = (id<NSMenuItemValidation>)saveItem.target;
        state.chrome.settings_open = false;
        state.chrome.palette_open = false;
        assert([validator validateMenuItem:saveItem]);
        assert([validator validateMenuItem:findItem]);
        assert([validator validateMenuItem:paletteItem]);
        assert([validator validateMenuItem:copyItem]);

        state.chrome.settings_open = true;
        assert(![validator validateMenuItem:saveItem]);
        assert(![validator validateMenuItem:findItem]);
        assert([validator validateMenuItem:paletteItem]);
        assert([validator validateMenuItem:copyItem]);
        state.chrome.settings_open = false;

        /* Triggering File -> Save and Edit -> Find runs on_action on the next frame. */
        state.chrome.status = NULL;
        [fileMenu performActionForItemAtIndex:[fileMenu indexOfItem:saveItem]];
        assert(cui_app_frame(app));
        frame(cui_app_ui(app), &state);
        assert(state.chrome.status && strcmp(state.chrome.status, "Save triggered.") == 0);
        assert(state.chrome.status_tone == CUI_TONE_SUCCESS);

        [editMenu performActionForItemAtIndex:[editMenu indexOfItem:findItem]];
        assert(cui_app_frame(app));
        frame(cui_app_ui(app), &state);
        assert(state.chrome.status && strcmp(state.chrome.status, "Find triggered.") == 0);
        assert(state.chrome.status_tone == CUI_TONE_INFO);

        /* Triggering View -> Command Palette… opens palette and closes Settings. */
        state.chrome.settings_open = true;
        state.chrome.palette_open = false;
        [viewMenu performActionForItemAtIndex:[viewMenu indexOfItem:paletteItem]];
        assert(state.chrome.palette_open);
        assert(!state.chrome.settings_open);
        state.chrome.palette_open = false;

        /* Triggering Edit -> Select All injects Cmd+A for one frame and resets mods afterward. */
        [editMenu performActionForItemAtIndex:[editMenu indexOfItem:selectAllItem]];
        assert(state.chrome.pending_edit_key == CUI_KEY_A);
        assert(cui_app_frame(app));
        cui_ctx *ui = cui_app_ui(app);
        app_chrome_begin_frame(&state.chrome, ui);
        assert(cui_key_pressed(ui, CUI_KEY_A));
        assert((cui_key_mods(ui) & CUI_MOD_PRIMARY) != 0);
        app_chrome_end_frame(&state.chrome, ui);
        assert(cui_key_mods(ui) == 0);

        /* Index 0 is About <APP_NAME>, index 2 is Settings… */
        assert([[appMenu itemAtIndex:0].title isEqualToString:@"About " APP_NAME]);
        assert([[appMenu itemAtIndex:2].title isEqualToString:@"Settings…"]);

        state.chrome.settings_open = false;
        state.chrome.settings_section = SETTINGS_SECTION_GENERAL;
        state.chrome.palette_open = true;
        [appMenu performActionForItemAtIndex:0];
        assert(state.chrome.settings_open);
        assert(state.chrome.settings_section == SETTINGS_SECTION_ABOUT);
        assert(!state.chrome.palette_open);

        state.chrome.settings_open = false;
        state.chrome.settings_section = SETTINGS_SECTION_GENERAL;
        [appMenu performActionForItemAtIndex:2];
        assert(state.chrome.settings_open);
        assert(state.chrome.settings_section == SETTINGS_SECTION_GENERAL);

        state.chrome.settings_open = false;
        NSEvent *shortcut = [NSEvent keyEventWithType:NSEventTypeKeyDown
            location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand
            timestamp:0 windowNumber:0 context:nil characters:@","
            charactersIgnoringModifiers:@"," isARepeat:NO keyCode:43];
        assert([NSApp.mainMenu performKeyEquivalent:shortcut]);
        assert(state.chrome.settings_open);

        /* Probe the menu item action instead of a second local event monitor,
         * since AppKit does not guarantee invocation order across monitors. */
        NSMenuItem *settings_item = [appMenu itemAtIndex:2];
        id orig_target = settings_item.target;
        SEL orig_action = settings_item.action;
        MenuProbe *probe = [[MenuProbe alloc] init];
        settings_item.target = probe;
        settings_item.action = @selector(menuAction:);

        state.chrome.settings_open = false;
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        [NSApp sendEvent:shortcut];
        frame(cui_app_ui(app), &state);
        assert(state.chrome.settings_open);
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
        frame(cui_app_ui(app), &state);
        assert(!state.chrome.settings_open);

        /* Window position save, off-screen rejection, and restore on macOS. */
        const char *pos_path = "/tmp/c_gui_template_pos_test.json";
        remove(pos_path);
        state.chrome.settings_path = pos_path;
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

        /* Moving the window marks dirty and app_chrome_flush persists it to disk. */
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        frame(cui_app_ui(app), &state);
        assert(state.chrome.settings.window_has_pos);
        assert(state.chrome.settings.window_x == target_x && state.chrome.settings.window_y == target_y);
        assert(state.chrome.window_dirty);
        app_chrome_flush(&state.chrome);
        assert(!state.chrome.window_dirty);

        app_settings reloaded = app_chrome_default_settings();
        assert(app_settings_load(&reloaded, pos_path));
        assert(reloaded.window_has_pos);
        assert(reloaded.window_x == target_x && reloaded.window_y == target_y);
        cui_app_destroy(app);

        /* A new window restores the saved position on its first frame. */
        state.chrome.settings = reloaded;
        state.chrome.window_state_ui = NULL;
        state.chrome.window_dirty = false;
        cui_app *app2 = cui_app_create(&(cui_app_desc){
            .hidden = true,
            .width = state.chrome.settings.window_width,
            .height = state.chrome.settings.window_height,
        });
        assert(app2);
        assert(cui_app_frame(app2));
        frame(cui_app_ui(app2), &state);
        NSWindow *window2 = (__bridge NSWindow *)cui_app_native_window(app2);
        assert(app_window_get_pos((__bridge void *)window2, "cocoa", &got_x, &got_y));
        assert(got_x == target_x && got_y == target_y);
        assert(!state.chrome.window_dirty);
        cui_app_destroy(app2);
        remove(pos_path);
    }
    return 0;
}
