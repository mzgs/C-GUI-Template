#ifndef MACOS_MENU_H
#define MACOS_MENU_H

#include <stdbool.h>
#include "settings.h"

typedef struct {
    bool *settings_open;
    int *settings_section;
    int about_section;
    bool *palette_open;
    void *user;
    void (*on_action)(app_action action, void *user);
    void (*on_edit_key)(cui_key key, void *user);
    void (*on_toggle_fullscreen)(void *user);
    void (*on_will_terminate)(void *user);
} app_menu_hooks;

typedef struct {
    cui_key_combo save_combo;
    cui_key_combo find_combo;
    cui_key_combo palette_combo;
    bool fullscreen;
} app_menu_state;

/* Call from frame() after CUI creates the menu; referenced state must outlive the app. */
void app_menu_install(const app_menu_hooks *hooks);
/* Sync dynamic shortcut labels and fullscreen title on the native menu bar. */
void app_menu_sync(const app_menu_state *state);

#endif
