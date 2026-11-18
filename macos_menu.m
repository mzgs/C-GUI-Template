#import <Cocoa/Cocoa.h>
#include <cui/cui_app.h>
#include "macos_menu.h"

@interface AppMenuTarget : NSObject
@property(nonatomic, assign) bool *settingsOpen;
@property(nonatomic, assign) int *settingsSection;
@property(nonatomic, assign) int aboutSection;
@property(nonatomic, assign) bool *paletteOpen;
@property(nonatomic, assign) void (*onWillTerminate)(void);
- (void)showAbout:(id)sender;
- (void)showSettings:(id)sender;
- (void)windowDidMove:(NSNotification *)note;
- (void)appWillTerminate:(NSNotification *)note;
@end

@implementation AppMenuTarget
- (void)showAbout:(id)sender {
    (void)sender;
    if (self.paletteOpen) *self.paletteOpen = false;
    if (self.settingsSection) *self.settingsSection = self.aboutSection;
    if (self.settingsOpen) *self.settingsOpen = true;
    cui_ctx *ui = cui_app_ui(NULL);
    if (ui) cui_request_redraw(ui);
}

- (void)showSettings:(id)sender {
    (void)sender;
    if (self.paletteOpen) *self.paletteOpen = false;
    if (self.settingsOpen) *self.settingsOpen = true;
    cui_ctx *ui = cui_app_ui(NULL);
    if (ui) cui_request_redraw(ui);
}

- (void)windowDidMove:(NSNotification *)note {
    void *native = cui_app_native_window(NULL);
    if (!native || note.object != (__bridge id)native) return;
    cui_ctx *ui = cui_app_ui(NULL);
    if (ui) cui_request_redraw(ui);
}

- (void)appWillTerminate:(NSNotification *)note {
    (void)note;
    if (self.onWillTerminate) self.onWillTerminate();
}
@end

void app_menu_install(const app_menu_hooks *hooks) {
    static AppMenuTarget *target;
    if (target || !hooks || !hooks->settings_open) return;
    NSMenu *menu = NSApp.mainMenu.itemArray.firstObject.submenu;
    if (!menu) return;

    target = [[AppMenuTarget alloc] init];
    target.settingsOpen = hooks->settings_open;
    target.settingsSection = hooks->settings_section;
    target.aboutSection = hooks->about_section;
    target.paletteOpen = hooks->palette_open;
    target.onWillTerminate = hooks->on_will_terminate;

    NSMenuItem *about = [[NSMenuItem alloc] initWithTitle:[@"About " stringByAppendingString:@APP_NAME]
        action:@selector(showAbout:) keyEquivalent:@""];
    about.target = target;
    NSMenuItem *settings = [[NSMenuItem alloc] initWithTitle:@"Settings…"
        action:@selector(showSettings:) keyEquivalent:@","];
    settings.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    settings.target = target;

    [menu insertItem:about atIndex:0];
    [menu insertItem:[NSMenuItem separatorItem] atIndex:1];
    [menu insertItem:settings atIndex:2];
    [menu insertItem:[NSMenuItem separatorItem] atIndex:3];

    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:target selector:@selector(windowDidMove:)
               name:NSWindowDidMoveNotification object:nil];
    [nc addObserver:target selector:@selector(appWillTerminate:)
               name:NSApplicationWillTerminateNotification object:nil];

    /* CUI consumes custom Command shortcuts before the native menu sees them. */
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *event) {
        if (*target.settingsOpen) return event;
        NSEventModifierFlags modifiers = event.modifierFlags &
            (NSEventModifierFlagCommand | NSEventModifierFlagControl |
             NSEventModifierFlagOption | NSEventModifierFlagShift);
        if (modifiers == NSEventModifierFlagCommand &&
            [event.charactersIgnoringModifiers isEqualToString:@","]) {
            [target showSettings:nil];
            return nil;
        }
        return event;
    }];
}
