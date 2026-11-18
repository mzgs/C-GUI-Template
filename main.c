#include <cui/cui_app.h>
#include <cui/cui.h>
#include <math.h>
#include <ctype.h>

#ifdef __APPLE__
#include "macos_menu.h"
#endif

static bool settings_open;
static int settings_section;
static char example_string[128] = "Example";
static double example_number = 42;
static char keybinding_filter[128];
static struct {
    const char *action;
    cui_key_combo combo;
} keybindings[] = {
    { "Find", { CUI_MOD_PRIMARY, CUI_KEY_F } },
    { "Save", { CUI_MOD_PRIMARY, CUI_KEY_S } },
};

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
    (void)user;
#ifdef __APPLE__
    app_menu_install(&settings_open);
#endif
    cui_vec2 size = cui_viewport_size(ui);
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
                                             .group = "settings-sections")) settings_section = i;
                        }
                    }
                }
                cui_separator(ui);
                cui_scroll(ui, .id = "settings-page", .width = CUI_FILL) {
                    cui_column(ui, .height = CUI_FILL, .gap = 12) {
                        cui_heading(ui, sections[settings_section], 4);
                        if (settings_section == 0) {
                            cui_row(ui, .gap = 16) {
                                cui_text(ui, "Example string", .font = CUI_FONT_MEDIUM, .width = CUI_PX(168));
                                cui_input(ui, "example-string", example_string, sizeof example_string,
                                          .width = CUI_FILL);
                            }
                            cui_row(ui, .gap = 16) {
                                cui_text(ui, "Example number (0–100)", .font = CUI_FONT_MEDIUM, .width = CUI_PX(168));
                                cui_number(ui, "example-number", &example_number, 0, 100, .width = CUI_FILL);
                            }
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
                                for (size_t i = 0; i < sizeof keybindings / sizeof keybindings[0]; ++i) {
                                    if (!contains_ignore_case(keybindings[i].action, keybinding_filter) &&
                                        !contains_ignore_case(cui_shortcut_label(ui, keybindings[i].combo),
                                                              keybinding_filter)) continue;
                                    ++visible;
                                    cui_table_row(ui, false);
                                    cui_table_cell_text(ui, keybindings[i].action);
                                    cui_table_cell(ui);
                                    cui_theme *theme = cui_theme_get(ui);
                                    float control_height = theme->control_md;
                                    theme->control_md = theme->control_sm;
                                    cui_shortcut_recorder(ui, keybindings[i].action,
                                                          &keybindings[i].combo, .width = CUI_FILL);
                                    theme->control_md = control_height;
                                }
                            }
                            if (!visible) cui_secondary(ui, "No shortcuts match your filter.");
                        } else {
                            cui_text(ui, "C GUI Template");
                            cui_secondary(ui, "Version " APP_VERSION);
                            cui_secondary(ui, "A C99 desktop app built with Better-C-UI.");
                        }
                    }
                }
            }
        }
    }
}

int main(void) {
    return cui_app_run(&(cui_app_desc){
        .title = "C GUI Template",
        .width = 900,
        .height = 600,
        .follow_system_theme = true,
    }, frame, NULL);
}
