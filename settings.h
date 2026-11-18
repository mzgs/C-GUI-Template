#ifndef SETTINGS_H
#define SETTINGS_H

#include <cui/cui.h>

enum { EXAMPLE_NUMBER_MIN = 0, EXAMPLE_NUMBER_MAX = 100 };

typedef struct {
    char example_string[128];
    double example_number;
    struct {
        const char *name;
        const char *action;
        cui_key_combo combo;
    } keybindings[2];
} app_settings;

/* A NULL path uses the app's settings.json in Application Support. */
void app_settings_load(app_settings *settings, const char *path);
bool app_settings_save(const app_settings *settings, const char *path);

#endif
