#ifndef MACOS_MENU_H
#define MACOS_MENU_H

#include <stdbool.h>

typedef struct {
    bool *settings_open;
    int *settings_section;
    int about_section;
    bool *palette_open;
    void (*on_will_terminate)(void);
} app_menu_hooks;

/* Call from frame() after CUI creates the menu; referenced state must outlive the app. */
void app_menu_install(const app_menu_hooks *hooks);

#endif
