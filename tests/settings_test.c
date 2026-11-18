#define main template_main
#include "../main.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *settings_override;

static void press_key(cui_ctx *ui, cui_key key, int mods) {
    cui_input_key(ui, key, true, mods);
    cui_begin_frame(ui, 1.0f / 60);
    frame(ui, settings_override);
    cui_end_frame(ui);
    cui_input_key(ui, key, false, 0);
}

int main(int argc, char **argv) {
    char test_path[4096] = "settings-test/settings.json";
    settings_override = test_path;
#ifndef _WIN32
    assert(argc == 2 && argv[1][0] == '/');
    assert(setenv("XDG_CONFIG_HOME", argv[1], 1) == 0);
    int len = snprintf(test_path, sizeof test_path, "%s/%s/settings.json", argv[1], APP_NAME);
    assert(len >= 0 && (size_t)len < sizeof test_path);
    settings_override = NULL;
#else
    (void)argc;
    (void)argv;
#endif
    remove(test_path);
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = CUI_THEME_LIGHT });
    assert(ui);
    cui_set_viewport(ui, 900, 600, 1);
    cui_set_reduced_motion(ui, true);
    press_key(ui, CUI_KEY_NONE, 0);
    press_key(ui, CUI_KEY_COMMA, CUI_MOD_PRIMARY);
    assert(settings_open);

    app_settings reloaded = settings;
    /* Close, three sidebar items, the text field, then the number field. */
    for (int i = 0; i < 6; ++i) press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_UP, 0);
    app_settings_load(&reloaded, settings_override);
    assert(reloaded.example_number == 43);
    assert(strcmp(reloaded.example_string, "Example") == 0);
    assert(reloaded.keybindings[0].combo.key == CUI_KEY_F);
    cui_destroy(ui);
    assert(remove(test_path) == 0);
    return 0;
}
