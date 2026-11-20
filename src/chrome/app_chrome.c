#include "chrome_internal.h"
#include "platform.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    .updates = {
        .check_on_startup = true,
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

void chrome_open_log_file(app_chrome *chrome, cui_ctx *ui) {
    const char *path = app_log_path();
    if (!path || !path[0] || !cui_app_open_url(path)) {
        app_chrome_notify(chrome, ui, "Couldn’t open log file.", CUI_TONE_DANGER);
    }
}

void chrome_format_debug_info(const app_chrome *chrome, cui_ctx *ui, char *out, size_t cap) {
    if (!out || cap == 0) return;
    static const char *const mode_names[APP_THEME_COUNT] = { "System", "Light", "Dark" };
    char os_info[128] = "unknown";
    app_os_info(os_info, sizeof os_info);

    const char *backend = cui_app_backend(NULL);
    const char *renderer = cui_app_is_software(NULL) ? "Software" : "OpenGL";
    const char *resolved_theme = (ui && cui_get_theme_mode(ui) == CUI_THEME_DARK) ? "Dark" : "Light";
    app_theme_mode mode = chrome ? chrome->settings.appearance.theme_mode : APP_THEME_SYSTEM;
    const char *mode_name = (mode >= 0 && mode < APP_THEME_COUNT) ? mode_names[mode] : "System";
    int scale_pct = chrome ? (int)lroundf(chrome->settings.appearance.font_scale * 100.0f) : 100;
    cui_vec2 win = cui_app_ui(NULL) ? cui_app_size(NULL) : (cui_vec2){ 0, 0 };
    int win_w = (int)lroundf(win.x);
    int win_h = (int)lroundf(win.y);
    if (win_w <= 0 && chrome) win_w = chrome->settings.window.width;
    if (win_h <= 0 && chrome) win_h = chrome->settings.window.height;

    char settings_dir[1024] = "";
    if (chrome) app_settings_dir(settings_dir, sizeof settings_dir, chrome->cli.settings_path, false);
    const char *log_path = app_log_path();

    snprintf(out, cap,
             "App: %s %s\n"
             "Bundle ID: %s\n"
             "OS: %s\n"
             "Backend: %s (%s)\n"
             "Theme: %s (%s) · Scale: %d%%\n"
             "Window: %dx%d%s\n"
             "Settings dir: %s\n"
             "Log file: %s",
             APP_NAME, APP_VERSION,
             APP_BUNDLE_ID[0] ? APP_BUNDLE_ID : "none",
             os_info,
             backend ? backend : "unknown", renderer,
             resolved_theme, mode_name, scale_pct,
             win_w, win_h, (cui_app_ui(NULL) && cui_app_is_fullscreen(NULL)) ? " (fullscreen)" : "",
             settings_dir[0] ? settings_dir : "unknown",
             (log_path && log_path[0]) ? log_path : "disabled");
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
    if (chrome->updater) {
        app_updater_destroy(chrome->updater);
        chrome->updater = NULL;
    }
    app_log_info("Shutting down %s", APP_NAME);
    app_log_shutdown();
}

static app_updater *chrome_ensure_updater(app_chrome *chrome) {
    if (!chrome->updater && app_updater_configured()) {
        chrome->updater = app_updater_create(APP_GITHUB_REPO, APP_VERSION);
    }
    return chrome->updater;
}

void chrome_check_for_updates(app_chrome *chrome, bool silent) {
    app_updater *updater = chrome_ensure_updater(chrome);
    if (updater) {
        chrome->update_auto_checked = true;
        if (app_updater_check(updater)) {
            chrome->update_check_pending = true;
            chrome->update_check_silent = silent;
        }
    }
}

void chrome_start_update(app_chrome *chrome, cui_ctx *ui) {
    app_update_snapshot snap;
    chrome_updater_snapshot(chrome, &snap);
    if (!snap.has_update) return;

    chrome->update_check_silent = false;
    if (app_updater_can_self_update()) {
        app_updater *updater = chrome_ensure_updater(chrome);
        if (updater && app_updater_start_install(updater)) {
            if (!(chrome->settings_open && chrome->settings_section == SETTINGS_SECTION_ABOUT)) {
                const char *ver = snap.latest_tag[0] ? snap.latest_tag : snap.latest_version;
                char msg[96];
                snprintf(msg, sizeof msg, "Updating to %s…", ver);
                app_chrome_notify(chrome, ui, msg, CUI_TONE_INFO);
            }
        }
        return;
    }

    if (snap.release_url[0] && cui_app_open_url(snap.release_url)) {
        app_chrome_notify(chrome, ui, "Opened release page in browser.", CUI_TONE_INFO);
    } else {
        app_chrome_notify(chrome, ui, "Couldn’t open release page.", CUI_TONE_DANGER);
    }
}

void chrome_updater_snapshot(app_chrome *chrome, app_update_snapshot *out) {
    app_updater_snapshot(chrome ? chrome->updater : NULL, out);
}

static void chrome_poll_updater(app_chrome *chrome, cui_ctx *ui) {
    if (chrome->update_toast_handle > 0 &&
        cui_toast_action_clicked(ui, chrome->update_toast_handle)) {
        cui_toast_dismiss(ui, chrome->update_toast_handle);
        chrome->update_toast_handle = 0;
        chrome_start_update(chrome, ui);
    }

    if (!chrome->updater) return;
    app_update_snapshot snap;
    app_updater_snapshot(chrome->updater, &snap);

    if (snap.state == APP_UPDATE_CHECKING || snap.state == APP_UPDATE_UPDATING) {
        cui_request_wakeup(ui, 0.1f);
    }

    if (chrome->update_check_pending) {
        if (snap.state == APP_UPDATE_UP_TO_DATE || snap.state == APP_UPDATE_AVAILABLE) {
            chrome->update_check_pending = false;
            int64_t now = (int64_t)time(NULL);
            if (now > 0) {
                chrome->settings.updates.last_checked = now;
                chrome_save_settings(chrome, ui);
            }
        } else if (snap.state == APP_UPDATE_ERROR) {
            chrome->update_check_pending = false;
        }
    }

    bool about_visible = chrome->settings_open && chrome->settings_section == SETTINGS_SECTION_ABOUT;
    if (snap.state == APP_UPDATE_AVAILABLE && snap.has_update && !chrome->update_notified) {
        chrome->update_notified = true;
        if (!about_visible) {
            const char *ver = snap.latest_tag[0] ? snap.latest_tag : snap.latest_version;
            char title[96];
            snprintf(title, sizeof title, "Update available: %s", ver);
            const char *desc = app_updater_can_self_update()
                ? "Click Update to download and relaunch."
                : "Click Update to open the release page.";
            chrome->update_toast_handle = cui_toast(ui, title,
                .description = desc,
                .tone = CUI_TONE_INFO,
                .duration = 8.0f,
                .action = "Update");
        }
    } else if (snap.state == APP_UPDATE_READY_TO_RELAUNCH && !chrome->update_relaunching) {
        chrome->update_relaunching = true;
        capture_live_window_bounds(chrome);
        save_if_dirty(chrome);
        if (app_updater_relaunch(snap.relaunch_path)) {
            cui_app_quit(NULL);
        } else {
            app_chrome_notify(chrome, ui, "Update installed. Please restart the app.", CUI_TONE_SUCCESS);
        }
    } else if (snap.state == APP_UPDATE_ERROR &&
               chrome->last_update_state != APP_UPDATE_ERROR &&
               !chrome->update_check_silent &&
               !about_visible) {
        app_chrome_notify(chrome, ui,
                          snap.error[0] ? snap.error : "Update failed.",
                          CUI_TONE_DANGER);
    }
    chrome->last_update_state = (int)snap.state;
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
        const char *backend = cui_app_backend(NULL);
        app_log_info("Window ready (backend=%s, renderer=%s)",
                     backend ? backend : "unknown",
                     cui_app_is_software(NULL) ? "Software" : "OpenGL");
        app_window_apply_metadata(cui_app_native_window(NULL), backend);
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
        if (!chrome->update_startup_checked &&
            !chrome->env_screenshot_mode &&
            chrome->settings.updates.check_on_startup &&
            app_updater_configured()) {
            chrome->update_startup_checked = true;
            if (app_updater_check_due(chrome->settings.updates.last_checked, (int64_t)time(NULL))) {
                chrome_check_for_updates(chrome, true);
            }
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
    chrome_poll_updater(chrome, ui);
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
    char log_dir[4096];
    if (app_settings_dir(log_dir, sizeof log_dir, chrome->cli.settings_path, true)) {
        char log_path[4096];
        size_t dlen = strlen(log_dir);
#ifdef _WIN32
        const char *sep = (dlen > 0 && (log_dir[dlen - 1] == '/' || log_dir[dlen - 1] == '\\')) ? "" : "\\";
#else
        const char *sep = (dlen > 0 && log_dir[dlen - 1] == '/') ? "" : "/";
#endif
        int n = snprintf(log_path, sizeof log_path, "%s%sapp.log", log_dir, sep);
        if (n > 0 && (size_t)n < sizeof log_path) {
            app_log_init(log_path, 0);
        }
    }
    app_log_info("Starting %s %s", APP_NAME, APP_VERSION);
    app_settings_load(&chrome->settings, chrome->cli.settings_path);
    const char *env_theme = getenv("CUI_THEME");
    const char *env_accent = getenv("CUI_ACCENT");
    const char *env_screenshot = getenv("CUI_SCREENSHOT");
    chrome->env_theme_override = env_theme && env_theme[0] != '\0';
    chrome->env_accent_override = env_accent && env_accent[0] != '\0';
    chrome->env_screenshot_mode = env_screenshot && env_screenshot[0] != '\0';
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
