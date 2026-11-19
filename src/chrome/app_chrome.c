#include "chrome_internal.h"
#include "platform.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include "app_menu.h"
#endif

const char *const chrome_theme_icons[APP_THEME_COUNT] = {
    CUI_ICON_MONITOR, CUI_ICON_SUN, CUI_ICON_MOON
};

const char *const chrome_section_labels[SETTINGS_SECTION_COUNT] = {
    "General", "Appearance", "Keybindings", "About"
};

const char *const chrome_section_icons[SETTINGS_SECTION_COUNT] = {
    CUI_ICON_SETTINGS, CUI_ICON_PALETTE, CUI_ICON_KEYBOARD, CUI_ICON_INFO
};

static const app_settings k_default_settings = {
    .example_string = "Example",
    .example_number = 42,
    .appearance = {
        .theme_mode = APP_THEME_SYSTEM,
        .accent = CUI_ACCENT_NEUTRAL,
        .font_scale = 1.0f,
        .reduced_motion = false,
    },
    .window = {
        .has_pos = false,
        .x = 0,
        .y = 0,
        .width = 900,
        .height = 600,
        .fullscreen = false,
    },
    .keybindings = {
        [APP_ACTION_FIND] = { "Find", "find", { CUI_MOD_PRIMARY, CUI_KEY_F } },
        [APP_ACTION_SAVE] = { "Save", "save", { CUI_MOD_PRIMARY, CUI_KEY_S } },
        [APP_ACTION_COMMAND_PALETTE] = { "Command Palette", "command_palette", { CUI_MOD_PRIMARY, CUI_KEY_K } },
    },
};

app_settings chrome_default_settings(void) {
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

static cui_theme_mode resolve_theme_mode(app_theme_mode mode, cui_theme_mode fallback) {
    if (mode == APP_THEME_LIGHT) return CUI_THEME_LIGHT;
    if (mode == APP_THEME_DARK) return CUI_THEME_DARK;
    int dark = app_system_dark();
    if (dark >= 0) return dark ? CUI_THEME_DARK : CUI_THEME_LIGHT;
    return fallback;
}

void app_chrome_notify(app_chrome *chrome, cui_ctx *ui, const char *message, cui_tone tone) {
    (void)chrome;
    if (ui && message) cui_toast(ui, message, .tone = tone);
}

const app_settings *app_chrome_settings(const app_chrome *chrome) {
    return &chrome->settings;
}

void chrome_open_settings_folder(app_chrome *chrome, cui_ctx *ui) {
    char dir[4096];
    if (!app_settings_dir(dir, sizeof dir, chrome->cli.settings_path, true) || !cui_app_open_url(dir)) {
        app_chrome_notify(chrome, ui, "Couldn’t open settings folder.", CUI_TONE_DANGER);
    }
}

void chrome_toggle_fullscreen(void) {
    if (cui_app_ui(NULL)) cui_app_set_fullscreen(NULL, !cui_app_is_fullscreen(NULL));
}

void chrome_trigger_action(app_chrome *chrome, cui_ctx *ui, app_action action) {
    if (action == APP_ACTION_COMMAND_PALETTE) {
        chrome->palette_open = true;
        return;
    }
    if (chrome->on_action) {
        chrome->on_action(ui, action, chrome->on_action_user);
    }
}

void chrome_apply_appearance(app_chrome *chrome, cui_ctx *ui) {
    bool first = (chrome->appearance_ui != ui);
    if (first) chrome->appearance_ui = ui;

    if (!chrome->env_theme_override) {
        cui_theme_mode current = cui_get_theme_mode(ui);
        cui_theme_mode target = resolve_theme_mode(chrome->settings.appearance.theme_mode, current);
        cui_set_theme(ui, target);
    }
    if (!chrome->env_accent_override) {
        cui_set_accent(ui, chrome->settings.appearance.accent);
    }
    float scale = chrome->settings.appearance.font_scale;
    if (scale < FONT_SCALE_MIN || scale > FONT_SCALE_MAX || !isfinite(scale)) scale = 1.0f;
    cui_theme_get(ui)->font_scale = scale;

    if (chrome->appearance_force_motion || (first && chrome->settings.appearance.reduced_motion)) {
        cui_set_reduced_motion(ui, chrome->settings.appearance.reduced_motion);
        chrome->appearance_force_motion = false;
    }
}

void chrome_reload_appearance(app_chrome *chrome, cui_ctx *ui) {
    chrome->env_theme_override = false;
    chrome->env_accent_override = false;
    chrome->appearance_force_motion = true;
    chrome_apply_appearance(chrome, ui);
}

bool chrome_save_settings(app_chrome *chrome, cui_ctx *ui) {
    bool prev_failed = chrome->settings_save_failed;
    bool ok = app_settings_save(&chrome->settings, chrome->cli.settings_path);
    chrome->settings_save_failed = !ok;
    chrome->settings_dirty = false;
    if (ok) {
        chrome->window_dirty = false;
    } else if (!prev_failed) {
        app_chrome_notify(chrome, ui, "Couldn’t save settings.", CUI_TONE_DANGER);
    }
    return ok;
}

static void observe_window_pos(app_chrome *chrome) {
    int x = 0, y = 0;
    chrome->observed_window_has_pos =
        app_window_get_pos(cui_app_native_window(NULL), cui_app_backend(NULL), &x, &y);
    chrome->observed_window_x = x;
    chrome->observed_window_y = y;
}

void chrome_reset_window(app_chrome *chrome, cui_ctx *ui) {
    if (cui_app_ui(NULL) != ui) return;
    if (cui_app_is_fullscreen(NULL)) cui_app_set_fullscreen(NULL, false);
    cui_app_set_size(NULL, chrome->settings.window.width, chrome->settings.window.height);
    app_window_center(cui_app_native_window(NULL), cui_app_backend(NULL));
    observe_window_pos(chrome);
}

static bool capture_live_window_bounds(app_chrome *chrome) {
    if (!cui_app_ui(NULL) || chrome->window_fullscreen_pending) return false;
    bool changed = false;
    bool fs = cui_app_is_fullscreen(NULL);
    if (fs != chrome->settings.window.fullscreen) {
        chrome->settings.window.fullscreen = fs;
        changed = true;
    }
    if (!fs) {
        cui_vec2 win = cui_app_size(NULL);
        int w = (int)lroundf(win.x);
        int h = (int)lroundf(win.y);
        if (w >= WINDOW_WIDTH_MIN && w <= WINDOW_WIDTH_MAX &&
            h >= WINDOW_HEIGHT_MIN && h <= WINDOW_HEIGHT_MAX &&
            (w != chrome->settings.window.width || h != chrome->settings.window.height)) {
            chrome->settings.window.width = w;
            chrome->settings.window.height = h;
            changed = true;
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
                if (!chrome->settings.window.has_pos ||
                    x != chrome->settings.window.x || y != chrome->settings.window.y) {
                    chrome->settings.window.has_pos = true;
                    chrome->settings.window.x = x;
                    chrome->settings.window.y = y;
                    changed = true;
                }
            }
        }
    }
    if (changed) chrome->window_dirty = true;
    return changed;
}

static void save_if_dirty(app_chrome *chrome) {
    if (chrome->settings_dirty || chrome->window_dirty) {
        app_settings_save(&chrome->settings, chrome->cli.settings_path);
        chrome->settings_dirty = false;
        chrome->window_dirty = false;
    }
}

void app_chrome_shutdown(app_chrome *chrome) {
    save_if_dirty(chrome);
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
    chrome_toggle_fullscreen();
}

static void on_menu_will_terminate(void *user) {
    app_chrome *chrome = user;
    capture_live_window_bounds(chrome);
    save_if_dirty(chrome);
}
#endif

static void sync_window_state(app_chrome *chrome, cui_ctx *ui) {
    if (cui_app_ui(NULL) != ui) return;
    if (chrome->window_state_ui != ui) {
        chrome->window_state_ui = ui;
        chrome->window_fullscreen_pending = false;
        if (chrome->settings.window.has_pos) {
            app_window_set_pos(cui_app_native_window(NULL), cui_app_backend(NULL),
                               chrome->settings.window.x, chrome->settings.window.y);
        }
        observe_window_pos(chrome);
        if (chrome->settings.window.fullscreen) {
            cui_app_set_fullscreen(NULL, true);
            chrome->window_fullscreen_pending = true;
            chrome->window_fullscreen_since = cui_time(ui);
        }
    }
    if (chrome->window_fullscreen_pending) {
        if (cui_app_is_fullscreen(NULL) || (cui_time(ui) - chrome->window_fullscreen_since >= 1.5))
            chrome->window_fullscreen_pending = false;
    }
    if (!chrome->window_fullscreen_pending && capture_live_window_bounds(chrome)) {
        chrome->window_dirty_since = cui_time(ui);
    }
    if (chrome->window_dirty) {
        double elapsed = cui_time(ui) - chrome->window_dirty_since;
        if (elapsed >= 0.5) {
            chrome->settings_save_failed = !app_settings_save(&chrome->settings, chrome->cli.settings_path);
            chrome->window_dirty = false;
        } else {
            cui_request_wakeup(ui, (float)(0.5 - elapsed) + 0.02f);
        }
    }
}

void app_chrome_begin_frame(app_chrome *chrome, cui_ctx *ui) {
    chrome_apply_appearance(chrome, ui);
    sync_window_state(chrome, ui);

#ifdef __APPLE__
    chrome->injected_edit_key = CUI_KEY_NONE;
    if (chrome->pending_edit_key != CUI_KEY_NONE) {
        chrome->injected_edit_key = chrome->pending_edit_key;
        chrome->pending_edit_key = CUI_KEY_NONE;
        cui_input_key(ui, chrome->injected_edit_key, true, CUI_MOD_PRIMARY);
    }

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
        .fullscreen = cui_app_ui(NULL) ? cui_app_is_fullscreen(NULL) : chrome->settings.window.fullscreen,
    });
    if (!chrome->settings_open && !chrome->palette_open && !cui_wants_keyboard(ui)) {
        if (cui_shortcut(ui, CUI_MOD_PRIMARY | CUI_MOD_CTRL, CUI_KEY_F)) chrome_toggle_fullscreen();
    }
    if (chrome->pending_action >= 0 && chrome->pending_action < APP_ACTION_COUNT) {
        app_action queued = (app_action)chrome->pending_action;
        chrome->pending_action = -1;
        chrome_trigger_action(chrome, ui, queued);
    }
#else
    cui_menu_bar(ui, "app-menu") {
        cui_menu_bar_menu(ui, "File") {
            if (cui_menu_item(ui, "Save", .icon = CUI_ICON_SAVE,
                              .shortcut = cui_shortcut_label(ui, chrome->settings.keybindings[APP_ACTION_SAVE].combo)))
                chrome_trigger_action(chrome, ui, APP_ACTION_SAVE);
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
                chrome_trigger_action(chrome, ui, APP_ACTION_FIND);
        }
        cui_menu_bar_menu(ui, "View") {
            if (cui_menu_item(ui, "Command Palette…", .icon = CUI_ICON_COMMAND,
                              .shortcut = cui_shortcut_label(ui, chrome->settings.keybindings[APP_ACTION_COMMAND_PALETTE].combo)))
                chrome->palette_open = true;
            if (cui_menu_item(ui, "Fullscreen", .icon = CUI_ICON_MAXIMIZE_2,
                              .shortcut = "F11", .checked = cui_app_is_fullscreen(NULL)))
                chrome_toggle_fullscreen();
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
        else if (cui_shortcut(ui, 0, CUI_KEY_F11)) chrome_toggle_fullscreen();
    }
#endif

    if (!chrome->settings_open && !chrome->palette_open) {
        if (app_action_pressed(ui, &chrome->settings, APP_ACTION_COMMAND_PALETTE)) {
            chrome->palette_open = true;
        } else if (app_action_pressed(ui, &chrome->settings, APP_ACTION_FIND)) {
            chrome_trigger_action(chrome, ui, APP_ACTION_FIND);
        } else if (app_action_pressed(ui, &chrome->settings, APP_ACTION_SAVE)) {
            chrome_trigger_action(chrome, ui, APP_ACTION_SAVE);
        }
    }
}

void app_chrome_end_frame(app_chrome *chrome, cui_ctx *ui) {
    chrome_draw_command_palette(chrome, ui);
    chrome_draw_settings_dialog(chrome, ui);
#ifdef __APPLE__
    if (chrome->injected_edit_key != CUI_KEY_NONE) {
        cui_input_key(ui, chrome->injected_edit_key, false, 0);
        cui_input_key(ui, CUI_KEY_NONE, false, 0);
        chrome->injected_edit_key = CUI_KEY_NONE;
    }
#endif
}

static bool parse_cli(int argc, char **argv, app_cli_opts *opts, int *exit_code) {
    memset(opts, 0, sizeof *opts);
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strncmp(arg, "-psn_", 5) == 0) {
            /* macOS LaunchServices passes -psn_* when launching a bundle. */
            continue;
        }
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            fprintf(stdout,
                    "Usage: %s [options]\n\n"
                    "Options:\n"
                    "  -h, --help             Show this help message and exit\n"
                    "  -v, --version          Print version information and exit\n"
                    "  --settings <path>      Use a custom settings.json file\n"
                    "  --software             Force the software renderer\n",
                    argc > 0 && argv[0] ? argv[0] : APP_NAME);
            if (exit_code) *exit_code = 0;
            return false;
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            fprintf(stdout, "%s %s\n", APP_NAME, APP_VERSION);
            if (exit_code) *exit_code = 0;
            return false;
        }
        if (strcmp(arg, "--software") == 0) {
            opts->software_renderer = true;
            continue;
        }
        const char *settings_arg = NULL;
        if (strcmp(arg, "--settings") == 0) {
            settings_arg = (i + 1 < argc) ? argv[++i] : "";
        } else if (strncmp(arg, "--settings=", 11) == 0) {
            settings_arg = arg + 11;
        }
        if (settings_arg) {
            if (!settings_arg[0]) {
                fprintf(stderr, "Missing path for --settings\n");
                if (exit_code) *exit_code = 2;
                return false;
            }
            opts->settings_path = settings_arg;
            continue;
        }
        fprintf(stderr, "Unknown option: %s\nTry --help for usage.\n", arg);
        if (exit_code) *exit_code = 2;
        return false;
    }
    return true;
}

bool app_chrome_init(app_chrome *chrome, int argc, char **argv, int *exit_code) {
    app_cli_opts cli;
    if (!parse_cli(argc, argv, &cli, exit_code)) return false;

    memset(chrome, 0, sizeof *chrome);
    chrome->settings = k_default_settings;
    chrome->cli = cli;
#ifdef __APPLE__
    chrome->pending_action = -1;
#endif
    app_settings_load(&chrome->settings, chrome->cli.settings_path);
    const char *env_theme = getenv("CUI_THEME");
    const char *env_accent = getenv("CUI_ACCENT");
    chrome->env_theme_override = env_theme && env_theme[0] != '\0';
    chrome->env_accent_override = env_accent && env_accent[0] != '\0';
    return true;
}

cui_app_desc app_chrome_app_desc(const app_chrome *chrome) {
    return (cui_app_desc){
        .title = APP_NAME,
        .width = chrome->settings.window.width,
        .height = chrome->settings.window.height,
        .renderer = chrome->cli.software_renderer ? CUI_RENDERER_SOFTWARE : CUI_RENDERER_AUTO,
        .theme = resolve_theme_mode(chrome->settings.appearance.theme_mode, CUI_THEME_LIGHT),
        .accent = chrome->settings.appearance.accent,
        .follow_system_theme = false,
    };
}

void app_chrome_set_action_handler(app_chrome *chrome, app_action_handler handler, void *user) {
    chrome->on_action = handler;
    chrome->on_action_user = user;
}
