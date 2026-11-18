#include <cui/cui_app.h>
#include <cui/cui.h>
#include <math.h>

#ifdef __APPLE__
#include "macos_menu.h"
#endif

static bool settings_open;

static void frame(cui_ctx *ui, void *user) {
    (void)user;
#ifdef __APPLE__
    app_menu_install(&settings_open);
#endif
    cui_vec2 size = cui_viewport_size(ui);
    cui_dialog(ui, "Settings", &settings_open, .width = size.x * 0.9f) {
        /* shortcut: pinned CUI adds 94pt for the header, padding and gap; revisit if its layout changes. */
        cui_column(ui, .height = CUI_PX(fminf(size.y * 0.9f, size.y - 48) - 94), .gap = 16) {
            cui_secondary(ui, "No settings available yet.");
            cui_spacer(ui);
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
