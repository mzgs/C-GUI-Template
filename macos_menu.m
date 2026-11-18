#import <Cocoa/Cocoa.h>
#include <cui/cui_app.h>
#include "macos_menu.h"

@interface AppMenuTarget : NSObject
@property(nonatomic, assign) bool *settingsOpen;
@property(nonatomic, assign) int *settingsSection;
@property(nonatomic, assign) int aboutSection;
@property(nonatomic, assign) bool *paletteOpen;
- (void)showAbout:(id)sender;
- (void)showSettings:(id)sender;
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
