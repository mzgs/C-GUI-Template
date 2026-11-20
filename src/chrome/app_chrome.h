#ifndef APP_CHROME_H
#define APP_CHROME_H

#include <cui/cui_app.h>
#include <cui/cui.h>
#include <stdbool.h>
#include "app_log.h"
#include "settings.h"

enum {
    SETTINGS_SECTION_GENERAL = 0,
    SETTINGS_SECTION_APPEARANCE,
    SETTINGS_SECTION_KEYBINDINGS,
    SETTINGS_SECTION_ABOUT,
    SETTINGS_SECTION_COUNT
};

typedef struct {
    const char *settings_path;
    bool software_renderer;
} app_cli_opts;

typedef void (*app_action_handler)(cui_ctx *ui, app_action action, void *user);

struct app_updater;

typedef struct {
    /* Settings & CLI options */
    app_settings settings;
    app_cli_opts cli;

    /* Chrome UI state */
    bool settings_open;
    bool palette_open;
    int settings_section;
    char keybinding_filter[128];

    /* Update checker state */
    struct app_updater *updater;
    bool update_auto_checked;
    bool update_relaunching;
    int last_update_state;

    /* Internal lifecycle & persistence tracking */
    bool settings_save_failed;
    bool settings_dirty;
    bool window_dirty;
    double window_dirty_since;
    bool window_fullscreen_pending;
    double window_fullscreen_since;
    bool observed_window_has_pos;
    int observed_window_x;
    int observed_window_y;
    cui_ctx *window_state_ui;
    cui_ctx *appearance_ui;
    bool appearance_force_motion;
    bool env_theme_override;
    bool env_accent_override;
#ifdef __APPLE__
    int pending_action;
    cui_key pending_edit_key;
    cui_key injected_edit_key;
#endif

    /* Action callback */
    app_action_handler on_action;
    void *on_action_user;
} app_chrome;

/* Parse CLI, load settings, and initialize chrome. Returns false when the app should exit with *exit_code. */
bool app_chrome_init(app_chrome *chrome, int argc, char **argv, int *exit_code);
cui_app_desc app_chrome_app_desc(const app_chrome *chrome);
void app_chrome_set_action_handler(app_chrome *chrome, app_action_handler handler, void *user);

/* Call at the start and end of frame() around your application UI. */
void app_chrome_begin_frame(app_chrome *chrome, cui_ctx *ui);
void app_chrome_end_frame(app_chrome *chrome, cui_ctx *ui);
void app_chrome_shutdown(app_chrome *chrome);

void app_chrome_notify(app_chrome *chrome, cui_ctx *ui, const char *message, cui_tone tone);
const app_settings *app_chrome_settings(const app_chrome *chrome);

#endif
