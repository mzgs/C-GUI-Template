#include "app_chrome.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include "macos_menu.h"
#endif

static const app_settings k_default_settings = {
    .example_string = "Example",
    .example_number = 42,
    .theme_mode = APP_THEME_SYSTEM,
    .accent = CUI_ACCENT_NEUTRAL,
    .font_scale = 1.0f,
    .reduced_motion = false,
    .window_has_pos = false,
    .window_x = 0,
    .window_y = 0,
    .window_width = 900,
    .window_height = 600,
    .window_fullscreen = false,
    .keybindings = {
        [APP_ACTION_FIND] = { "Find", "find", { CUI_MOD_PRIMARY, CUI_KEY_F } },
        [APP_ACTION_SAVE] = { "Save", "save", { CUI_MOD_PRIMARY, CUI_KEY_S } },
        [APP_ACTION_COMMAND_PALETTE] = { "Command Palette", "command_palette", { CUI_MOD_PRIMARY, CUI_KEY_K } },
    },
};

static bool default_file_dialog(const cui_file_dialog_opts *opts, char *out, size_t cap) {
    return cui_app_file_dialog(NULL, opts, out, cap);
}

app_settings app_chrome_default_settings(void) {
    return k_default_settings;
}

bool app_contains_ignore_case(const char *text, const char *query) {
    for (; *text; ++text) {
        const char *a = text, *b = query;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            ++a;
            ++b;
        }
        if (!*b) return true;
    }
    return !*query;
}

static cui_theme_mode resolve_theme_mode(const app_chrome *chrome, app_theme_mode mode, cui_theme_mode fallback) {
    if (mode == APP_THEME_LIGHT) return CUI_THEME_LIGHT;
    if (mode == APP_THEME_DARK) return CUI_THEME_DARK;
    int (*sys_dark)(void) = chrome->system_dark ? chrome->system_dark : app_system_dark;
    int dark = sys_dark();
    if (dark >= 0) return dark ? CUI_THEME_DARK : CUI_THEME_LIGHT;
    return fallback;
}

void app_chrome_notify(app_chrome *chrome, cui_ctx *ui, const char *message, cui_tone tone) {
    chrome->status = message;
    chrome->status_tone = tone;
    if (ui && message) cui_toast(ui, message, .tone = tone);
}

const app_settings *app_chrome_settings(const app_chrome *chrome) {
    return &chrome->settings;
}

static void open_settings_folder(app_chrome *chrome, cui_ctx *ui) {
    char dir[4096];
    bool (*opener)(const char *) = chrome->open_url ? chrome->open_url : cui_app_open_url;
    if (app_settings_dir(dir, sizeof dir, chrome->settings_path, true) && opener(dir)) {
        app_chrome_notify(chrome, ui, "Opened settings folder.", CUI_TONE_INFO);
    } else {
        app_chrome_notify(chrome, ui, "Couldn’t open settings folder.", CUI_TONE_DANGER);
    }
}

static void toggle_fullscreen(void) {
    if (cui_app_ui(NULL)) cui_app_set_fullscreen(NULL, !cui_app_is_fullscreen(NULL));
}

static void trigger_action(app_chrome *chrome, cui_ctx *ui, app_action action) {
    if (action == APP_ACTION_COMMAND_PALETTE) {
        chrome->palette_open = true;
        return;
    }
    if (chrome->on_action) {
        chrome->on_action(ui, action, chrome->on_action_user);
    }
}

static void apply_appearance(app_chrome *chrome, cui_ctx *ui) {
    bool first = (chrome->appearance_ui != ui);
    if (first) chrome->appearance_ui = ui;

    if (!chrome->env_theme_override) {
        cui_theme_mode current = cui_get_theme_mode(ui);
        cui_theme_mode target = resolve_theme_mode(chrome, chrome->settings.theme_mode, current);
        cui_set_theme(ui, target);
    }
    if (!chrome->env_accent_override) {
        cui_set_accent(ui, chrome->settings.accent);
    }
    float scale = chrome->settings.font_scale;
    if (scale < FONT_SCALE_MIN || scale > FONT_SCALE_MAX || !isfinite(scale)) scale = 1.0f;
    cui_theme_get(ui)->font_scale = scale;

    if (chrome->appearance_force_motion || (first && chrome->settings.reduced_motion)) {
        cui_set_reduced_motion(ui, chrome->settings.reduced_motion);
        chrome->appearance_force_motion = false;
    }
}

static void capture_live_window_bounds(app_chrome *chrome) {
    if (!cui_app_ui(NULL) || chrome->window_fullscreen_pending) return;
    bool fs = cui_app_is_fullscreen(NULL);
    if (fs != chrome->settings.window_fullscreen) {
        chrome->settings.window_fullscreen = fs;
        chrome->window_dirty = true;
    }
    if (fs) return;
    cui_vec2 win = cui_app_size(NULL);
    int w = (int)lroundf(win.x);
    int h = (int)lroundf(win.y);
    if (w >= WINDOW_WIDTH_MIN && w <= WINDOW_WIDTH_MAX &&
        h >= WINDOW_HEIGHT_MIN && h <= WINDOW_HEIGHT_MAX &&
        (w != chrome->settings.window_width || h != chrome->settings.window_height)) {
        chrome->settings.window_width = w;
        chrome->settings.window_height = h;
        chrome->window_dirty = true;
    }
    int x = 0, y = 0;
    if (app_window_get_pos(cui_app_native_window(NULL), cui_app_backend(NULL), &x, &y)) {
        if (!chrome->observed_window_has_pos) {
            chrome->observed_window_has_pos = true;
            chrome->observed_window_x = x;
            chrome->observed_window_y = y;
        } else if (x != chrome->observed_window_x || y != chrome->observed_window_y) {
            chrome->observed_window_x = x;
            chrome->observed_window_y = y;
            if (!chrome->settings.window_has_pos ||
                x != chrome->settings.window_x || y != chrome->settings.window_y) {
                chrome->settings.window_has_pos = true;
                chrome->settings.window_x = x;
                chrome->settings.window_y = y;
                chrome->window_dirty = true;
            }
        }
    }
}

void app_chrome_flush(app_chrome *chrome) {
    capture_live_window_bounds(chrome);
    if (chrome->settings_dirty || chrome->window_dirty) {
        app_settings_save(&chrome->settings, chrome->settings_path);
        chrome->settings_dirty = false;
        chrome->window_dirty = false;
    }
}

void app_chrome_shutdown(app_chrome *chrome) {
    if (chrome->settings_dirty || chrome->window_dirty) {
        app_settings_save(&chrome->settings, chrome->settings_path);
        chrome->settings_dirty = false;
        chrome->window_dirty = false;
    }
}

#ifdef __APPLE__
static void on_menu_action(app_action action, void *user) {
    app_chrome *chrome = user;
    chrome->pending_action = (int)action;
}

static void on_menu_edit_key(cui_key key, void *user) {
    app_chrome *chrome = user;
    chrome->pending_edit_key = key;
}

static void on_menu_toggle_fullscreen(void *user) {
    (void)user;
    toggle_fullscreen();
}

static void on_menu_will_terminate(void *user) {
    app_chrome_flush((app_chrome *)user);
}
#endif

static void sync_window_state(app_chrome *chrome, cui_ctx *ui) {
    if (cui_app_ui(NULL) != ui) return;
    if (chrome->window_state_ui != ui) {
        chrome->window_state_ui = ui;
        chrome->window_fullscreen_pending = false;
        void *native = cui_app_native_window(NULL);
        const char *backend = cui_app_backend(NULL);
        if (chrome->settings.window_has_pos) {
            app_window_set_pos(native, backend, chrome->settings.window_x, chrome->settings.window_y);
        }
        int x = 0, y = 0;
        chrome->observed_window_has_pos = app_window_get_pos(native, backend, &x, &y);
        chrome->observed_window_x = x;
        chrome->observed_window_y = y;
        if (chrome->settings.window_fullscreen) {
            cui_app_set_fullscreen(NULL, true);
            chrome->window_fullscreen_pending = true;
            chrome->window_fullscreen_since = cui_time(ui);
        }
    }
    bool fs = cui_app_is_fullscreen(NULL);
    if (chrome->window_fullscreen_pending) {
        if (fs || (cui_time(ui) - chrome->window_fullscreen_since >= 1.5))
            chrome->window_fullscreen_pending = false;
    }
    if (!chrome->window_fullscreen_pending) {
        bool was_dirty = chrome->window_dirty;
        int prev_w = chrome->settings.window_width, prev_h = chrome->settings.window_height;
        int prev_x = chrome->settings.window_x, prev_y = chrome->settings.window_y;
        bool prev_has_pos = chrome->settings.window_has_pos, prev_fs = chrome->settings.window_fullscreen;
        capture_live_window_bounds(chrome);
        if (chrome->window_dirty && (!was_dirty ||
                                     chrome->settings.window_width != prev_w ||
                                     chrome->settings.window_height != prev_h ||
                                     chrome->settings.window_x != prev_x ||
                                     chrome->settings.window_y != prev_y ||
                                     chrome->settings.window_has_pos != prev_has_pos ||
                                     chrome->settings.window_fullscreen != prev_fs)) {
            chrome->window_dirty_since = cui_time(ui);
        }
    }
    if (chrome->window_dirty) {
        double elapsed = cui_time(ui) - chrome->window_dirty_since;
        if (elapsed >= 0.5) {
            chrome->settings_save_failed = !app_settings_save(&chrome->settings, chrome->settings_path);
            chrome->window_dirty = false;
        } else {
            cui_request_wakeup(ui, (float)(0.5 - elapsed) + 0.02f);
        }
    }
}

static bool save_settings(app_chrome *chrome, cui_ctx *ui) {
    bool prev_failed = chrome->settings_save_failed;
    bool ok = app_settings_save(&chrome->settings, chrome->settings_path);
    chrome->settings_save_failed = !ok;
    if (ok) {
        chrome->window_dirty = false;
    } else if (!prev_failed) {
        app_chrome_notify(chrome, ui, "Couldn’t save settings.", CUI_TONE_DANGER);
    }
    return ok;
}

static void draw_command_palette(app_chrome *chrome, cui_ctx *ui) {
    const char *settings_shortcut = cui_shortcut_label(ui, (cui_key_combo){ CUI_MOD_PRIMARY, CUI_KEY_COMMA });
    const char *find_shortcut = cui_shortcut_label(ui, chrome->settings.keybindings[APP_ACTION_FIND].combo);
    const char *save_shortcut = cui_shortcut_label(ui, chrome->settings.keybindings[APP_ACTION_SAVE].combo);

    cui_command commands[] = {
        { "Open Settings", CUI_ICON_SETTINGS, "Navigation", settings_shortcut },
        { "Appearance Settings", CUI_ICON_PALETTE, "Navigation", NULL },
        { "Keybindings", CUI_ICON_KEYBOARD, "Navigation", NULL },
        { "About " APP_NAME, CUI_ICON_INFO, "Navigation", NULL },
        { "Theme: System", CUI_ICON_MONITOR, "Appearance", NULL },
        { "Theme: Light", CUI_ICON_SUN, "Appearance", NULL },
        { "Theme: Dark", CUI_ICON_MOON, "Appearance", NULL },
        { "Toggle Fullscreen", CUI_ICON_MAXIMIZE_2, "Appearance", NULL },
        { "Find", CUI_ICON_SEARCH, "Actions", find_shortcut },
        { "Save", CUI_ICON_SAVE, "Actions", save_shortcut },
        { "Open Settings Folder", CUI_ICON_FOLDER_OPEN, "Actions", NULL },
    };
    int chosen = cui_command_palette(ui, &chrome->palette_open, commands, (int)(sizeof commands / sizeof commands[0]));
    switch (chosen) {
    case 0:
        chrome->settings_open = true;
        break;
    case 1:
        chrome->settings_section = SETTINGS_SECTION_APPEARANCE;
        chrome->settings_open = true;
        break;
    case 2:
        chrome->settings_section = SETTINGS_SECTION_KEYBINDINGS;
        chrome->settings_open = true;
        break;
    case 3:
        chrome->settings_section = SETTINGS_SECTION_ABOUT;
        chrome->settings_open = true;
        break;
    case 4:
    case 5:
    case 6: {
        app_theme_mode modes[] = { APP_THEME_SYSTEM, APP_THEME_LIGHT, APP_THEME_DARK };
        const char *messages[] = { "Theme set to System.", "Theme set to Light.", "Theme set to Dark." };
        chrome->settings.theme_mode = modes[chosen - 4];
        chrome->env_theme_override = false;
        apply_appearance(chrome, ui);
        if (save_settings(chrome, ui)) app_chrome_notify(chrome, ui, messages[chosen - 4], CUI_TONE_INFO);
        break;
    }
    case 7:
        toggle_fullscreen();
        break;
    case 8:
        trigger_action(chrome, ui, APP_ACTION_FIND);
        break;
    case 9:
        trigger_action(chrome, ui, APP_ACTION_SAVE);
        break;
    case 10:
        open_settings_folder(chrome, ui);
        break;
    default:
        break;
    }
}

static void draw_settings_dialog(app_chrome *chrome, cui_ctx *ui) {
    static const char *const sections[SETTINGS_SECTION_COUNT] = {
        "General", "Appearance", "Keybindings", "About"
    };
    static const char *const icons[SETTINGS_SECTION_COUNT] = {
        CUI_ICON_SETTINGS, CUI_ICON_PALETTE, CUI_ICON_KEYBOARD, CUI_ICON_INFO
    };
    static const char *const theme_labels[APP_THEME_COUNT] = { "System", "Light", "Dark" };
    static const char *const theme_icons[APP_THEME_COUNT] = {
        CUI_ICON_MONITOR, CUI_ICON_SUN, CUI_ICON_MOON
    };
    static const char *const accent_labels[CUI_ACCENT_COUNT] = {
        "Neutral", "Violet", "Blue", "Green", "Amber", "Orange", "Coral", "Rose"
    };

    cui_vec2 size = cui_viewport_size(ui);
    bool compact = size.x < 600.0f;
    bool (*dialog_fn)(const cui_file_dialog_opts *, char *, size_t) =
        chrome->file_dialog ? chrome->file_dialog : default_file_dialog;

    cui_dialog(ui, "Settings", &chrome->settings_open, .width = size.x * 0.9f) {
        /* shortcut: pinned CUI adds 94pt for the header, padding and gap; revisit if its layout changes. */
        cui_column(ui, .height = CUI_PX(fminf(size.y * 0.9f, size.y - 48) - 94)) {
            cui_row(ui, .height = CUI_FILL, .gap = 16, .align = CUI_ALIGN_STRETCH) {
                cui_column(ui, .width = CUI_PCT(30), .min_width = 128, .max_width = 180,
                           .height = CUI_FILL) {
                    cui_scroll(ui, .id = "settings-sidebar", .gap = 4) {
                        for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
                            if (i == SETTINGS_SECTION_ABOUT) cui_separator(ui);
                            if (cui_nav_item(ui, sections[i], .icon = icons[i],
                                             .selected = chrome->settings_section == i,
                                             .group = "settings-sections")) {
                                if (chrome->settings_section != i) chrome->status = NULL;
                                chrome->settings_section = i;
                            }
                        }
                    }
                }
                cui_separator(ui);
                cui_scroll(ui, .id = "settings-page", .width = CUI_FILL) {
                    cui_column(ui, .height = chrome->settings_section == SETTINGS_SECTION_KEYBINDINGS ? CUI_FILL : CUI_FIT,
                               .gap = 12) {
                        bool changed = false;
                        cui_heading(ui, sections[chrome->settings_section], 4);
                        if (chrome->settings_section == SETTINGS_SECTION_GENERAL) {
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16) {
                                cui_text(ui, "Example string", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                int flags = cui_input(ui, "example-string", chrome->settings.example_string,
                                                      sizeof chrome->settings.example_string, .width = CUI_FILL);
                                if (flags & CUI_CHANGED) {
                                    chrome->settings_dirty = true;
                                    chrome->status = NULL;
                                }
                                if (chrome->settings_dirty && (flags & (CUI_SUBMITTED | CUI_BLURRED))) changed = true;
                            }
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16) {
                                cui_text(ui, "Example number (0–100)", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                double previous = chrome->settings.example_number;
                                if (cui_number(ui, "example-number", &chrome->settings.example_number,
                                               EXAMPLE_NUMBER_MIN, EXAMPLE_NUMBER_MAX, .width = CUI_FILL)) {
                                    if (isfinite(chrome->settings.example_number)) {
                                        changed = true;
                                        chrome->status = NULL;
                                    } else {
                                        chrome->settings.example_number = previous;
                                    }
                                }
                            }
                            cui_separator(ui);
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16) {
                                cui_text(ui, "Settings file", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                cui_row(ui, .width = CUI_FILL, .gap = 8, .wrap = true) {
                                    if (cui_button(ui, "Import…", .icon = CUI_ICON_UPLOAD)) {
                                        char path[4096];
                                        if (dialog_fn(&(cui_file_dialog_opts){
                                                .kind = CUI_FILE_OPEN,
                                                .title = "Import Settings",
                                                .filter_name = "JSON",
                                                .filter = "json",
                                            }, path, sizeof path)) {
                                            if (app_settings_load(&chrome->settings, path)) {
                                                chrome->env_theme_override = false;
                                                chrome->env_accent_override = false;
                                                chrome->appearance_force_motion = true;
                                                apply_appearance(chrome, ui);
                                                chrome->settings_dirty = false;
                                                changed = false;
                                                if (save_settings(chrome, ui))
                                                    app_chrome_notify(chrome, ui, "Settings imported.", CUI_TONE_SUCCESS);
                                            } else {
                                                app_chrome_notify(chrome, ui, "Couldn’t import settings.", CUI_TONE_DANGER);
                                            }
                                        }
                                    }
                                    if (cui_button(ui, "Export…", .icon = CUI_ICON_DOWNLOAD)) {
                                        char path[4096];
                                        if (dialog_fn(&(cui_file_dialog_opts){
                                                .kind = CUI_FILE_SAVE,
                                                .title = "Export Settings",
                                                .filter_name = "JSON",
                                                .filter = "json",
                                                .default_name = "settings.json",
                                            }, path, sizeof path)) {
                                            if (app_settings_save(&chrome->settings, path))
                                                app_chrome_notify(chrome, ui, "Settings exported.", CUI_TONE_SUCCESS);
                                            else
                                                app_chrome_notify(chrome, ui, "Couldn’t export settings.", CUI_TONE_DANGER);
                                        }
                                    }
                                    if (cui_button(ui, "Open Folder", .icon = CUI_ICON_FOLDER_OPEN)) {
                                        open_settings_folder(chrome, ui);
                                    }
                                }
                            }
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16) {
                                cui_text(ui, "Reset settings", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                if (cui_confirm_button(ui, "Reset",
                                                       .icon = CUI_ICON_ROTATE_CCW,
                                                       .confirm_label = "Confirm")) {
                                    chrome->settings = k_default_settings;
                                    chrome->env_theme_override = false;
                                    chrome->env_accent_override = false;
                                    chrome->appearance_force_motion = true;
                                    apply_appearance(chrome, ui);
                                    if (cui_app_ui(NULL) == ui) {
                                        if (cui_app_is_fullscreen(NULL)) cui_app_set_fullscreen(NULL, false);
                                        cui_app_set_size(NULL, chrome->settings.window_width, chrome->settings.window_height);
                                        void *native = cui_app_native_window(NULL);
                                        const char *backend = cui_app_backend(NULL);
                                        app_window_center(native, backend);
                                        int x = 0, y = 0;
                                        chrome->observed_window_has_pos = app_window_get_pos(native, backend, &x, &y);
                                        chrome->observed_window_x = x;
                                        chrome->observed_window_y = y;
                                    }
                                    chrome->settings_dirty = false;
                                    changed = false;
                                    if (save_settings(chrome, ui))
                                        app_chrome_notify(chrome, ui, "Settings reset to defaults.", CUI_TONE_INFO);
                                }
                            }
                        } else if (chrome->settings_section == SETTINGS_SECTION_APPEARANCE) {
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16,
                                    .align = compact ? CUI_ALIGN_STRETCH : CUI_ALIGN_CENTER) {
                                cui_text(ui, "Theme", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                int mode = (int)chrome->settings.theme_mode;
                                bool theme_changed = compact
                                    ? cui_select(ui, "theme-mode", theme_labels, APP_THEME_COUNT, &mode,
                                                 .icons = theme_icons, .width = CUI_FILL)
                                    : cui_segmented(ui, "theme-mode", theme_labels, APP_THEME_COUNT, &mode,
                                                    .icons = theme_icons);
                                if (theme_changed) {
                                    chrome->settings.theme_mode = (app_theme_mode)mode;
                                    chrome->env_theme_override = false;
                                    apply_appearance(chrome, ui);
                                    changed = true;
                                }
                            }
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16,
                                    .align = compact ? CUI_ALIGN_STRETCH : CUI_ALIGN_CENTER) {
                                cui_text(ui, "Accent color", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                int accent = (int)chrome->settings.accent;
                                if (cui_select(ui, "accent-color", accent_labels, CUI_ACCENT_COUNT, &accent,
                                               .width = CUI_FILL)) {
                                    chrome->settings.accent = (cui_accent)accent;
                                    chrome->env_accent_override = false;
                                    apply_appearance(chrome, ui);
                                    changed = true;
                                }
                            }
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16,
                                    .align = compact ? CUI_ALIGN_STRETCH : CUI_ALIGN_CENTER) {
                                cui_text(ui, "Text scale", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                float percent = roundf(chrome->settings.font_scale * 100.0f);
                                if (cui_slider(ui, "##text-scale", &percent,
                                               FONT_SCALE_MIN * 100.0f, FONT_SCALE_MAX * 100.0f,
                                               .step = 5.0f, .show_value = true, .format = "%.0f%%",
                                               .width = CUI_FILL)) {
                                    chrome->settings.font_scale = percent / 100.0f;
                                    apply_appearance(chrome, ui);
                                    changed = true;
                                }
                            }
                            cui_separator(ui);
                            if (cui_switch(ui, "Reduce motion", &chrome->settings.reduced_motion,
                                           .description = "Disable UI animations")) {
                                chrome->appearance_force_motion = true;
                                apply_appearance(chrome, ui);
                                changed = true;
                            }
                        } else if (chrome->settings_section == SETTINGS_SECTION_KEYBINDINGS) {
                            cui_table_column columns[] = {
                                { "Action", CUI_FILL, CUI_TEXT_LEFT, false },
                                { "Keys", CUI_PX(240), CUI_TEXT_LEFT, false },
                            };
                            cui_secondary(ui, "Click a shortcut, then press a key combination. Backspace clears.");
                            cui_search(ui, "keybinding-filter", chrome->keybinding_filter,
                                       sizeof chrome->keybinding_filter,
                                       .placeholder = "Filter actions or keys...", .width = CUI_FILL);
                            bool has_conflict = false;
                            for (int i = 0; i < APP_ACTION_COUNT; ++i) {
                                if (app_keybinding_conflict(&chrome->settings, (app_action)i) != APP_CONFLICT_NONE) {
                                    has_conflict = true;
                                    break;
                                }
                            }
                            if (has_conflict) {
                                cui_alert(ui, "Shortcut conflict",
                                          .description = "Duplicate or reserved shortcuts are highlighted below.",
                                          .tone = CUI_TONE_WARNING);
                            }
                            int visible = 0;
                            cui_table(ui, "keybindings", columns, 2, .striped = true, .compact = true,
                                      .height = CUI_FILL) {
                                for (int i = 0; i < APP_ACTION_COUNT; ++i) {
                                    const char *action = chrome->settings.keybindings[i].action;
                                    cui_key_combo *combo = &chrome->settings.keybindings[i].combo;
                                    if (!app_contains_ignore_case(action, chrome->keybinding_filter) &&
                                        !app_contains_ignore_case(cui_shortcut_label(ui, *combo),
                                                                  chrome->keybinding_filter)) continue;
                                    ++visible;
                                    int conflict = app_keybinding_conflict(&chrome->settings, (app_action)i);
                                    cui_table_row(ui, false);
                                    if (conflict == APP_CONFLICT_NONE) {
                                        cui_table_cell_text(ui, action);
                                    } else {
                                        cui_table_cell(ui);
                                        cui_text(ui, action);
                                        cui_badge(ui, conflict == APP_CONFLICT_SETTINGS ? "Reserved" : "Conflict",
                                                  .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                                    }
                                    cui_table_cell(ui);
                                    cui_theme *theme = cui_theme_get(ui);
                                    float control_height = theme->control_md;
                                    theme->control_md = theme->control_sm;
                                    if (cui_shortcut_recorder(ui, action, combo, .width = CUI_FILL)) changed = true;
                                    theme->control_md = control_height;
                                }
                            }
                            if (!visible) cui_secondary(ui, "No shortcuts match your filter.");
                        } else {
                            cui_text(ui, APP_NAME, .font = CUI_FONT_MEDIUM);
                            cui_secondary(ui, "Version " APP_VERSION);
                            cui_secondary(ui, "A C99 desktop app built with Better-C-UI.");
                            cui_separator(ui);
                            const char *backend = cui_app_backend(NULL);
                            const char *renderer = cui_app_ui(NULL)
                                ? (cui_app_is_software(NULL) ? "Software" : "OpenGL")
                                : "Headless";
                            char runtime_info[128];
                            snprintf(runtime_info, sizeof runtime_info, "Backend: %s  ·  Renderer: %s",
                                     backend ? backend : "unknown", renderer);
                            cui_secondary(ui, runtime_info);
                            char dir[4096];
                            if (app_settings_dir(dir, sizeof dir, chrome->settings_path, false)) {
                                cui_secondary(ui, dir);
                            }
                            cui_row(ui, .gap = 8, .wrap = true) {
                                if (cui_button(ui, "Open Settings Folder", .icon = CUI_ICON_FOLDER_OPEN)) {
                                    open_settings_folder(chrome, ui);
                                }
                            }
                        }
                        if (changed || (chrome->settings_dirty && chrome->settings_section != SETTINGS_SECTION_GENERAL)) {
                            save_settings(chrome, ui);
                            chrome->settings_dirty = false;
                        }
                        if (chrome->settings_save_failed) cui_secondary(ui, "Couldn’t save settings.");
                    }
                }
            }
        }
    }
    if (!chrome->settings_open) {
        if (chrome->prev_settings_open) chrome->status = NULL;
        if (chrome->settings_dirty) {
            save_settings(chrome, ui);
            chrome->settings_dirty = false;
        }
    }
    chrome->prev_settings_open = chrome->settings_open;
}

void app_chrome_begin_frame(app_chrome *chrome, cui_ctx *ui) {
    apply_appearance(chrome, ui);
    sync_window_state(chrome, ui);

    chrome->injected_edit_key = CUI_KEY_NONE;
    if (chrome->pending_edit_key != CUI_KEY_NONE) {
        chrome->injected_edit_key = chrome->pending_edit_key;
        chrome->pending_edit_key = CUI_KEY_NONE;
        cui_input_key(ui, chrome->injected_edit_key, true, CUI_MOD_PRIMARY);
    }

#ifdef __APPLE__
    app_menu_install(&(app_menu_hooks){
        .settings_open = &chrome->settings_open,
        .settings_section = &chrome->settings_section,
        .about_section = SETTINGS_SECTION_ABOUT,
        .palette_open = &chrome->palette_open,
        .user = chrome,
        .on_action = on_menu_action,
        .on_edit_key = on_menu_edit_key,
        .on_toggle_fullscreen = on_menu_toggle_fullscreen,
        .on_will_terminate = on_menu_will_terminate,
    });
    app_menu_sync(&(app_menu_state){
        .save_combo = chrome->settings.keybindings[APP_ACTION_SAVE].combo,
        .find_combo = chrome->settings.keybindings[APP_ACTION_FIND].combo,
        .palette_combo = chrome->settings.keybindings[APP_ACTION_COMMAND_PALETTE].combo,
        .fullscreen = cui_app_ui(NULL) ? cui_app_is_fullscreen(NULL) : chrome->settings.window_fullscreen,
    });
    if (!chrome->settings_open && !chrome->palette_open && !cui_wants_keyboard(ui)) {
        if (cui_shortcut(ui, CUI_MOD_PRIMARY | CUI_MOD_CTRL, CUI_KEY_F)) toggle_fullscreen();
    }
#else
    cui_menu_bar(ui, "app-menu") {
        cui_menu_bar_menu(ui, "File") {
            if (cui_menu_item(ui, "Save", .icon = CUI_ICON_SAVE,
                              .shortcut = cui_shortcut_label(ui, chrome->settings.keybindings[APP_ACTION_SAVE].combo)))
                trigger_action(chrome, ui, APP_ACTION_SAVE);
            cui_menu_separator(ui);
            if (cui_menu_item(ui, "Settings…", .icon = CUI_ICON_SETTINGS, .shortcut = "Ctrl ,"))
                chrome->settings_open = true;
            cui_menu_separator(ui);
            if (cui_menu_item(ui, "Quit", .icon = CUI_ICON_LOG_OUT, .shortcut = "Ctrl Q"))
                cui_app_quit(NULL);
        }
        cui_menu_bar_menu(ui, "Edit") {
            if (cui_menu_item(ui, "Find", .icon = CUI_ICON_SEARCH,
                              .shortcut = cui_shortcut_label(ui, chrome->settings.keybindings[APP_ACTION_FIND].combo)))
                trigger_action(chrome, ui, APP_ACTION_FIND);
        }
        cui_menu_bar_menu(ui, "View") {
            if (cui_menu_item(ui, "Command Palette…", .icon = CUI_ICON_COMMAND,
                              .shortcut = cui_shortcut_label(ui, chrome->settings.keybindings[APP_ACTION_COMMAND_PALETTE].combo)))
                chrome->palette_open = true;
            if (cui_menu_item(ui, "Fullscreen", .icon = CUI_ICON_MAXIMIZE_2,
                              .shortcut = "F11", .checked = cui_app_is_fullscreen(NULL)))
                toggle_fullscreen();
        }
        cui_menu_bar_menu(ui, "Help") {
            if (cui_menu_item(ui, "About " APP_NAME, .icon = CUI_ICON_INFO)) {
                chrome->settings_section = SETTINGS_SECTION_ABOUT;
                chrome->settings_open = true;
            }
        }
    }
    if (!chrome->settings_open && !chrome->palette_open && !cui_wants_keyboard(ui)) {
        if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_COMMA)) chrome->settings_open = true;
        else if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_Q)) cui_app_quit(NULL);
        else if (cui_shortcut(ui, 0, CUI_KEY_F11)) toggle_fullscreen();
    }
#endif

    if (chrome->pending_action >= 0 && chrome->pending_action < APP_ACTION_COUNT) {
        app_action queued = (app_action)chrome->pending_action;
        chrome->pending_action = -1;
        trigger_action(chrome, ui, queued);
    }

    if (!chrome->settings_open && !chrome->palette_open) {
        if (app_action_pressed(ui, &chrome->settings, APP_ACTION_COMMAND_PALETTE)) {
            chrome->palette_open = true;
        } else if (app_action_pressed(ui, &chrome->settings, APP_ACTION_FIND)) {
            trigger_action(chrome, ui, APP_ACTION_FIND);
        } else if (app_action_pressed(ui, &chrome->settings, APP_ACTION_SAVE)) {
            trigger_action(chrome, ui, APP_ACTION_SAVE);
        }
    }
}

void app_chrome_end_frame(app_chrome *chrome, cui_ctx *ui) {
    draw_command_palette(chrome, ui);
    draw_settings_dialog(chrome, ui);
    if (chrome->injected_edit_key != CUI_KEY_NONE) {
        cui_input_key(ui, chrome->injected_edit_key, false, 0);
        cui_input_key(ui, CUI_KEY_NONE, false, 0);
        chrome->injected_edit_key = CUI_KEY_NONE;
    }
}

app_cli_action app_chrome_parse_cli(int argc, char **argv, app_cli_opts *opts, FILE *out, FILE *err) {
    memset(opts, 0, sizeof *opts);
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strncmp(arg, "-psn_", 5) == 0) {
            /* macOS LaunchServices passes -psn_* when launching a bundle. */
            continue;
        }
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            fprintf(out,
                    "Usage: %s [options]\n\n"
                    "Options:\n"
                    "  -h, --help             Show this help message and exit\n"
                    "  -v, --version          Print version information and exit\n"
                    "  --settings <path>      Use a custom settings.json file\n"
                    "  --software             Force the software renderer\n",
                    argc > 0 && argv[0] ? argv[0] : APP_NAME);
            return APP_CLI_EXIT_OK;
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            fprintf(out, "%s %s\n", APP_NAME, APP_VERSION);
            return APP_CLI_EXIT_OK;
        }
        if (strcmp(arg, "--software") == 0) {
            opts->software_renderer = true;
            continue;
        }
        if (strcmp(arg, "--settings") == 0) {
            if (i + 1 >= argc || !argv[i + 1][0]) {
                fprintf(err, "Missing path for --settings\n");
                return APP_CLI_EXIT_ERROR;
            }
            opts->settings_path = argv[++i];
            continue;
        }
        if (strncmp(arg, "--settings=", 11) == 0) {
            if (!arg[11]) {
                fprintf(err, "Missing path for --settings\n");
                return APP_CLI_EXIT_ERROR;
            }
            opts->settings_path = arg + 11;
            continue;
        }
        fprintf(err, "Unknown option: %s\nTry --help for usage.\n", arg);
        return APP_CLI_EXIT_ERROR;
    }
    return APP_CLI_RUN;
}

void app_chrome_reset(app_chrome *chrome, const char *settings_path) {
    memset(chrome, 0, sizeof *chrome);
    chrome->settings = k_default_settings;
    chrome->settings_path = settings_path;
    chrome->pending_action = -1;
    chrome->file_dialog = default_file_dialog;
    chrome->open_url = cui_app_open_url;
    chrome->system_dark = app_system_dark;
}

bool app_chrome_init(app_chrome *chrome, int argc, char **argv, int *exit_code) {
    app_cli_opts cli;
    app_cli_action action = app_chrome_parse_cli(argc, argv, &cli, stdout, stderr);
    if (action == APP_CLI_EXIT_OK) {
        if (exit_code) *exit_code = 0;
        return false;
    }
    if (action == APP_CLI_EXIT_ERROR) {
        if (exit_code) *exit_code = 2;
        return false;
    }
    app_chrome_reset(chrome, cli.settings_path);
    chrome->cli = cli;
    app_settings_load(&chrome->settings, cli.settings_path);
    const char *env_theme = getenv("CUI_THEME");
    const char *env_accent = getenv("CUI_ACCENT");
    chrome->env_theme_override = env_theme && env_theme[0] != '\0';
    chrome->env_accent_override = env_accent && env_accent[0] != '\0';
    return true;
}

cui_app_desc app_chrome_app_desc(const app_chrome *chrome) {
    return (cui_app_desc){
        .title = APP_NAME,
        .width = chrome->settings.window_width,
        .height = chrome->settings.window_height,
        .renderer = chrome->cli.software_renderer ? CUI_RENDERER_SOFTWARE : CUI_RENDERER_AUTO,
        .theme = resolve_theme_mode(chrome, chrome->settings.theme_mode, CUI_THEME_LIGHT),
        .accent = chrome->settings.accent,
        .follow_system_theme = false,
    };
}

void app_chrome_set_action_handler(app_chrome *chrome, app_action_handler handler, void *user) {
    chrome->on_action = handler;
    chrome->on_action_user = user;
}
