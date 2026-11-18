#ifndef APP_CHROME_H
#define APP_CHROME_H

#include <cui/cui_app.h>
#include <cui/cui.h>
#include <stdbool.h>
#include <stdio.h>
#include "settings.h"

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

typedef void (*app_action_handler)(cui_ctx *ui, app_action action, void *user);

typedef struct {
    app_settings settings;
    app_cli_opts cli;
    const char *settings_path;
    bool settings_open;
    bool prev_settings_open;
    bool palette_open;
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
    const char *status;
    cui_tone status_tone;
    int settings_section;
    char keybinding_filter[128];
    int pending_action;
    cui_key pending_edit_key;
    cui_key injected_edit_key;
    app_action_handler on_action;
    void *on_action_user;
    /* Injectable platform hooks (default to CUI / OS implementations). */
    bool (*file_dialog)(const cui_file_dialog_opts *opts, char *out, size_t cap);
    bool (*open_url)(const char *url);
    int (*system_dark)(void);
} app_chrome;

app_settings app_chrome_default_settings(void);
app_cli_action app_chrome_parse_cli(int argc, char **argv, app_cli_opts *opts, FILE *out, FILE *err);
bool app_contains_ignore_case(const char *text, const char *query);

/* Reset chrome to defaults without parsing CLI or reading disk. */
void app_chrome_reset(app_chrome *chrome, const char *settings_path);
/* Parse CLI, load settings, and initialize chrome. Returns false when the app should exit with *exit_code. */
bool app_chrome_init(app_chrome *chrome, int argc, char **argv, int *exit_code);
cui_app_desc app_chrome_app_desc(const app_chrome *chrome);
void app_chrome_set_action_handler(app_chrome *chrome, app_action_handler handler, void *user);

/* Call at the start and end of frame() around your application UI. */
void app_chrome_begin_frame(app_chrome *chrome, cui_ctx *ui);
void app_chrome_end_frame(app_chrome *chrome, cui_ctx *ui);
void app_chrome_flush(app_chrome *chrome);
void app_chrome_shutdown(app_chrome *chrome);

void app_chrome_notify(app_chrome *chrome, cui_ctx *ui, const char *message, cui_tone tone);
const app_settings *app_chrome_settings(const app_chrome *chrome);

#endif
