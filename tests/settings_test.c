#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#define main template_main
#include "../main.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *test_settings_path;
static const char *mock_dialog_path;
static cui_file_dialog_kind mock_dialog_last_kind;
static const char *mock_dialog_last_filter;
static const char *mock_dialog_last_default_name;
static int mock_dialog_calls;

static bool mock_file_dialog(const cui_file_dialog_opts *opts, char *out, size_t cap) {
    ++mock_dialog_calls;
    mock_dialog_last_kind = opts->kind;
    mock_dialog_last_filter = opts->filter;
    mock_dialog_last_default_name = opts->default_name;
    if (!mock_dialog_path) return false;
    int len = snprintf(out, cap, "%s", mock_dialog_path);
    return len >= 0 && (size_t)len < cap;
}

static void draw_settings(cui_ctx *ui) {
    cui_begin_frame(ui, 1.0f / 60);
    frame(ui, (void *)test_settings_path);
    cui_end_frame(ui);
}

static void press_key(cui_ctx *ui, cui_key key, int mods) {
    cui_input_key(ui, key, true, mods);
    draw_settings(ui);
    cui_input_key(ui, key, false, 0);
    cui_input_key(ui, CUI_KEY_NONE, false, 0);
}

static void write_text_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs(content, file) >= 0);
    assert(fclose(file) == 0);
}

static void read_text_file(const char *path, char *buf, size_t cap) {
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t n = fread(buf, 1, cap - 1, file);
    assert(!ferror(file));
    buf[n] = '\0';
    fclose(file);
}

static void check_settings_layout(float width, float height, cui_theme_mode theme) {
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, width, height, 1);
    cui_set_reduced_motion(ui, true);
    settings_open = true;
    settings_section = 0;
    draw_settings(ui);
    const cui_draw_data *draw = cui_get_draw_data(ui);
    bool found_panel = false;
    float dialog_right = width * 0.95f + 1.0f;
    int input_boxes = 0;
    float last_cy = -1.0f;
    for (int i = 0; i < draw->vertex_count; ++i) {
        const cui_vertex *v = &draw->vertices[i];
        if (v->mode != CUI_SHADE_RRECT || v->border != 1) continue;
        float w = v->hw * 2, h = v->hh * 2;
        if (fabsf(w - width * 0.9f) < 0.1f &&
            fabsf(h - fminf(height * 0.9f, height - 48)) < 0.1f)
            found_panel = true;
        float cx = v->x - v->lx, cy = v->y - v->ly;
        if (w >= 80.0f && h >= 28.0f && h <= 44.0f && cx + v->hw <= dialog_right &&
            fabsf(cy - last_cy) > 1.0f) {
            ++input_boxes;
            last_cy = cy;
        }
    }
    assert(found_panel);
    assert(input_boxes >= (height >= 360.0f ? 2 : 1));

    press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == 1);
    press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == 2);
    press_key(ui, CUI_KEY_TAB, CUI_MOD_SHIFT);
    press_key(ui, CUI_KEY_TAB, CUI_MOD_SHIFT);
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == 0);
    cui_destroy(ui);
    settings_open = false;
}

static void check_general_autosave(cui_theme_mode theme) {
    remove(test_settings_path);
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, 900, 600, 1);
    cui_set_reduced_motion(ui, true);
    settings_open = true;
    settings_section = 0;
    settings_dirty = false;
    app_settings original = settings;
    draw_settings(ui);
    /* Close, three sidebar items, then the text field and the number field. */
    for (int i = 0; i < 5; ++i) press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_A, CUI_MOD_PRIMARY);
    const char *edited = "Café \"settings\" \\";
    cui_input_text(ui, edited);
    draw_settings(ui);
    /* Typing marks dirty without rewriting disk on every keystroke. */
    assert(settings_dirty);
    FILE *probe = fopen(test_settings_path, "rb");
    assert(!probe);

    /* Tabbing to the number field blurs Example string and flushes it to disk. */
    press_key(ui, CUI_KEY_TAB, 0);
    assert(!settings_dirty);
    app_settings reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(strcmp(reloaded.example_string, edited) == 0);
    assert(reloaded.example_number == original.example_number);

    press_key(ui, CUI_KEY_UP, 0);
    reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(strcmp(reloaded.example_string, edited) == 0);
    assert(reloaded.example_number == original.example_number + 1);

    /* Closing the dialog flushes an unblurred text edit. */
    press_key(ui, CUI_KEY_TAB, CUI_MOD_SHIFT);
    press_key(ui, CUI_KEY_A, CUI_MOD_PRIMARY);
    cui_input_text(ui, "Flushed on close");
    draw_settings(ui);
    assert(settings_dirty);
    settings_open = false;
    draw_settings(ui);
    assert(!settings_dirty);
    reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(strcmp(reloaded.example_string, "Flushed on close") == 0);

    settings = original;
    cui_destroy(ui);
    settings_open = false;
    remove(test_settings_path);
}

static void check_keybinding_edit(cui_theme_mode theme) {
    remove(test_settings_path);
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
    for (int i = 0; i < 7; ++i) press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_ENTER, 0);
    press_key(ui, CUI_KEY_K, CUI_MOD_PRIMARY | CUI_MOD_SHIFT);
    assert(settings.keybindings[1].combo.key == CUI_KEY_K);

    /* Keybindings are stored under a "keybindings" object using stable key/modifier names. */
    char json[1024];
    read_text_file(test_settings_path, json, sizeof json);
    assert(strstr(json, "\"keybindings\": {") != NULL);
    assert(strstr(json, "\"save\": {") != NULL);
    assert(strstr(json, "\"key\": \"K\"") != NULL);
    assert(strstr(json, "\"save_key\"") == NULL);

    app_settings reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(reloaded.keybindings[1].combo.key == CUI_KEY_K);
    assert(reloaded.keybindings[1].combo.mods == (CUI_MOD_PRIMARY | CUI_MOD_SHIFT));
    assert(reloaded.keybindings[0].combo.key == CUI_KEY_F);

    press_key(ui, CUI_KEY_ENTER, 0);
    press_key(ui, CUI_KEY_ESCAPE, 0);
    assert(settings_open);
    assert(settings.keybindings[1].combo.key == CUI_KEY_K);

    press_key(ui, CUI_KEY_ENTER, 0);
    press_key(ui, CUI_KEY_BACKSPACE, 0);
    assert(settings.keybindings[1].combo.key == CUI_KEY_NONE);
    reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(reloaded.keybindings[1].combo.key == CUI_KEY_NONE);
    assert(reloaded.keybindings[1].combo.mods == 0);

    settings = original;
    keybinding_filter[0] = '\0';
    cui_destroy(ui);
    settings_open = false;
    remove(test_settings_path);
}

static void check_load_validation_and_compat(const char *path) {
    app_settings original = settings;
    write_text_file(path,
        "{\"example_number\":true,\"example_string\":\"Loaded\","
        "\"keybindings\":{\"find\":{\"key\":\"NotAKey\",\"mods\":\"ctrl\"},"
        "\"save\":{\"key\":-1,\"mods\":0}}}");
    app_settings loaded = original;
    assert(app_settings_load(&loaded, path));
    assert(loaded.example_number == 42);
    assert(strcmp(loaded.example_string, "Loaded") == 0);
    assert(loaded.keybindings[0].combo.key == CUI_KEY_F);
    assert(loaded.keybindings[1].combo.key == CUI_KEY_S);

    /* Nested keybindings object (including optional omitted mods) loads cleanly. */
    write_text_file(path,
        "{\"keybindings\":{\"find\":{\"key\":\"H\"},"
        "\"save\":{\"key\":\"L\",\"mods\":\"ctrl alt\"}}}");
    loaded = original;
    assert(app_settings_load(&loaded, path));
    assert(loaded.keybindings[0].combo.key == CUI_KEY_H);
    assert(loaded.keybindings[0].combo.mods == 0);
    assert(loaded.keybindings[1].combo.key == CUI_KEY_L);
    assert(loaded.keybindings[1].combo.mods == (CUI_MOD_CTRL | CUI_MOD_ALT));

    /* Legacy flat and numeric keybinding formats remain readable. */
    char legacy[256];
    snprintf(legacy, sizeof legacy,
             "{\"find_key\":%d,\"find_mods\":%d,\"save_key\":\"J\",\"save_mods\":\"ctrl alt\"}",
             CUI_KEY_G, CUI_MOD_SHIFT | CUI_MOD_CTRL);
    write_text_file(path, legacy);
    loaded = original;
    assert(app_settings_load(&loaded, path));
    assert(loaded.keybindings[0].combo.key == CUI_KEY_G);
    assert(loaded.keybindings[0].combo.mods == (CUI_MOD_SHIFT | CUI_MOD_CTRL));
    assert(loaded.keybindings[1].combo.key == CUI_KEY_J);
    assert(loaded.keybindings[1].combo.mods == (CUI_MOD_CTRL | CUI_MOD_ALT));

    /* Malformed, empty, non-object, or unrelated JSON files fail without mutating settings. */
    const char *invalid_cases[] = { "{not json", "[1,2,3]", "{}", "{\"unrelated\":123}" };
    for (size_t i = 0; i < sizeof invalid_cases / sizeof invalid_cases[0]; ++i) {
        write_text_file(path, invalid_cases[i]);
        loaded = original;
        assert(!app_settings_load(&loaded, path));
        assert(strcmp(loaded.example_string, original.example_string) == 0);
        assert(loaded.example_number == original.example_number);
    }
    assert(!app_settings_load(&loaded, ""));
    remove(path);
}

static void check_import_export(const char *base_dir, cui_theme_mode theme) {
    char export_path[4096];
    char bad_path[4096];
    int len = snprintf(export_path, sizeof export_path, "%s/exported.json", base_dir);
    assert(len >= 0 && (size_t)len < sizeof export_path);
    len = snprintf(bad_path, sizeof bad_path, "%s/bad.json", base_dir);
    assert(len >= 0 && (size_t)len < sizeof bad_path);
    remove(test_settings_path);
    remove(export_path);
    remove(bad_path);

    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, 900, 600, 1);
    cui_set_reduced_motion(ui, true);
    app_settings original = settings;
    settings_open = true;
    settings_section = 0;
    settings_dirty = false;
    settings_status = NULL;
    settings_file_dialog = mock_file_dialog;

    strcpy(settings.example_string, "Exported Value");
    settings.example_number = 84;
    settings.keybindings[0].combo = (cui_key_combo){ CUI_MOD_CTRL | CUI_MOD_SHIFT, CUI_KEY_G };

    draw_settings(ui);
    /* Close, 3 sidebar items, Example string, Example number (+2 steppers), Import..., Export... */
    for (int i = 0; i < 10; ++i) press_key(ui, CUI_KEY_TAB, 0);

    /* Export to chosen path. */
    mock_dialog_calls = 0;
    mock_dialog_path = export_path;
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(mock_dialog_calls == 1);
    assert(mock_dialog_last_kind == CUI_FILE_SAVE);
    assert(strcmp(mock_dialog_last_filter, "json") == 0);
    assert(strcmp(mock_dialog_last_default_name, "settings.json") == 0);
    assert(settings_status && strcmp(settings_status, "Settings exported.") == 0);

    /* Change in-memory settings, then cancel Import: nothing changes. */
    settings = original;
    press_key(ui, CUI_KEY_TAB, CUI_MOD_SHIFT);
    mock_dialog_path = NULL;
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(mock_dialog_calls == 2);
    assert(mock_dialog_last_kind == CUI_FILE_OPEN);
    assert(strcmp(settings.example_string, original.example_string) == 0);
    assert(settings.example_number == original.example_number);

    /* Import from exported file: updates state and persists to per-user settings. */
    mock_dialog_path = export_path;
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(mock_dialog_calls == 3);
    assert(mock_dialog_last_kind == CUI_FILE_OPEN);
    assert(strcmp(mock_dialog_last_filter, "json") == 0);
    assert(strcmp(settings.example_string, "Exported Value") == 0);
    assert(settings.example_number == 84);
    assert(settings.keybindings[0].combo.key == CUI_KEY_G);
    assert(settings.keybindings[0].combo.mods == (CUI_MOD_CTRL | CUI_MOD_SHIFT));
    assert(settings_status && strcmp(settings_status, "Settings imported.") == 0);

    app_settings persisted = original;
    assert(app_settings_load(&persisted, test_settings_path));
    assert(strcmp(persisted.example_string, "Exported Value") == 0);
    assert(persisted.example_number == 84);
    assert(persisted.keybindings[0].combo.key == CUI_KEY_G);

    /* Import invalid JSON: leaves settings intact and reports error. */
    write_text_file(bad_path, "{broken");
    mock_dialog_path = bad_path;
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(strcmp(settings.example_string, "Exported Value") == 0);
    assert(settings.example_number == 84);
    assert(settings_status && strstr(settings_status, "import") != NULL);

    /* Closing settings clears status. */
    settings_open = false;
    draw_settings(ui);
    assert(settings_status == NULL);

    settings_file_dialog = default_file_dialog;
    settings = original;
    cui_destroy(ui);
    remove(test_settings_path);
    remove(export_path);
    remove(bad_path);
}

static void check_override_paths(const char *base_dir) {
    app_settings original = settings;
    char nested[4096];
    int len = snprintf(nested, sizeof nested, "%s/nested/sub/settings.json", base_dir);
    assert(len >= 0 && (size_t)len < sizeof nested);
    remove(nested);
    app_settings saved = original;
    saved.example_number = 77;
    assert(app_settings_save(&saved, nested));
    app_settings loaded = original;
    app_settings_load(&loaded, nested);
    assert(loaded.example_number == 77);
    remove(nested);

    /* Bare filename with no directory separator works on all platforms. */
    const char *bare = "settings-bare-test.json";
    remove(bare);
    saved.example_number = 88;
    assert(app_settings_save(&saved, bare));
    loaded = original;
    app_settings_load(&loaded, bare);
    assert(loaded.example_number == 88);
    assert(remove(bare) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(contains_ignore_case("Find", ""));
    assert(contains_ignore_case("", ""));
    assert(contains_ignore_case("Save", "sAv"));
    assert(contains_ignore_case("Cmd Shift K", "shift k"));
    assert(!contains_ignore_case("Find", "Finding"));
    assert(!contains_ignore_case("", "Find"));
    assert(!contains_ignore_case("Find", "xyz"));

    char path[4096];
    int len = snprintf(path, sizeof path, "%s/settings.json", argv[1]);
    assert(len >= 0 && (size_t)len < sizeof path);
    test_settings_path = path;
    remove(path);

    assert(!app_settings_load(&settings, test_settings_path));
    assert(settings.example_number == 42);

    check_settings_layout(900, 600, CUI_THEME_LIGHT);
    check_settings_layout(360, 280, CUI_THEME_LIGHT);
    check_settings_layout(360, 380, CUI_THEME_LIGHT);
    check_settings_layout(900, 600, CUI_THEME_DARK);
    check_settings_layout(360, 280, CUI_THEME_DARK);
    check_settings_layout(360, 380, CUI_THEME_DARK);
    check_general_autosave(CUI_THEME_LIGHT);
    check_general_autosave(CUI_THEME_DARK);
    check_keybinding_edit(CUI_THEME_LIGHT);
    check_keybinding_edit(CUI_THEME_DARK);
    check_load_validation_and_compat(path);
    check_import_export(argv[1], CUI_THEME_LIGHT);
    check_import_export(argv[1], CUI_THEME_DARK);
    check_override_paths(argv[1]);

#ifndef __APPLE__
    {
        cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = CUI_THEME_LIGHT });
        assert(ui);
        cui_set_viewport(ui, 900, 600, 1);
        cui_set_reduced_motion(ui, true);
        settings_open = false;
        press_key(ui, CUI_KEY_NONE, 0);
        press_key(ui, CUI_KEY_COMMA, CUI_MOD_PRIMARY);
        assert(settings_open);
        cui_destroy(ui);
        settings_open = false;
    }
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
    {
        assert(argv[1][0] == '/');
        assert(setenv("XDG_CONFIG_HOME", argv[1], 1) == 0);
        char xdg_path[4096];
        len = snprintf(xdg_path, sizeof xdg_path, "%s/%s/settings.json", argv[1], APP_NAME);
        assert(len >= 0 && (size_t)len < sizeof xdg_path);
        remove(xdg_path);
        app_settings saved = settings;
        saved.example_number = 55;
        assert(app_settings_save(&saved, NULL));
        app_settings loaded = settings;
        app_settings_load(&loaded, NULL);
        assert(loaded.example_number == 55);
        assert(remove(xdg_path) == 0);
    }
#endif
    return 0;
}
