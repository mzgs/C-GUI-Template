#import <Cocoa/Cocoa.h>
#include "macos_menu.h"

@interface AppMenuTarget : NSObject
@property(nonatomic, assign) bool *settingsOpen;
- (void)showSettings:(id)sender;
@end

@implementation AppMenuTarget
- (void)showSettings:(id)sender {
    (void)sender;
    *self.settingsOpen = true;
}
@end

void app_menu_install(bool *settings_open) {
    static AppMenuTarget *target;
    if (target) return;
    NSMenu *menu = NSApp.mainMenu.itemArray.firstObject.submenu;
    if (!menu) return;

    target = [[AppMenuTarget alloc] init];
    target.settingsOpen = settings_open;
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"Settings…"
        action:@selector(showSettings:) keyEquivalent:@","];
    item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    item.target = target;
    [menu insertItem:item atIndex:0];
    [menu insertItem:[NSMenuItem separatorItem] atIndex:1];
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
