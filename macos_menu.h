#ifndef MACOS_MENU_H
#define MACOS_MENU_H

#include <stdbool.h>

/* Call from frame() after CUI creates the menu; settings_open must outlive the app. */
void app_menu_install(bool *settings_open);

#endif
