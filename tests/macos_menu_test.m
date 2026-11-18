#import <Cocoa/Cocoa.h>
#define main template_main
#include "../main.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include <math.h>

static void press_settings_key(cui_ctx *ui, cui_key key, int mods) {
    cui_input_key(ui, key, true, mods);
    cui_begin_frame(ui, 1.0f / 60);
    frame(ui, NULL);
    cui_end_frame(ui);
}

static void check_settings_size(float width, float height, cui_theme_mode theme) {
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, width, height, 1);
    cui_set_reduced_motion(ui, true);
    settings_open = true;
    settings_section = 0;
    cui_begin_frame(ui, 1.0f / 60);
    frame(ui, NULL);
    cui_end_frame(ui);
    const cui_draw_data *draw = cui_get_draw_data(ui);
    bool found_panel = false;
    for (int i = 0; i < draw->vertex_count; ++i) {
        const cui_vertex *v = &draw->vertices[i];
        if (v->mode == CUI_SHADE_RRECT && v->border == 1 &&
            fabsf(v->hw * 2 - width * 0.9f) < 0.1f &&
            fabsf(v->hh * 2 - fminf(height * 0.9f, height - 48)) < 0.1f)
            found_panel = true;
    }
    assert(found_panel);
    press_settings_key(ui, CUI_KEY_TAB, 0);
    press_settings_key(ui, CUI_KEY_TAB, 0);
    press_settings_key(ui, CUI_KEY_TAB, 0);
    press_settings_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == 1);
    press_settings_key(ui, CUI_KEY_TAB, 0);
    press_settings_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == 2);
    press_settings_key(ui, CUI_KEY_TAB, CUI_MOD_SHIFT);
    press_settings_key(ui, CUI_KEY_TAB, CUI_MOD_SHIFT);
    press_settings_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == 0);
    cui_destroy(ui);
    settings_open = false;
}

int main(void) {
    @autoreleasepool {
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
        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        [NSApp sendEvent:shortcut];
        frame(cui_app_ui(app), NULL);
        assert(settings_open);

        cui_request_redraw(cui_app_ui(app));
        assert(cui_app_frame(app));
        cui_input_key(cui_app_ui(app), CUI_KEY_ESCAPE, true, 0);
        frame(cui_app_ui(app), NULL);
        assert(!settings_open);
        cui_app_destroy(app);
        check_settings_size(900, 600, CUI_THEME_LIGHT);
        check_settings_size(360, 280, CUI_THEME_LIGHT);
        check_settings_size(900, 600, CUI_THEME_DARK);
        check_settings_size(360, 280, CUI_THEME_DARK);
    }
    return 0;
}
