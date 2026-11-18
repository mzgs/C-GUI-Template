#import <Cocoa/Cocoa.h>
#define main template_main
#include "../main.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <string.h>

static const char *test_settings_path;

static void draw_settings(cui_ctx *ui) {
    cui_begin_frame(ui, 1.0f / 60);
    frame(ui, (void *)test_settings_path);
    cui_end_frame(ui);
}

static void press_settings_key(cui_ctx *ui, cui_key key, int mods) {
    cui_input_key(ui, key, true, mods);
    draw_settings(ui);
}

static void check_settings_size(float width, float height, cui_theme_mode theme) {
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, width, height, 1);
    cui_set_reduced_motion(ui, true);
    settings_open = true;
    settings_section = 0;
    draw_settings(ui);
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

static void check_general_autosave(cui_theme_mode theme) {
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, 900, 600, 1);
    cui_set_reduced_motion(ui, true);
    settings_open = true;
    settings_section = 0;
    app_settings original = settings;
    draw_settings(ui);
    /* Close, three sidebar items, then the text field and the number field. */
    for (int i = 0; i < 5; ++i) press_settings_key(ui, CUI_KEY_TAB, 0);
    press_settings_key(ui, CUI_KEY_A, CUI_MOD_PRIMARY);
    const char *edited = "Café \"settings\" \\";
    cui_input_key(ui, CUI_KEY_NONE, false, 0);
    cui_input_text(ui, edited);
    draw_settings(ui);
    app_settings reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(strcmp(reloaded.example_string, edited) == 0);
    assert(reloaded.example_number == original.example_number);

    press_settings_key(ui, CUI_KEY_TAB, 0);
    press_settings_key(ui, CUI_KEY_UP, 0);
    reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(strcmp(reloaded.example_string, edited) == 0);
    assert(reloaded.example_number == original.example_number + 1);
    settings = original;
    cui_destroy(ui);
    settings_open = false;
}

static void check_keybinding_edit(cui_theme_mode theme) {
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, 900, 600, 1);
    cui_set_reduced_motion(ui, true);
    settings_open = true;
    settings_section = 1;
    strcpy(keybinding_filter, "sAv");
    app_settings original = settings;
    draw_settings(ui);
    /* Close, three sidebar items, filter and its clear button, then the recorder. */
    for (int i = 0; i < 7; ++i) press_settings_key(ui, CUI_KEY_TAB, 0);
    press_settings_key(ui, CUI_KEY_ENTER, 0);
    press_settings_key(ui, CUI_KEY_K, CUI_MOD_PRIMARY | CUI_MOD_SHIFT);
    assert(settings.keybindings[1].combo.key == CUI_KEY_K);
    app_settings reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(reloaded.keybindings[1].combo.key == CUI_KEY_K);
    assert(reloaded.keybindings[1].combo.mods == (CUI_MOD_PRIMARY | CUI_MOD_SHIFT));
    assert(reloaded.keybindings[0].combo.key == CUI_KEY_F);
    press_settings_key(ui, CUI_KEY_ENTER, 0);
    press_settings_key(ui, CUI_KEY_ESCAPE, 0);
    assert(settings_open);
    assert(settings.keybindings[1].combo.key == CUI_KEY_K);
    press_settings_key(ui, CUI_KEY_ENTER, 0);
    press_settings_key(ui, CUI_KEY_BACKSPACE, 0);
    assert(settings.keybindings[1].combo.key == CUI_KEY_NONE);
    reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(reloaded.keybindings[1].combo.key == CUI_KEY_NONE);
    assert(reloaded.keybindings[1].combo.mods == 0);
    settings = original;
    keybinding_filter[0] = '\0';
    cui_destroy(ui);
    settings_open = false;
}

int main(void) {
    assert(contains_ignore_case("Find", ""));
    assert(contains_ignore_case("", ""));
    assert(contains_ignore_case("Save", "sAv"));
    assert(contains_ignore_case("Cmd Shift K", "shift k"));
    assert(!contains_ignore_case("Find", "Finding"));
    assert(!contains_ignore_case("", "Find"));
    assert(!contains_ignore_case("Find", "xyz"));
    @autoreleasepool {
        NSString *folder = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
        NSString *path = [folder stringByAppendingPathComponent:@"settings.json"];
        test_settings_path = path.UTF8String;
        app_settings_load(&settings, test_settings_path);
        assert(settings.example_number == 42);
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
        assert(![[NSFileManager defaultManager] fileExistsAtPath:path]);
        check_general_autosave(CUI_THEME_LIGHT);
        check_general_autosave(CUI_THEME_DARK);
        check_keybinding_edit(CUI_THEME_LIGHT);
        check_keybinding_edit(CUI_THEME_DARK);

        assert([@"{\"example_number\":true,\"example_string\":\"Loaded\",\"save_key\":-1,\"save_mods\":0}"
            writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL]);
        app_settings_load(&settings, test_settings_path);
        assert(settings.example_number == 42);
        assert(strcmp(settings.example_string, "Loaded") == 0);
        assert(settings.keybindings[1].combo.key == CUI_KEY_S);
        assert([[NSFileManager defaultManager] removeItemAtPath:folder error:NULL]);
    }
    return 0;
}
