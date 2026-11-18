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

static char mock_opened_url[4096];
static int mock_open_url_calls;
static bool mock_open_url_result = true;

static int mock_system_dark_value = -1;

static int mock_system_dark(void) {
    return mock_system_dark_value;
}

static bool mock_file_dialog(const cui_file_dialog_opts *opts, char *out, size_t cap) {
    ++mock_dialog_calls;
    mock_dialog_last_kind = opts->kind;
    mock_dialog_last_filter = opts->filter;
    mock_dialog_last_default_name = opts->default_name;
    if (!mock_dialog_path) return false;
    int len = snprintf(out, cap, "%s", mock_dialog_path);
    return len >= 0 && (size_t)len < cap;
}

static bool mock_open_url(const char *url) {
    ++mock_open_url_calls;
    snprintf(mock_opened_url, sizeof mock_opened_url, "%s", url ? url : "");
    return mock_open_url_result;
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

static void reset_ui_state(void) {
    settings = default_settings;
    settings_open = false;
    prev_settings_open = false;
    palette_open = false;
    settings_save_failed = false;
    settings_dirty = false;
    window_dirty = false;
    settings_status = NULL;
    settings_section = SETTINGS_SECTION_GENERAL;
    keybinding_filter[0] = '\0';
    appearance_ui = NULL;
    appearance_force_motion = false;
    env_theme_override = false;
    env_accent_override = false;
    mock_system_dark_value = -1;
}

static void check_settings_layout(float width, float height, cui_theme_mode theme) {
    reset_ui_state();
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, width, height, 1);
    cui_set_reduced_motion(ui, true);
    settings_open = true;
    settings_section = SETTINGS_SECTION_GENERAL;
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

    /* Close, General, Appearance -> Enter selects Appearance. */
    press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == SETTINGS_SECTION_APPEARANCE);

    press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == SETTINGS_SECTION_KEYBINDINGS);

    press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == SETTINGS_SECTION_ABOUT);

    press_key(ui, CUI_KEY_TAB, CUI_MOD_SHIFT);
    press_key(ui, CUI_KEY_TAB, CUI_MOD_SHIFT);
    press_key(ui, CUI_KEY_TAB, CUI_MOD_SHIFT);
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(settings_section == SETTINGS_SECTION_GENERAL);
    cui_destroy(ui);
    reset_ui_state();
}

static void check_general_autosave(cui_theme_mode theme) {
    reset_ui_state();
    remove(test_settings_path);
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, 900, 600, 1);
    cui_set_reduced_motion(ui, true);
    settings_open = true;
    settings_section = SETTINGS_SECTION_GENERAL;
    app_settings original = settings;
    draw_settings(ui);
    /* Close, four sidebar items, then the text field and the number field. */
    for (int i = 0; i < 6; ++i) press_key(ui, CUI_KEY_TAB, 0);
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

    cui_destroy(ui);
    reset_ui_state();
    remove(test_settings_path);
}

static void check_appearance_settings(void) {
    reset_ui_state();
    remove(test_settings_path);
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = CUI_THEME_LIGHT });
    assert(ui);
    cui_set_viewport(ui, 900, 600, 1);

    /* System theme follows OS dark/light state dynamically. */
    mock_system_dark_value = 1;
    draw_settings(ui);
    assert(cui_get_theme_mode(ui) == CUI_THEME_DARK);
    mock_system_dark_value = 0;
    draw_settings(ui);
    assert(cui_get_theme_mode(ui) == CUI_THEME_LIGHT);

    /* Explicit appearance values apply and persist cleanly. */
    settings.theme_mode = APP_THEME_DARK;
    settings.accent = CUI_ACCENT_BLUE;
    settings.font_scale = 1.15f;
    settings.reduced_motion = true;
    appearance_force_motion = true;
    draw_settings(ui);
    assert(cui_get_theme_mode(ui) == CUI_THEME_DARK);
    assert(cui_get_accent(ui) == CUI_ACCENT_BLUE);
    assert(fabsf(cui_theme_get(ui)->font_scale - 1.15f) < 0.001f);
    assert(cui_theme_get(ui)->motion == 0.0f);

    /* Render Appearance section at normal and compact sizes. */
    settings_open = true;
    settings_section = SETTINGS_SECTION_APPEARANCE;
    draw_settings(ui);
    cui_set_viewport(ui, 360, 280, 1);
    draw_settings(ui);
    cui_set_viewport(ui, 900, 600, 1);

    /* Turning reduced motion back off restores animations. */
    settings.reduced_motion = false;
    appearance_force_motion = true;
    draw_settings(ui);
    assert(cui_theme_get(ui)->motion > 0.0f);

    /* Save and reload appearance + window state (including negative multi-monitor pos). */
    settings.window_has_pos = true;
    settings.window_x = -240;
    settings.window_y = 180;
    settings.window_width = 1120;
    settings.window_height = 740;
    settings.window_fullscreen = true;
    assert(app_settings_save(&settings, test_settings_path));
    char json[2048];
    read_text_file(test_settings_path, json, sizeof json);
    assert(strstr(json, "\"appearance\": {") != NULL);
    assert(strstr(json, "\"theme\": \"dark\"") != NULL);
    assert(strstr(json, "\"accent\": \"blue\"") != NULL);
    assert(strstr(json, "\"window\": {") != NULL);
    assert(strstr(json, "\"x\": -240") != NULL);
    assert(strstr(json, "\"y\": 180") != NULL);
    assert(strstr(json, "\"width\": 1120") != NULL);
    assert(strstr(json, "\"fullscreen\": true") != NULL);

    app_settings reloaded = default_settings;
    assert(app_settings_load(&reloaded, test_settings_path));
    assert(reloaded.theme_mode == APP_THEME_DARK);
    assert(reloaded.accent == CUI_ACCENT_BLUE);
    assert(fabsf(reloaded.font_scale - 1.15f) < 0.001f);
    assert(!reloaded.reduced_motion);
    assert(reloaded.window_has_pos);
    assert(reloaded.window_x == -240);
    assert(reloaded.window_y == 180);
    assert(reloaded.window_width == 1120);
    assert(reloaded.window_height == 740);
    assert(reloaded.window_fullscreen);

    /* When window_has_pos is false, x/y are omitted from JSON. */
    settings.window_has_pos = false;
    assert(app_settings_save(&settings, test_settings_path));
    read_text_file(test_settings_path, json, sizeof json);
    assert(strstr(json, "\"x\":") == NULL);
    assert(strstr(json, "\"y\":") == NULL);
    reloaded = default_settings;
    assert(app_settings_load(&reloaded, test_settings_path));
    assert(!reloaded.window_has_pos);

    cui_destroy(ui);
    reset_ui_state();
    remove(test_settings_path);
}

static void check_keybinding_edit_and_conflicts(cui_theme_mode theme) {
    reset_ui_state();
    remove(test_settings_path);
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = theme });
    assert(ui);
    cui_set_viewport(ui, 900, 600, 1);
    cui_set_reduced_motion(ui, true);
    settings_open = true;
    settings_section = SETTINGS_SECTION_KEYBINDINGS;
    strcpy(keybinding_filter, "sAv");
    app_settings original = settings;
    draw_settings(ui);
    /* Close, four sidebar items, filter and its clear button, then the recorder. */
    for (int i = 0; i < 8; ++i) press_key(ui, CUI_KEY_TAB, 0);
    press_key(ui, CUI_KEY_ENTER, 0);
    press_key(ui, CUI_KEY_J, CUI_MOD_PRIMARY | CUI_MOD_SHIFT);
    assert(settings.keybindings[APP_ACTION_SAVE].combo.key == CUI_KEY_J);

    /* Keybindings are stored under a "keybindings" object using stable key/modifier names. */
    char json[2048];
    read_text_file(test_settings_path, json, sizeof json);
    assert(strstr(json, "\"keybindings\": {") != NULL);
    assert(strstr(json, "\"save\": {") != NULL);
    assert(strstr(json, "\"key\": \"J\"") != NULL);
    assert(strstr(json, "\"save_key\"") == NULL);

    app_settings reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(reloaded.keybindings[APP_ACTION_SAVE].combo.key == CUI_KEY_J);
    assert(reloaded.keybindings[APP_ACTION_SAVE].combo.mods == (CUI_MOD_PRIMARY | CUI_MOD_SHIFT));
    assert(reloaded.keybindings[APP_ACTION_FIND].combo.key == CUI_KEY_F);

    press_key(ui, CUI_KEY_ENTER, 0);
    press_key(ui, CUI_KEY_ESCAPE, 0);
    assert(settings_open);
    assert(settings.keybindings[APP_ACTION_SAVE].combo.key == CUI_KEY_J);

    press_key(ui, CUI_KEY_ENTER, 0);
    press_key(ui, CUI_KEY_BACKSPACE, 0);
    assert(settings.keybindings[APP_ACTION_SAVE].combo.key == CUI_KEY_NONE);
    reloaded = original;
    app_settings_load(&reloaded, test_settings_path);
    assert(reloaded.keybindings[APP_ACTION_SAVE].combo.key == CUI_KEY_NONE);
    assert(reloaded.keybindings[APP_ACTION_SAVE].combo.mods == 0);

    /* Conflict detection: default has no conflicts, duplicate & reserved do. */
    settings = original;
    assert(app_keybinding_conflict(&settings, APP_ACTION_FIND) == APP_CONFLICT_NONE);
    assert(app_keybinding_conflict(&settings, APP_ACTION_SAVE) == APP_CONFLICT_NONE);
    assert(app_keybinding_conflict(&settings, APP_ACTION_COMMAND_PALETTE) == APP_CONFLICT_NONE);

    settings.keybindings[APP_ACTION_SAVE].combo = settings.keybindings[APP_ACTION_FIND].combo;
    assert(app_keybinding_conflict(&settings, APP_ACTION_FIND) == APP_ACTION_SAVE);
    assert(app_keybinding_conflict(&settings, APP_ACTION_SAVE) == APP_ACTION_FIND);

    settings.keybindings[APP_ACTION_COMMAND_PALETTE].combo = (cui_key_combo){ CUI_MOD_PRIMARY, CUI_KEY_COMMA };
    assert(app_keybinding_conflict(&settings, APP_ACTION_COMMAND_PALETTE) == APP_CONFLICT_SETTINGS);

    /* Rendering Keybindings with conflicts exercises alert + badges cleanly. */
    keybinding_filter[0] = '\0';
    draw_settings(ui);

    /* Cleared shortcut never conflicts. */
    settings.keybindings[APP_ACTION_FIND].combo = (cui_key_combo){ 0, CUI_KEY_NONE };
    settings.keybindings[APP_ACTION_SAVE].combo = (cui_key_combo){ 0, CUI_KEY_NONE };
    assert(app_keybinding_conflict(&settings, APP_ACTION_FIND) == APP_CONFLICT_NONE);
    assert(app_keybinding_conflict(&settings, APP_ACTION_SAVE) == APP_CONFLICT_NONE);

    cui_destroy(ui);
    reset_ui_state();
    remove(test_settings_path);
}

static void check_actions_and_command_palette(void) {
    reset_ui_state();
    remove(test_settings_path);
    cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = CUI_THEME_LIGHT });
    assert(ui);
    cui_set_viewport(ui, 900, 600, 1);
    cui_set_reduced_motion(ui, true);

    /* Find & Save shortcuts dispatch actions and emit toasts. */
    press_key(ui, CUI_KEY_F, CUI_MOD_PRIMARY);
    assert(settings_status && strcmp(settings_status, "Find triggered.") == 0);
    assert(settings_status_tone == CUI_TONE_INFO);

    press_key(ui, CUI_KEY_S, CUI_MOD_PRIMARY);
    assert(settings_status && strcmp(settings_status, "Save triggered.") == 0);
    assert(settings_status_tone == CUI_TONE_SUCCESS);

    /* Command Palette opens with its configured shortcut (default Cmd/Ctrl+K). */
    assert(!palette_open);
    press_key(ui, CUI_KEY_K, CUI_MOD_PRIMARY);
    assert(palette_open);

    /* Filter and pick "Theme: Dark" inside the command palette. */
    cui_input_text(ui, "Theme: Dark");
    draw_settings(ui);
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(!palette_open);
    assert(settings.theme_mode == APP_THEME_DARK);
    assert(cui_get_theme_mode(ui) == CUI_THEME_DARK);

    /* Rebinding Command Palette shortcut updates live trigger. */
    settings.keybindings[APP_ACTION_COMMAND_PALETTE].combo = (cui_key_combo){ CUI_MOD_PRIMARY | CUI_MOD_SHIFT, CUI_KEY_P };
    press_key(ui, CUI_KEY_K, CUI_MOD_PRIMARY);
    assert(!palette_open);
    press_key(ui, CUI_KEY_P, CUI_MOD_PRIMARY | CUI_MOD_SHIFT);
    assert(palette_open);
    press_key(ui, CUI_KEY_ESCAPE, 0);
    assert(!palette_open);

    cui_destroy(ui);
    reset_ui_state();
    remove(test_settings_path);
}

static void check_load_validation_and_compat(const char *path) {
    reset_ui_state();
    app_settings original = settings;
    write_text_file(path,
        "{\"example_number\":true,\"example_string\":\"Loaded\","
        "\"appearance\":{\"theme\":\"nope\",\"accent\":\"ultraviolet\",\"font_scale\":9.0,\"reduced_motion\":123},"
        "\"window\":{\"x\":999999,\"y\":10.5,\"width\":100,\"height\":99999,\"fullscreen\":\"yes\"},"
        "\"keybindings\":{\"find\":{\"key\":\"NotAKey\",\"mods\":\"ctrl\"},"
        "\"save\":{\"key\":-1,\"mods\":0}}}");
    app_settings loaded = original;
    assert(app_settings_load(&loaded, path));
    assert(loaded.example_number == 42);
    assert(strcmp(loaded.example_string, "Loaded") == 0);
    assert(loaded.theme_mode == original.theme_mode);
    assert(loaded.accent == original.accent);
    assert(loaded.font_scale == original.font_scale);
    assert(loaded.reduced_motion == original.reduced_motion);
    assert(!loaded.window_has_pos);
    assert(loaded.window_width == original.window_width);
    assert(loaded.window_height == original.window_height);
    assert(loaded.window_fullscreen == original.window_fullscreen);
    assert(loaded.keybindings[APP_ACTION_FIND].combo.key == CUI_KEY_F);
    assert(loaded.keybindings[APP_ACTION_SAVE].combo.key == CUI_KEY_S);

    /* Nested keybindings object (including optional omitted mods) loads cleanly. */
    write_text_file(path,
        "{\"keybindings\":{\"find\":{\"key\":\"H\"},"
        "\"save\":{\"key\":\"L\",\"mods\":\"ctrl alt\"}}}");
    loaded = original;
    assert(app_settings_load(&loaded, path));
    assert(loaded.keybindings[APP_ACTION_FIND].combo.key == CUI_KEY_H);
    assert(loaded.keybindings[APP_ACTION_FIND].combo.mods == 0);
    assert(loaded.keybindings[APP_ACTION_SAVE].combo.key == CUI_KEY_L);
    assert(loaded.keybindings[APP_ACTION_SAVE].combo.mods == (CUI_MOD_CTRL | CUI_MOD_ALT));

    /* Legacy flat and numeric keybinding formats remain readable. */
    char legacy[256];
    snprintf(legacy, sizeof legacy,
             "{\"find_key\":%d,\"find_mods\":%d,\"save_key\":\"J\",\"save_mods\":\"ctrl alt\"}",
             CUI_KEY_G, CUI_MOD_SHIFT | CUI_MOD_CTRL);
    write_text_file(path, legacy);
    loaded = original;
    assert(app_settings_load(&loaded, path));
    assert(loaded.keybindings[APP_ACTION_FIND].combo.key == CUI_KEY_G);
    assert(loaded.keybindings[APP_ACTION_FIND].combo.mods == (CUI_MOD_SHIFT | CUI_MOD_CTRL));
    assert(loaded.keybindings[APP_ACTION_SAVE].combo.key == CUI_KEY_J);
    assert(loaded.keybindings[APP_ACTION_SAVE].combo.mods == (CUI_MOD_CTRL | CUI_MOD_ALT));

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

static void check_import_export_open_and_reset(const char *base_dir, cui_theme_mode theme) {
    reset_ui_state();
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
    settings_section = SETTINGS_SECTION_GENERAL;
    settings_file_dialog = mock_file_dialog;
    settings_open_url = mock_open_url;

    strcpy(settings.example_string, "Exported Value");
    settings.example_number = 84;
    settings.theme_mode = APP_THEME_DARK;
    settings.accent = CUI_ACCENT_GREEN;
    settings.font_scale = 1.20f;
    settings.keybindings[APP_ACTION_FIND].combo = (cui_key_combo){ CUI_MOD_CTRL | CUI_MOD_SHIFT, CUI_KEY_G };

    draw_settings(ui);
    /* Close, 4 sidebar items, Example string, Example number (+2 steppers), Import..., Export... */
    for (int i = 0; i < 11; ++i) press_key(ui, CUI_KEY_TAB, 0);

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

    /* Import from exported file: updates state, appearance, and persists. */
    mock_dialog_path = export_path;
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(mock_dialog_calls == 3);
    assert(mock_dialog_last_kind == CUI_FILE_OPEN);
    assert(strcmp(mock_dialog_last_filter, "json") == 0);
    assert(strcmp(settings.example_string, "Exported Value") == 0);
    assert(settings.example_number == 84);
    assert(settings.theme_mode == APP_THEME_DARK);
    assert(settings.accent == CUI_ACCENT_GREEN);
    assert(cui_get_accent(ui) == CUI_ACCENT_GREEN);
    assert(settings.keybindings[APP_ACTION_FIND].combo.key == CUI_KEY_G);
    assert(settings.keybindings[APP_ACTION_FIND].combo.mods == (CUI_MOD_CTRL | CUI_MOD_SHIFT));
    assert(settings_status && strcmp(settings_status, "Settings imported.") == 0);

    app_settings persisted = original;
    assert(app_settings_load(&persisted, test_settings_path));
    assert(strcmp(persisted.example_string, "Exported Value") == 0);
    assert(persisted.example_number == 84);
    assert(persisted.accent == CUI_ACCENT_GREEN);
    assert(persisted.keybindings[APP_ACTION_FIND].combo.key == CUI_KEY_G);

    /* Import invalid JSON: leaves settings intact and reports error. */
    write_text_file(bad_path, "{broken");
    mock_dialog_path = bad_path;
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(strcmp(settings.example_string, "Exported Value") == 0);
    assert(settings.example_number == 84);
    assert(settings_status && strstr(settings_status, "import") != NULL);
    assert(settings_status_tone == CUI_TONE_DANGER);

    /* Open Folder button resolves and opens the settings directory. */
    press_key(ui, CUI_KEY_TAB, 0); /* Export... */
    press_key(ui, CUI_KEY_TAB, 0); /* Open Folder */
    mock_open_url_calls = 0;
    mock_open_url_result = true;
    press_key(ui, CUI_KEY_ENTER, 0);
    assert(mock_open_url_calls == 1);
    assert(strcmp(mock_opened_url, base_dir) == 0);
    assert(settings_status && strcmp(settings_status, "Opened settings folder.") == 0);

    /* Reset to defaults: requires two-step confirm and disarms after timeout. */
    press_key(ui, CUI_KEY_TAB, 0); /* Reset */
    press_key(ui, CUI_KEY_ENTER, 0); /* Arms confirmation */
    assert(strcmp(settings.example_string, "Exported Value") == 0);
    /* Advance past 3s timeout so it disarms; next Enter re-arms without resetting. */
    cui_begin_frame(ui, 4.0f);
    frame(ui, (void *)test_settings_path);
    cui_end_frame(ui);
    press_key(ui, CUI_KEY_ENTER, 0); /* Re-arm */
    assert(strcmp(settings.example_string, "Exported Value") == 0);
    press_key(ui, CUI_KEY_ENTER, 0); /* Confirm */
    assert(strcmp(settings.example_string, default_settings.example_string) == 0);
    assert(settings.example_number == default_settings.example_number);
    assert(settings.theme_mode == default_settings.theme_mode);
    assert(settings.accent == default_settings.accent);
    assert(cui_get_accent(ui) == default_settings.accent);
    assert(settings.keybindings[APP_ACTION_FIND].combo.key == default_settings.keybindings[APP_ACTION_FIND].combo.key);
    assert(settings.keybindings[APP_ACTION_FIND].combo.mods == default_settings.keybindings[APP_ACTION_FIND].combo.mods);
    assert(settings_status && strcmp(settings_status, "Settings reset to defaults.") == 0);

    persisted = original;
    strcpy(persisted.example_string, "Stale");
    assert(app_settings_load(&persisted, test_settings_path));
    assert(strcmp(persisted.example_string, default_settings.example_string) == 0);
    assert(persisted.example_number == default_settings.example_number);
    assert(persisted.accent == default_settings.accent);
    assert(persisted.keybindings[APP_ACTION_FIND].combo.key == default_settings.keybindings[APP_ACTION_FIND].combo.key);

    /* Closing settings clears status. */
    settings_open = false;
    draw_settings(ui);
    assert(settings_status == NULL);

    settings_file_dialog = default_file_dialog;
    settings_open_url = cui_app_open_url;
    cui_destroy(ui);
    reset_ui_state();
    remove(test_settings_path);
    remove(export_path);
    remove(bad_path);
}

static void check_override_paths_and_dir(const char *base_dir) {
    reset_ui_state();
    app_settings original = settings;
    char nested[4096];
    int len = snprintf(nested, sizeof nested, "%s/nested/sub/settings.json", base_dir);
    assert(len >= 0 && (size_t)len < sizeof nested);
    remove(nested);

    char resolved_dir[4096];
    assert(app_settings_dir(resolved_dir, sizeof resolved_dir, nested, false));
    char expected_dir[4096];
    snprintf(expected_dir, sizeof expected_dir, "%s/nested/sub", base_dir);
    assert(strcmp(resolved_dir, expected_dir) == 0);

    app_settings saved = original;
    saved.example_number = 77;
    assert(app_settings_save(&saved, nested));
    app_settings loaded = original;
    app_settings_load(&loaded, nested);
    assert(loaded.example_number == 77);
    remove(nested);

    /* Bare filename with no directory separator works on all platforms. */
    const char *bare = "settings-bare-test.json";
    assert(app_settings_dir(resolved_dir, sizeof resolved_dir, bare, false));
    assert(strcmp(resolved_dir, ".") == 0);
    remove(bare);
    saved.example_number = 88;
    assert(app_settings_save(&saved, bare));
    loaded = original;
    app_settings_load(&loaded, bare);
    assert(loaded.example_number == 88);
    assert(remove(bare) == 0);
}

static void check_cli_parsing(void) {
    FILE *sink_out = tmpfile();
    FILE *sink_err = tmpfile();
    assert(sink_out && sink_err);
    app_cli_opts opts;

    char *argv_default[] = { "app" };
    assert(app_parse_cli(1, argv_default, &opts, sink_out, sink_err) == APP_CLI_RUN);
    assert(opts.settings_path == NULL);
    assert(!opts.software_renderer);

    char *argv_flags[] = { "app", "--software", "--settings", "/tmp/custom.json", "-psn_0_12345" };
    assert(app_parse_cli(5, argv_flags, &opts, sink_out, sink_err) == APP_CLI_RUN);
    assert(opts.software_renderer);
    assert(opts.settings_path && strcmp(opts.settings_path, "/tmp/custom.json") == 0);

    char *argv_eq[] = { "app", "--settings=/tmp/eq.json" };
    assert(app_parse_cli(2, argv_eq, &opts, sink_out, sink_err) == APP_CLI_RUN);
    assert(opts.settings_path && strcmp(opts.settings_path, "/tmp/eq.json") == 0);

    char *argv_help[] = { "app", "--help" };
    assert(app_parse_cli(2, argv_help, &opts, sink_out, sink_err) == APP_CLI_EXIT_OK);

    char *argv_version[] = { "app", "--version" };
    assert(app_parse_cli(2, argv_version, &opts, sink_out, sink_err) == APP_CLI_EXIT_OK);

    char *argv_missing[] = { "app", "--settings" };
    assert(app_parse_cli(2, argv_missing, &opts, sink_out, sink_err) == APP_CLI_EXIT_ERROR);

    char *argv_empty[] = { "app", "--settings=" };
    assert(app_parse_cli(2, argv_empty, &opts, sink_out, sink_err) == APP_CLI_EXIT_ERROR);

    char *argv_unknown[] = { "app", "--bogus" };
    assert(app_parse_cli(2, argv_unknown, &opts, sink_out, sink_err) == APP_CLI_EXIT_ERROR);

    fclose(sink_out);
    fclose(sink_err);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    settings_system_dark = mock_system_dark;

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
    check_appearance_settings();
    check_keybinding_edit_and_conflicts(CUI_THEME_LIGHT);
    check_keybinding_edit_and_conflicts(CUI_THEME_DARK);
    check_actions_and_command_palette();
    check_load_validation_and_compat(path);
    check_import_export_open_and_reset(argv[1], CUI_THEME_LIGHT);
    check_import_export_open_and_reset(argv[1], CUI_THEME_DARK);
    check_override_paths_and_dir(argv[1]);
    check_cli_parsing();

#ifndef __APPLE__
    {
        reset_ui_state();
        cui_ctx *ui = cui_create(&(cui_ctx_desc){ .theme = CUI_THEME_LIGHT });
        assert(ui);
        cui_set_viewport(ui, 900, 600, 1);
        cui_set_reduced_motion(ui, true);
        settings_open = false;
        press_key(ui, CUI_KEY_NONE, 0);
        press_key(ui, CUI_KEY_COMMA, CUI_MOD_PRIMARY);
        assert(settings_open);
        cui_destroy(ui);
        reset_ui_state();
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
        app_settings saved = default_settings;
        saved.example_number = 55;
        assert(app_settings_save(&saved, NULL));
        app_settings loaded = default_settings;
        app_settings_load(&loaded, NULL);
        assert(loaded.example_number == 55);
        assert(remove(xdg_path) == 0);
    }
#endif
    return 0;
}
