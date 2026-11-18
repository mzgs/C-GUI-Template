#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <cui/cui.h>
#include <stdbool.h>
#include <stddef.h>

enum {
    EXAMPLE_NUMBER_MIN = 0,
    EXAMPLE_NUMBER_MAX = 100,
    WINDOW_WIDTH_MIN = 360,
    WINDOW_WIDTH_MAX = 8192,
    WINDOW_HEIGHT_MIN = 280,
    WINDOW_HEIGHT_MAX = 8192,
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
    /* Template example fields — replace with your own app settings. */
    char example_string[128];
    double example_number;

    /* Persisted appearance preferences. */
    struct {
        app_theme_mode theme_mode;
        cui_accent accent;
        float font_scale;
        bool reduced_motion;
    } appearance;

    /* Persisted window geometry and fullscreen state. */
    struct {
        bool has_pos;
        int x;
        int y;
        int width;
        int height;
        bool fullscreen;
    } window;

    /* Configurable action keybindings. */
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

/* Keybinding dispatch and conflict detection. */
bool app_action_pressed(cui_ctx *ui, const app_settings *settings, app_action action);
/* Returns another conflicting app_action index, APP_CONFLICT_SETTINGS if it
 * matches the reserved Settings shortcut, or APP_CONFLICT_NONE. */
int app_keybinding_conflict(const app_settings *settings, app_action action);

#endif
