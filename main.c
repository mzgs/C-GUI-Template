#include <cui/cui_app.h>
#include <cui/cui.h>
#include <math.h>

#ifdef __APPLE__
#include "macos_menu.h"
#endif

static bool settings_open;
static int settings_section;

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
        cui_column(ui, .height = CUI_PX(fminf(size.y * 0.9f, size.y - 48) - 94), .gap = 16) {
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
                cui_scroll(ui, .id = "settings-page", .width = CUI_FILL, .gap = 12) {
                    cui_heading(ui, sections[settings_section], 4);
                    if (settings_section == 0)
                        cui_secondary(ui, "No general settings available yet.");
                    else if (settings_section == 1)
                        cui_secondary(ui, "No custom keybindings configured.");
                    else {
                        cui_text(ui, "C GUI Template");
                        cui_secondary(ui, "Version " APP_VERSION);
                        cui_secondary(ui, "A C99 desktop app built with Better-C-UI.");
                    }
                }
            }
            cui_dialog_footer(ui) {
                if (cui_button(ui, "Done", .variant = CUI_BUTTON_PRIMARY))
                    settings_open = false;
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
