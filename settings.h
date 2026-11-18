#ifndef SETTINGS_H
#define SETTINGS_H

#include <cui/cui.h>
#include <stddef.h>

enum {
    EXAMPLE_NUMBER_MIN = 0,
    EXAMPLE_NUMBER_MAX = 100,
    WINDOW_WIDTH_MIN = 360,
    WINDOW_WIDTH_MAX = 8192,
    WINDOW_HEIGHT_MIN = 280,
    WINDOW_HEIGHT_MAX = 8192,
    WINDOW_POS_MIN = -32768,
    WINDOW_POS_MAX = 32768,
    APP_CONFLICT_NONE = -1,
    APP_CONFLICT_SETTINGS = -2,
};

#define FONT_SCALE_MIN 0.85f
#define FONT_SCALE_MAX 1.35f

typedef enum {
    APP_THEME_SYSTEM = 0,
    APP_THEME_LIGHT,
    APP_THEME_DARK,
    APP_THEME_COUNT
} app_theme_mode;

typedef enum {
    APP_ACTION_FIND = 0,
    APP_ACTION_SAVE,
    APP_ACTION_COMMAND_PALETTE,
    APP_ACTION_COUNT
} app_action;

typedef struct {
    char example_string[128];
    double example_number;
    app_theme_mode theme_mode;
    cui_accent accent;
    float font_scale;
    bool reduced_motion;
    bool window_has_pos;
    int window_x;
    int window_y;
    int window_width;
    int window_height;
    bool window_fullscreen;
    struct {
        const char *action;
        const char *id;
        cui_key_combo combo;
    } keybindings[APP_ACTION_COUNT];
} app_settings;

/* A NULL path uses the app's per-user settings.json. */
bool app_settings_load(app_settings *settings, const char *path);
bool app_settings_save(const app_settings *settings, const char *path);
bool app_settings_dir(char *out, size_t capacity, const char *path, bool create);

/* Platform system theme: 1 dark, 0 light, -1 unknown. */
int app_system_dark(void);

/* Native window position helpers (backend is cui_app_backend(NULL)). */
bool app_window_get_pos(void *native_window, const char *backend, int *x, int *y);
bool app_window_set_pos(void *native_window, const char *backend, int x, int y);
void app_window_center(void *native_window, const char *backend);

/* Keybinding dispatch and conflict detection. */
bool app_action_pressed(cui_ctx *ui, const app_settings *settings, app_action action);
/* Returns another conflicting app_action index, APP_CONFLICT_SETTINGS if it
 * matches the reserved Settings shortcut, or APP_CONFLICT_NONE. */
int app_keybinding_conflict(const app_settings *settings, app_action action);

#endif
