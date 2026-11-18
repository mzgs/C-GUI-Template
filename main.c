#include <cui/cui_app.h>
#include <cui/cui.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"

#ifdef __APPLE__
#include "macos_menu.h"
#endif

enum {
    SETTINGS_SECTION_GENERAL = 0,
    SETTINGS_SECTION_APPEARANCE,
    SETTINGS_SECTION_KEYBINDINGS,
    SETTINGS_SECTION_ABOUT,
    SETTINGS_SECTION_COUNT
};

typedef enum {
    APP_CLI_RUN = 0,
    APP_CLI_EXIT_OK,
    APP_CLI_EXIT_ERROR,
} app_cli_action;

typedef struct {
    const char *settings_path;
    bool software_renderer;
} app_cli_opts;

static bool settings_open;
static bool prev_settings_open;
static bool palette_open;
static bool settings_save_failed;
static bool settings_dirty;
static bool window_dirty;
static double window_dirty_since;
static bool window_fullscreen_pending;
static double window_fullscreen_since;
static cui_ctx *window_state_ui;
static cui_ctx *appearance_ui;
static bool appearance_force_motion;
static bool env_theme_override;
static bool env_accent_override;
static const char *settings_status;
static cui_tone settings_status_tone;
static int settings_section;
static char keybinding_filter[128];

static bool default_file_dialog(const cui_file_dialog_opts *opts, char *out, size_t cap) {
    return cui_app_file_dialog(NULL, opts, out, cap);
}

static bool (*settings_file_dialog)(const cui_file_dialog_opts *opts, char *out, size_t cap) = default_file_dialog;
static bool (*settings_open_url)(const char *url) = cui_app_open_url;
static int (*settings_system_dark)(void) = app_system_dark;

#define APP_SETTINGS_DEFAULT { \
    .example_string = "Example", \
    .example_number = 42, \
    .theme_mode = APP_THEME_SYSTEM, \
    .accent = CUI_ACCENT_NEUTRAL, \
    .font_scale = 1.0f, \
    .reduced_motion = false, \
    .window_width = 900, \
    .window_height = 600, \
    .window_fullscreen = false, \
    .keybindings = { \
        [APP_ACTION_FIND] = { "Find", "find", { CUI_MOD_PRIMARY, CUI_KEY_F } }, \
        [APP_ACTION_SAVE] = { "Save", "save", { CUI_MOD_PRIMARY, CUI_KEY_S } }, \
        [APP_ACTION_COMMAND_PALETTE] = { "Command Palette", "command_palette", { CUI_MOD_PRIMARY, CUI_KEY_K } }, \
    }, \
}

static const app_settings default_settings = APP_SETTINGS_DEFAULT;
static app_settings settings = APP_SETTINGS_DEFAULT;

static bool contains_ignore_case(const char *text, const char *query) {
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

static cui_theme_mode resolve_theme_mode(app_theme_mode mode, cui_theme_mode fallback) {
    if (mode == APP_THEME_LIGHT) return CUI_THEME_LIGHT;
    if (mode == APP_THEME_DARK) return CUI_THEME_DARK;
    int dark = settings_system_dark();
    if (dark >= 0) return dark ? CUI_THEME_DARK : CUI_THEME_LIGHT;
    return fallback;
}

static void app_notify(cui_ctx *ui, const char *message, cui_tone tone) {
    settings_status = message;
    settings_status_tone = tone;
    if (ui && message) cui_toast(ui, message, .tone = tone);
}

static void open_settings_folder(cui_ctx *ui, const char *override) {
    char dir[4096];
    if (app_settings_dir(dir, sizeof dir, override, true) && settings_open_url(dir)) {
        app_notify(ui, "Opened settings folder.", CUI_TONE_INFO);
    } else {
        app_notify(ui, "Couldn’t open settings folder.", CUI_TONE_DANGER);
    }
}

static void toggle_fullscreen(void) {
    if (cui_app_ui(NULL)) cui_app_set_fullscreen(NULL, !cui_app_is_fullscreen(NULL));
}

static void trigger_action(cui_ctx *ui, app_action action) {
    switch (action) {
    case APP_ACTION_FIND:
        app_notify(ui, "Find triggered.", CUI_TONE_INFO);
        break;
    case APP_ACTION_SAVE:
        app_notify(ui, "Save triggered.", CUI_TONE_SUCCESS);
        break;
    case APP_ACTION_COMMAND_PALETTE:
        palette_open = true;
        break;
    default:
        break;
    }
}

static void apply_appearance(cui_ctx *ui) {
    bool first = (appearance_ui != ui);
    if (first) appearance_ui = ui;

    if (!env_theme_override) {
        cui_theme_mode current = cui_get_theme_mode(ui);
        cui_theme_mode target = resolve_theme_mode(settings.theme_mode, current);
        cui_set_theme(ui, target);
    }
    if (!env_accent_override) {
        cui_set_accent(ui, settings.accent);
    }
    float scale = settings.font_scale;
    if (scale < FONT_SCALE_MIN || scale > FONT_SCALE_MAX || !isfinite(scale)) scale = 1.0f;
    cui_theme_get(ui)->font_scale = scale;

    if (appearance_force_motion || (first && settings.reduced_motion)) {
        cui_set_reduced_motion(ui, settings.reduced_motion);
        appearance_force_motion = false;
    }
}

static void sync_window_state(cui_ctx *ui, const char *settings_path) {
    if (cui_app_ui(NULL) != ui) return;
    if (window_state_ui != ui) {
        window_state_ui = ui;
        window_fullscreen_pending = false;
        if (settings.window_fullscreen) {
            cui_app_set_fullscreen(NULL, true);
            window_fullscreen_pending = true;
            window_fullscreen_since = cui_time(ui);
        }
    }
    bool fs = cui_app_is_fullscreen(NULL);
    if (window_fullscreen_pending) {
        if (fs || (cui_time(ui) - window_fullscreen_since >= 1.5))
            window_fullscreen_pending = false;
    }
    if (!window_fullscreen_pending) {
        if (fs != settings.window_fullscreen) {
            settings.window_fullscreen = fs;
            window_dirty = true;
            window_dirty_since = cui_time(ui);
        }
        if (!fs) {
            cui_vec2 win = cui_app_size(NULL);
            int w = (int)lroundf(win.x);
            int h = (int)lroundf(win.y);
            if (w >= WINDOW_WIDTH_MIN && w <= WINDOW_WIDTH_MAX &&
                h >= WINDOW_HEIGHT_MIN && h <= WINDOW_HEIGHT_MAX &&
                (w != settings.window_width || h != settings.window_height)) {
                settings.window_width = w;
                settings.window_height = h;
                window_dirty = true;
                window_dirty_since = cui_time(ui);
            }
        }
    }
    if (window_dirty) {
        double elapsed = cui_time(ui) - window_dirty_since;
        if (elapsed >= 0.5) {
            settings_save_failed = !app_settings_save(&settings, settings_path);
            window_dirty = false;
        } else {
            cui_request_wakeup(ui, (float)(0.5 - elapsed) + 0.02f);
        }
    }
}

static bool save_settings(cui_ctx *ui, const char *path) {
    bool prev_failed = settings_save_failed;
    bool ok = app_settings_save(&settings, path);
    settings_save_failed = !ok;
    if (ok) {
        window_dirty = false;
    } else if (!prev_failed) {
        app_notify(ui, "Couldn’t save settings.", CUI_TONE_DANGER);
    }
    return ok;
}

static void draw_command_palette(cui_ctx *ui, const char *settings_path) {
    const char *settings_shortcut = cui_shortcut_label(ui, (cui_key_combo){ CUI_MOD_PRIMARY, CUI_KEY_COMMA });
    const char *find_shortcut = cui_shortcut_label(ui, settings.keybindings[APP_ACTION_FIND].combo);
    const char *save_shortcut = cui_shortcut_label(ui, settings.keybindings[APP_ACTION_SAVE].combo);

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
    int chosen = cui_command_palette(ui, &palette_open, commands, (int)(sizeof commands / sizeof commands[0]));
    switch (chosen) {
    case 0:
        settings_open = true;
        break;
    case 1:
        settings_section = SETTINGS_SECTION_APPEARANCE;
        settings_open = true;
        break;
    case 2:
        settings_section = SETTINGS_SECTION_KEYBINDINGS;
        settings_open = true;
        break;
    case 3:
        settings_section = SETTINGS_SECTION_ABOUT;
        settings_open = true;
        break;
    case 4:
    case 5:
    case 6: {
        app_theme_mode modes[] = { APP_THEME_SYSTEM, APP_THEME_LIGHT, APP_THEME_DARK };
        const char *messages[] = { "Theme set to System.", "Theme set to Light.", "Theme set to Dark." };
        settings.theme_mode = modes[chosen - 4];
        env_theme_override = false;
        apply_appearance(ui);
        if (save_settings(ui, settings_path)) app_notify(ui, messages[chosen - 4], CUI_TONE_INFO);
        break;
    }
    case 7:
        toggle_fullscreen();
        break;
    case 8:
        trigger_action(ui, APP_ACTION_FIND);
        break;
    case 9:
        trigger_action(ui, APP_ACTION_SAVE);
        break;
    case 10:
        open_settings_folder(ui, settings_path);
        break;
    default:
        break;
    }
}

static void frame(cui_ctx *ui, void *user) {
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

    const char *settings_path = user;
    apply_appearance(ui);
    sync_window_state(ui, settings_path);

#ifdef __APPLE__
    app_menu_install(&(app_menu_hooks){
        .settings_open = &settings_open,
        .settings_section = &settings_section,
        .about_section = SETTINGS_SECTION_ABOUT,
        .palette_open = &palette_open,
    });
#else
    cui_menu_bar(ui, "app-menu") {
        cui_menu_bar_menu(ui, "File") {
            if (cui_menu_item(ui, "Save", .icon = CUI_ICON_SAVE,
                              .shortcut = cui_shortcut_label(ui, settings.keybindings[APP_ACTION_SAVE].combo)))
                trigger_action(ui, APP_ACTION_SAVE);
            cui_menu_separator(ui);
            if (cui_menu_item(ui, "Settings…", .icon = CUI_ICON_SETTINGS, .shortcut = "Ctrl ,"))
                settings_open = true;
            cui_menu_separator(ui);
            if (cui_menu_item(ui, "Quit", .icon = CUI_ICON_LOG_OUT, .shortcut = "Ctrl Q"))
                cui_app_quit(NULL);
        }
        cui_menu_bar_menu(ui, "Edit") {
            if (cui_menu_item(ui, "Find", .icon = CUI_ICON_SEARCH,
                              .shortcut = cui_shortcut_label(ui, settings.keybindings[APP_ACTION_FIND].combo)))
                trigger_action(ui, APP_ACTION_FIND);
        }
        cui_menu_bar_menu(ui, "View") {
            if (cui_menu_item(ui, "Command Palette…", .icon = CUI_ICON_COMMAND,
                              .shortcut = cui_shortcut_label(ui, settings.keybindings[APP_ACTION_COMMAND_PALETTE].combo)))
                palette_open = true;
            if (cui_menu_item(ui, "Fullscreen", .icon = CUI_ICON_MAXIMIZE_2,
                              .shortcut = "F11", .checked = cui_app_is_fullscreen(NULL)))
                toggle_fullscreen();
        }
        cui_menu_bar_menu(ui, "Help") {
            if (cui_menu_item(ui, "About " APP_NAME, .icon = CUI_ICON_INFO)) {
                settings_section = SETTINGS_SECTION_ABOUT;
                settings_open = true;
            }
        }
    }
    if (!settings_open && !palette_open && !cui_wants_keyboard(ui)) {
        if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_COMMA)) settings_open = true;
        else if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_Q)) cui_app_quit(NULL);
        else if (cui_shortcut(ui, 0, CUI_KEY_F11)) toggle_fullscreen();
    }
#endif

    if (!settings_open && !palette_open) {
        if (app_action_pressed(ui, &settings, APP_ACTION_COMMAND_PALETTE)) {
            palette_open = true;
        } else if (app_action_pressed(ui, &settings, APP_ACTION_FIND)) {
            trigger_action(ui, APP_ACTION_FIND);
        } else if (app_action_pressed(ui, &settings, APP_ACTION_SAVE)) {
            trigger_action(ui, APP_ACTION_SAVE);
        }
    }

    draw_command_palette(ui, settings_path);

    cui_vec2 size = cui_viewport_size(ui);
    bool compact = size.x < 600.0f;
    cui_dialog(ui, "Settings", &settings_open, .width = size.x * 0.9f) {
        /* shortcut: pinned CUI adds 94pt for the header, padding and gap; revisit if its layout changes. */
        cui_column(ui, .height = CUI_PX(fminf(size.y * 0.9f, size.y - 48) - 94)) {
            cui_row(ui, .height = CUI_FILL, .gap = 16, .align = CUI_ALIGN_STRETCH) {
                cui_column(ui, .width = CUI_PCT(30), .min_width = 128, .max_width = 180,
                           .height = CUI_FILL) {
                    cui_scroll(ui, .id = "settings-sidebar", .gap = 4) {
                        for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
                            if (i == SETTINGS_SECTION_ABOUT) cui_separator(ui);
                            if (cui_nav_item(ui, sections[i], .icon = icons[i],
                                             .selected = settings_section == i,
                                             .group = "settings-sections")) {
                                if (settings_section != i) settings_status = NULL;
                                settings_section = i;
                            }
                        }
                    }
                }
                cui_separator(ui);
                cui_scroll(ui, .id = "settings-page", .width = CUI_FILL) {
                    cui_column(ui, .height = settings_section == SETTINGS_SECTION_KEYBINDINGS ? CUI_FILL : CUI_FIT,
                               .gap = 12) {
                        bool changed = false;
                        cui_heading(ui, sections[settings_section], 4);
                        if (settings_section == SETTINGS_SECTION_GENERAL) {
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16) {
                                cui_text(ui, "Example string", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                int flags = cui_input(ui, "example-string", settings.example_string,
                                                      sizeof settings.example_string, .width = CUI_FILL);
                                if (flags & CUI_CHANGED) {
                                    settings_dirty = true;
                                    settings_status = NULL;
                                }
                                if (settings_dirty && (flags & (CUI_SUBMITTED | CUI_BLURRED))) changed = true;
                            }
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16) {
                                cui_text(ui, "Example number (0–100)", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                double previous = settings.example_number;
                                if (cui_number(ui, "example-number", &settings.example_number,
                                               EXAMPLE_NUMBER_MIN, EXAMPLE_NUMBER_MAX, .width = CUI_FILL)) {
                                    if (isfinite(settings.example_number)) {
                                        changed = true;
                                        settings_status = NULL;
                                    } else {
                                        settings.example_number = previous;
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
                                        if (settings_file_dialog(&(cui_file_dialog_opts){
                                                .kind = CUI_FILE_OPEN,
                                                .title = "Import Settings",
                                                .filter_name = "JSON",
                                                .filter = "json",
                                            }, path, sizeof path)) {
                                            if (app_settings_load(&settings, path)) {
                                                env_theme_override = false;
                                                env_accent_override = false;
                                                appearance_force_motion = true;
                                                apply_appearance(ui);
                                                settings_dirty = false;
                                                changed = false;
                                                if (save_settings(ui, settings_path))
                                                    app_notify(ui, "Settings imported.", CUI_TONE_SUCCESS);
                                            } else {
                                                app_notify(ui, "Couldn’t import settings.", CUI_TONE_DANGER);
                                            }
                                        }
                                    }
                                    if (cui_button(ui, "Export…", .icon = CUI_ICON_DOWNLOAD)) {
                                        char path[4096];
                                        if (settings_file_dialog(&(cui_file_dialog_opts){
                                                .kind = CUI_FILE_SAVE,
                                                .title = "Export Settings",
                                                .filter_name = "JSON",
                                                .filter = "json",
                                                .default_name = "settings.json",
                                            }, path, sizeof path)) {
                                            if (app_settings_save(&settings, path))
                                                app_notify(ui, "Settings exported.", CUI_TONE_SUCCESS);
                                            else
                                                app_notify(ui, "Couldn’t export settings.", CUI_TONE_DANGER);
                                        }
                                    }
                                    if (cui_button(ui, "Open Folder", .icon = CUI_ICON_FOLDER_OPEN)) {
                                        open_settings_folder(ui, settings_path);
                                    }
                                }
                            }
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16) {
                                cui_text(ui, "Reset settings", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                if (cui_confirm_button(ui, "Reset",
                                                       .icon = CUI_ICON_ROTATE_CCW,
                                                       .confirm_label = "Confirm")) {
                                    settings = default_settings;
                                    env_theme_override = false;
                                    env_accent_override = false;
                                    appearance_force_motion = true;
                                    apply_appearance(ui);
                                    if (cui_app_ui(NULL) == ui) {
                                        if (cui_app_is_fullscreen(NULL)) cui_app_set_fullscreen(NULL, false);
                                        cui_app_set_size(NULL, settings.window_width, settings.window_height);
                                    }
                                    settings_dirty = false;
                                    changed = false;
                                    if (save_settings(ui, settings_path))
                                        app_notify(ui, "Settings reset to defaults.", CUI_TONE_INFO);
                                }
                            }
                        } else if (settings_section == SETTINGS_SECTION_APPEARANCE) {
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16,
                                    .align = compact ? CUI_ALIGN_STRETCH : CUI_ALIGN_CENTER) {
                                cui_text(ui, "Theme", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                int mode = (int)settings.theme_mode;
                                bool theme_changed = compact
                                    ? cui_select(ui, "theme-mode", theme_labels, APP_THEME_COUNT, &mode,
                                                 .icons = theme_icons, .width = CUI_FILL)
                                    : cui_segmented(ui, "theme-mode", theme_labels, APP_THEME_COUNT, &mode,
                                                    .icons = theme_icons);
                                if (theme_changed) {
                                    settings.theme_mode = (app_theme_mode)mode;
                                    env_theme_override = false;
                                    apply_appearance(ui);
                                    changed = true;
                                }
                            }
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16,
                                    .align = compact ? CUI_ALIGN_STRETCH : CUI_ALIGN_CENTER) {
                                cui_text(ui, "Accent color", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                int accent = (int)settings.accent;
                                if (cui_select(ui, "accent-color", accent_labels, CUI_ACCENT_COUNT, &accent,
                                               .width = CUI_FILL)) {
                                    settings.accent = (cui_accent)accent;
                                    env_accent_override = false;
                                    apply_appearance(ui);
                                    changed = true;
                                }
                            }
                            cui_box(ui, .dir = compact ? CUI_COLUMN : CUI_ROW, .gap = compact ? 6 : 16,
                                    .align = compact ? CUI_ALIGN_STRETCH : CUI_ALIGN_CENTER) {
                                cui_text(ui, "Text scale", .font = CUI_FONT_MEDIUM,
                                         .width = compact ? CUI_FILL : CUI_PX(168));
                                float percent = roundf(settings.font_scale * 100.0f);
                                if (cui_slider(ui, "##text-scale", &percent,
                                               FONT_SCALE_MIN * 100.0f, FONT_SCALE_MAX * 100.0f,
                                               .step = 5.0f, .show_value = true, .format = "%.0f%%",
                                               .width = CUI_FILL)) {
                                    settings.font_scale = percent / 100.0f;
                                    apply_appearance(ui);
                                    changed = true;
                                }
                            }
                            cui_separator(ui);
                            if (cui_switch(ui, "Reduce motion", &settings.reduced_motion,
                                           .description = "Disable UI animations")) {
                                appearance_force_motion = true;
                                apply_appearance(ui);
                                changed = true;
                            }
                        } else if (settings_section == SETTINGS_SECTION_KEYBINDINGS) {
                            cui_table_column columns[] = {
                                { "Action", CUI_FILL, CUI_TEXT_LEFT, false },
                                { "Keys", CUI_PX(240), CUI_TEXT_LEFT, false },
                            };
                            cui_secondary(ui, "Click a shortcut, then press a key combination. Backspace clears.");
                            cui_search(ui, "keybinding-filter", keybinding_filter, sizeof keybinding_filter,
                                       .placeholder = "Filter actions or keys...", .width = CUI_FILL);
                            bool has_conflict = false;
                            for (int i = 0; i < APP_ACTION_COUNT; ++i) {
                                if (app_keybinding_conflict(&settings, (app_action)i) != APP_CONFLICT_NONE) {
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
                                    const char *action = settings.keybindings[i].action;
                                    cui_key_combo *combo = &settings.keybindings[i].combo;
                                    if (!contains_ignore_case(action, keybinding_filter) &&
                                        !contains_ignore_case(cui_shortcut_label(ui, *combo), keybinding_filter)) continue;
                                    ++visible;
                                    int conflict = app_keybinding_conflict(&settings, (app_action)i);
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
                            if (app_settings_dir(dir, sizeof dir, settings_path, false)) {
                                cui_secondary(ui, dir);
                            }
                            cui_row(ui, .gap = 8, .wrap = true) {
                                if (cui_button(ui, "Open Settings Folder", .icon = CUI_ICON_FOLDER_OPEN)) {
                                    open_settings_folder(ui, settings_path);
                                }
                            }
                        }
                        if (changed || (settings_dirty && settings_section != SETTINGS_SECTION_GENERAL)) {
                            save_settings(ui, settings_path);
                            settings_dirty = false;
                        }
                        if (settings_save_failed) cui_secondary(ui, "Couldn’t save settings.");
                    }
                }
            }
        }
    }
    if (!settings_open) {
        if (prev_settings_open) settings_status = NULL;
        if (settings_dirty) {
            save_settings(ui, settings_path);
            settings_dirty = false;
        }
    }
    prev_settings_open = settings_open;
}

static app_cli_action app_parse_cli(int argc, char **argv, app_cli_opts *opts, FILE *out, FILE *err) {
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

int main(int argc, char **argv) {
    app_cli_opts cli;
    app_cli_action action = app_parse_cli(argc, argv, &cli, stdout, stderr);
    if (action == APP_CLI_EXIT_OK) return 0;
    if (action == APP_CLI_EXIT_ERROR) return 2;

    app_settings_load(&settings, cli.settings_path);
    const char *env_theme = getenv("CUI_THEME");
    const char *env_accent = getenv("CUI_ACCENT");
    env_theme_override = env_theme && env_theme[0] != '\0';
    env_accent_override = env_accent && env_accent[0] != '\0';

    int status = cui_app_run(&(cui_app_desc){
        .title = APP_NAME,
        .width = settings.window_width,
        .height = settings.window_height,
        .renderer = cli.software_renderer ? CUI_RENDERER_SOFTWARE : CUI_RENDERER_AUTO,
        .theme = resolve_theme_mode(settings.theme_mode, CUI_THEME_LIGHT),
        .accent = settings.accent,
        .follow_system_theme = false,
    }, frame, (void *)cli.settings_path);

    if (settings_dirty || window_dirty) app_settings_save(&settings, cli.settings_path);
    return status;
}
