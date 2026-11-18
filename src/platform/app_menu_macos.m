#import <Cocoa/Cocoa.h>
#include <cui/cui_app.h>
#include "app_menu.h"

static bool combo_equal(cui_key_combo a, cui_key_combo b) {
    return a.key == b.key && a.mods == b.mods;
}

static NSString *macos_key_equivalent(cui_key key, bool *is_function_key) {
    *is_function_key = false;
    if (key >= CUI_KEY_A && key <= CUI_KEY_Z) {
        unichar ch = (unichar)('a' + (key - CUI_KEY_A));
        return [NSString stringWithCharacters:&ch length:1];
    }
    if (key >= CUI_KEY_0 && key <= CUI_KEY_9) {
        unichar ch = (unichar)('0' + (key - CUI_KEY_0));
        return [NSString stringWithCharacters:&ch length:1];
    }
    if (key >= CUI_KEY_F1 && key <= CUI_KEY_F12) {
        *is_function_key = true;
        unichar ch = (unichar)(NSF1FunctionKey + (key - CUI_KEY_F1));
        return [NSString stringWithCharacters:&ch length:1];
    }
    unichar ch = 0;
    switch (key) {
    case CUI_KEY_TAB: ch = '\t'; break;
    case CUI_KEY_ENTER: ch = '\r'; break;
    case CUI_KEY_ESCAPE: ch = 0x1b; break;
    case CUI_KEY_BACKSPACE: ch = NSBackspaceCharacter; break;
    case CUI_KEY_DELETE: ch = NSDeleteFunctionKey; *is_function_key = true; break;
    case CUI_KEY_SPACE: ch = ' '; break;
    case CUI_KEY_LEFT: ch = NSLeftArrowFunctionKey; *is_function_key = true; break;
    case CUI_KEY_RIGHT: ch = NSRightArrowFunctionKey; *is_function_key = true; break;
    case CUI_KEY_UP: ch = NSUpArrowFunctionKey; *is_function_key = true; break;
    case CUI_KEY_DOWN: ch = NSDownArrowFunctionKey; *is_function_key = true; break;
    case CUI_KEY_HOME: ch = NSHomeFunctionKey; *is_function_key = true; break;
    case CUI_KEY_END: ch = NSEndFunctionKey; *is_function_key = true; break;
    case CUI_KEY_PAGE_UP: ch = NSPageUpFunctionKey; *is_function_key = true; break;
    case CUI_KEY_PAGE_DOWN: ch = NSPageDownFunctionKey; *is_function_key = true; break;
    case CUI_KEY_MINUS: ch = '-'; break;
    case CUI_KEY_EQUAL: ch = '='; break;
    case CUI_KEY_COMMA: ch = ','; break;
    case CUI_KEY_PERIOD: ch = '.'; break;
    case CUI_KEY_SLASH: ch = '/'; break;
    default: return @"";
    }
    return [NSString stringWithCharacters:&ch length:1];
}

static void macos_apply_key_combo(NSMenuItem *item, cui_key_combo combo) {
    if (!item) return;
    if (combo.key == CUI_KEY_NONE) {
        item.keyEquivalent = @"";
        item.keyEquivalentModifierMask = 0;
        return;
    }
    bool is_fn = false;
    NSString *equiv = macos_key_equivalent(combo.key, &is_fn);
    if (!equiv.length ||
        (!is_fn && !(combo.mods & (CUI_MOD_SUPER | CUI_MOD_CTRL | CUI_MOD_ALT)))) {
        item.keyEquivalent = @"";
        item.keyEquivalentModifierMask = 0;
        return;
    }
    NSEventModifierFlags mask = 0;
    if (combo.mods & CUI_MOD_SUPER) mask |= NSEventModifierFlagCommand;
    if (combo.mods & CUI_MOD_CTRL) mask |= NSEventModifierFlagControl;
    if (combo.mods & CUI_MOD_ALT) mask |= NSEventModifierFlagOption;
    if (combo.mods & CUI_MOD_SHIFT) mask |= NSEventModifierFlagShift;
    item.keyEquivalent = equiv;
    item.keyEquivalentModifierMask = mask;
}

@interface AppMenuTarget : NSObject <NSMenuItemValidation>
@property(nonatomic, assign) bool *settingsOpen;
@property(nonatomic, assign) int *settingsSection;
@property(nonatomic, assign) int aboutSection;
@property(nonatomic, assign) bool *paletteOpen;
@property(nonatomic, assign) void *user;
@property(nonatomic, assign) void (*onAction)(app_action action, void *user);
@property(nonatomic, assign) void (*onEditKey)(cui_key key, void *user);
@property(nonatomic, assign) void (*onToggleFullscreen)(void *user);
@property(nonatomic, assign) void (*onWillTerminate)(void *user);
@property(nonatomic, strong) NSMenuItem *saveItem;
@property(nonatomic, strong) NSMenuItem *findItem;
@property(nonatomic, strong) NSMenuItem *paletteItem;
@property(nonatomic, strong) NSMenuItem *fullscreenItem;
@property(nonatomic, assign) bool hasSyncedState;
@property(nonatomic, assign) app_menu_state lastState;
- (void)showAbout:(id)sender;
- (void)showSettings:(id)sender;
- (void)triggerSave:(id)sender;
- (void)triggerFind:(id)sender;
- (void)triggerPalette:(id)sender;
- (void)toggleFullscreen:(id)sender;
- (void)editCut:(id)sender;
- (void)editCopy:(id)sender;
- (void)editPaste:(id)sender;
- (void)editSelectAll:(id)sender;
- (void)windowDidMove:(NSNotification *)note;
- (void)appWillTerminate:(NSNotification *)note;
@end

@implementation AppMenuTarget
- (void)applyHooks:(const app_menu_hooks *)hooks {
    self.settingsOpen = hooks->settings_open;
    self.settingsSection = hooks->settings_section;
    self.aboutSection = hooks->about_section;
    self.paletteOpen = hooks->palette_open;
    self.user = hooks->user;
    self.onAction = hooks->on_action;
    self.onEditKey = hooks->on_edit_key;
    self.onToggleFullscreen = hooks->on_toggle_fullscreen;
    self.onWillTerminate = hooks->on_will_terminate;
}

- (void)requestRedraw {
    cui_ctx *ui = cui_app_ui(NULL);
    if (ui) cui_request_redraw(ui);
}

- (void)showAbout:(id)sender {
    (void)sender;
    if (self.paletteOpen) *self.paletteOpen = false;
    if (self.settingsSection) *self.settingsSection = self.aboutSection;
    if (self.settingsOpen) *self.settingsOpen = true;
    [self requestRedraw];
}

- (void)showSettings:(id)sender {
    (void)sender;
    if (self.paletteOpen) *self.paletteOpen = false;
    if (self.settingsOpen) *self.settingsOpen = true;
    [self requestRedraw];
}

- (void)triggerSave:(id)sender {
    (void)sender;
    if (self.onAction) self.onAction(APP_ACTION_SAVE, self.user);
    [self requestRedraw];
}

- (void)triggerFind:(id)sender {
    (void)sender;
    if (self.onAction) self.onAction(APP_ACTION_FIND, self.user);
    [self requestRedraw];
}

- (void)triggerPalette:(id)sender {
    (void)sender;
    if (self.settingsOpen) *self.settingsOpen = false;
    if (self.paletteOpen) *self.paletteOpen = true;
    [self requestRedraw];
}

- (void)toggleFullscreen:(id)sender {
    (void)sender;
    if (self.onToggleFullscreen) self.onToggleFullscreen(self.user);
    [self requestRedraw];
}

- (void)editCut:(id)sender {
    (void)sender;
    if (self.onEditKey) self.onEditKey(CUI_KEY_X, self.user);
    [self requestRedraw];
}

- (void)editCopy:(id)sender {
    (void)sender;
    if (self.onEditKey) self.onEditKey(CUI_KEY_C, self.user);
    [self requestRedraw];
}

- (void)editPaste:(id)sender {
    (void)sender;
    if (self.onEditKey) self.onEditKey(CUI_KEY_V, self.user);
    [self requestRedraw];
}

- (void)editSelectAll:(id)sender {
    (void)sender;
    if (self.onEditKey) self.onEditKey(CUI_KEY_A, self.user);
    [self requestRedraw];
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(triggerSave:) || item.action == @selector(triggerFind:)) {
        bool blocked = (self.settingsOpen && *self.settingsOpen) ||
                       (self.paletteOpen && *self.paletteOpen);
        return !blocked;
    }
    return YES;
}

- (void)windowDidMove:(NSNotification *)note {
    void *native = cui_app_native_window(NULL);
    if (!native || note.object != (__bridge id)native) return;
    [self requestRedraw];
}

- (void)appWillTerminate:(NSNotification *)note {
    (void)note;
    if (self.onWillTerminate) self.onWillTerminate(self.user);
}
@end

static AppMenuTarget *shared_menu_target;

static NSMenuItem *make_item(NSString *title, SEL action, NSString *equiv,
                             NSEventModifierFlags mask, id target) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:equiv];
    item.keyEquivalentModifierMask = mask;
    item.target = target;
    return item;
}

static NSMenuItem *make_submenu_item(NSString *title, NSMenu *submenu) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    item.submenu = submenu;
    return item;
}

void app_menu_install(const app_menu_hooks *hooks) {
    if (!hooks || !hooks->settings_open) return;
    if (shared_menu_target) {
        [shared_menu_target applyHooks:hooks];
        return;
    }
    NSMenu *bar = NSApp.mainMenu;
    NSMenu *appMenu = bar.itemArray.firstObject.submenu;
    if (!bar || !appMenu) return;

    AppMenuTarget *target = [[AppMenuTarget alloc] init];
    [target applyHooks:hooks];
    shared_menu_target = target;

    NSMenuItem *about = make_item([@"About " stringByAppendingString:@APP_NAME],
        @selector(showAbout:), @"", 0, target);
    NSMenuItem *settings = make_item(@"Settings…",
        @selector(showSettings:), @",", NSEventModifierFlagCommand, target);

    [appMenu insertItem:about atIndex:0];
    [appMenu insertItem:[NSMenuItem separatorItem] atIndex:1];
    [appMenu insertItem:settings atIndex:2];
    [appMenu insertItem:[NSMenuItem separatorItem] atIndex:3];

    /* File menu */
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    target.saveItem = make_item(@"Save", @selector(triggerSave:), @"s",
                                NSEventModifierFlagCommand, target);
    [fileMenu addItem:target.saveItem];

    /* Edit menu */
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItem:make_item(@"Cut", @selector(editCut:), @"x",
                                NSEventModifierFlagCommand, target)];
    [editMenu addItem:make_item(@"Copy", @selector(editCopy:), @"c",
                                NSEventModifierFlagCommand, target)];
    [editMenu addItem:make_item(@"Paste", @selector(editPaste:), @"v",
                                NSEventModifierFlagCommand, target)];
    [editMenu addItem:make_item(@"Select All", @selector(editSelectAll:), @"a",
                                NSEventModifierFlagCommand, target)];
    [editMenu addItem:[NSMenuItem separatorItem]];
    target.findItem = make_item(@"Find", @selector(triggerFind:), @"f",
                                NSEventModifierFlagCommand, target);
    [editMenu addItem:target.findItem];

    /* View menu */
    NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
    target.paletteItem = make_item(@"Command Palette…", @selector(triggerPalette:), @"k",
                                   NSEventModifierFlagCommand, target);
    [viewMenu addItem:target.paletteItem];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    target.fullscreenItem = make_item(@"Enter Full Screen", @selector(toggleFullscreen:), @"f",
                                      NSEventModifierFlagCommand | NSEventModifierFlagControl, target);
    [viewMenu addItem:target.fullscreenItem];

    NSInteger insertAt = bar.numberOfItems > 1 ? 1 : bar.numberOfItems;
    [bar insertItem:make_submenu_item(@"File", fileMenu) atIndex:insertAt];
    [bar insertItem:make_submenu_item(@"Edit", editMenu) atIndex:insertAt + 1];
    [bar insertItem:make_submenu_item(@"View", viewMenu) atIndex:insertAt + 2];

    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:target selector:@selector(windowDidMove:)
               name:NSWindowDidMoveNotification object:nil];
    [nc addObserver:target selector:@selector(appWillTerminate:)
               name:NSApplicationWillTerminateNotification object:nil];

    /* CUI consumes custom Command shortcuts before the native menu sees them. */
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *event) {
        if (!target.settingsOpen || *target.settingsOpen) return event;
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

void app_menu_sync(const app_menu_state *state) {
    if (!shared_menu_target || !state) return;
    AppMenuTarget *target = shared_menu_target;
    if (!target.hasSyncedState || !combo_equal(target.lastState.save_combo, state->save_combo)) {
        macos_apply_key_combo(target.saveItem, state->save_combo);
    }
    if (!target.hasSyncedState || !combo_equal(target.lastState.find_combo, state->find_combo)) {
        macos_apply_key_combo(target.findItem, state->find_combo);
    }
    if (!target.hasSyncedState || !combo_equal(target.lastState.palette_combo, state->palette_combo)) {
        macos_apply_key_combo(target.paletteItem, state->palette_combo);
    }
    if (!target.hasSyncedState || target.lastState.fullscreen != state->fullscreen) {
        target.fullscreenItem.title = state->fullscreen ? @"Exit Full Screen" : @"Enter Full Screen";
    }
    target.lastState = *state;
    target.hasSyncedState = true;
}
