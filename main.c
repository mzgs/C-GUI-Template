#include <cui/cui_app.h>
#include <cui/cui.h>
#include <math.h>
#include <ctype.h>
#include "settings.h"

#ifdef __APPLE__
#include "macos_menu.h"
#endif

static bool settings_open;
static bool settings_save_failed;
static bool settings_dirty;
static const char *settings_status;
static int settings_section;
static char keybinding_filter[128];

static bool default_file_dialog(const cui_file_dialog_opts *opts, char *out, size_t cap) {
    return cui_app_file_dialog(NULL, opts, out, cap);
}

static bool (*settings_file_dialog)(const cui_file_dialog_opts *opts, char *out, size_t cap) = default_file_dialog;
#define APP_SETTINGS_DEFAULT { \
    .example_string = "Example", \
    .example_number = 42, \
    .keybindings = { \
        { "Find", "find", { CUI_MOD_PRIMARY, CUI_KEY_F } }, \
        { "Save", "save", { CUI_MOD_PRIMARY, CUI_KEY_S } }, \
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

static void frame(cui_ctx *ui, void *user) {
    static const char *const sections[] = { "General", "Keybindings", "About" };
    static const char *const icons[] = { CUI_ICON_SETTINGS, CUI_ICON_KEYBOARD, CUI_ICON_INFO };
#ifdef __APPLE__
    app_menu_install(&settings_open);
#else
    cui_row(ui, .pad = 12) {
        if (cui_button(ui, "Settings", .icon = CUI_ICON_SETTINGS)) settings_open = true;
    }
    if (!settings_open && !cui_wants_keyboard(ui) &&
        cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_COMMA))
        settings_open = true;
#endif
    cui_vec2 size = cui_viewport_size(ui);
    bool compact = size.x < 600.0f;
    cui_dialog(ui, "Settings", &settings_open, .width = size.x * 0.9f) {
        /* shortcut: pinned CUI adds 94pt for the header, padding and gap; revisit if its layout changes. */
        cui_column(ui, .height = CUI_PX(fminf(size.y * 0.9f, size.y - 48) - 94)) {
            cui_row(ui, .height = CUI_FILL, .gap = 16, .align = CUI_ALIGN_STRETCH) {
                cui_column(ui, .width = CUI_PCT(30), .min_width = 128, .max_width = 180,
                           .height = CUI_FILL) {
                    cui_scroll(ui, .id = "settings-sidebar", .gap = 4) {
                        for (int i = 0; i < 3; ++i) {
                            if (i == 2) cui_separator(ui);
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
                    cui_column(ui, .height = settings_section == 1 ? CUI_FILL : CUI_FIT, .gap = 12) {
                        bool changed = false;
                        cui_heading(ui, sections[settings_section], 4);
                        if (settings_section == 0) {
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
                                                settings_save_failed = !app_settings_save(&settings, user);
                                                settings_dirty = false;
                                                changed = false;
                                                settings_status = settings_save_failed ? NULL : "Settings imported.";
                                            } else {
                                                settings_status = "Couldn’t import settings.";
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
                                            settings_status = app_settings_save(&settings, path)
                                                ? "Settings exported."
                                                : "Couldn’t export settings.";
                                        }
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
                                    settings_save_failed = !app_settings_save(&settings, user);
                                    settings_dirty = false;
                                    changed = false;
                                    settings_status = settings_save_failed ? NULL : "Settings reset to defaults.";
                                }
                            }
                            if (settings_status) cui_secondary(ui, settings_status);
                        } else if (settings_section == 1) {
                            cui_table_column columns[] = {
                                { "Action", CUI_FILL, CUI_TEXT_LEFT, false },
                                { "Keys", CUI_PX(240), CUI_TEXT_LEFT, false },
                            };
                            cui_secondary(ui, "Click a shortcut, then press a key combination. Backspace clears.");
                            cui_search(ui, "keybinding-filter", keybinding_filter, sizeof keybinding_filter,
                                       .placeholder = "Filter actions or keys...", .width = CUI_FILL);
                            int visible = 0;
                            cui_table(ui, "keybindings", columns, 2, .striped = true, .compact = true,
                                      .height = CUI_FILL) {
                                for (size_t i = 0; i < sizeof settings.keybindings / sizeof settings.keybindings[0]; ++i) {
                                    const char *action = settings.keybindings[i].action;
                                    cui_key_combo *combo = &settings.keybindings[i].combo;
                                    if (!contains_ignore_case(action, keybinding_filter) &&
                                        !contains_ignore_case(cui_shortcut_label(ui, *combo), keybinding_filter)) continue;
                                    ++visible;
                                    cui_table_row(ui, false);
                                    cui_table_cell_text(ui, action);
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
                            cui_text(ui, APP_NAME);
                            cui_secondary(ui, "Version " APP_VERSION);
                            cui_secondary(ui, "A C99 desktop app built with Better-C-UI.");
                        }
                        if (changed || (settings_dirty && settings_section != 0)) {
                            settings_save_failed = !app_settings_save(&settings, user);
                            settings_dirty = false;
                        }
                        if (settings_save_failed) cui_secondary(ui, "Couldn’t save settings.");
                    }
                }
            }
        }
    }
    if (!settings_open) {
        settings_status = NULL;
        if (settings_dirty) {
            settings_save_failed = !app_settings_save(&settings, user);
            settings_dirty = false;
        }
    }
}

int main(void) {
    app_settings_load(&settings, NULL);
    int status = cui_app_run(&(cui_app_desc){
        .title = APP_NAME,
        .width = 900,
        .height = 600,
        .follow_system_theme = true,
    }, frame, NULL);
    if (settings_dirty) app_settings_save(&settings, NULL);
    return status;
}
