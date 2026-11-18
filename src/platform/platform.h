#ifndef APP_PLATFORM_H
#define APP_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

enum {
    APP_WINDOW_POS_MIN = -32768,
    APP_WINDOW_POS_MAX = 32768,
};

/* Platform system theme: 1 dark, 0 light, -1 unknown. */
int app_system_dark(void);

/* Native window position helpers (backend is cui_app_backend(NULL)). */
bool app_window_get_pos(void *native_window, const char *backend, int *x, int *y);
bool app_window_set_pos(void *native_window, const char *backend, int x, int y);
void app_window_center(void *native_window, const char *backend);

#ifdef __APPLE__
/* Resolve the default per-user macOS settings.json path. */
bool macos_settings_path(char *path, size_t capacity);
#endif

#endif
