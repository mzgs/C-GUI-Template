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

/* Human-readable OS and architecture summary (e.g. "macOS 15.3.1 (arm64)"). */
bool app_os_info(char *out, size_t capacity);

/* Native window helpers (backend is cui_app_backend(NULL)). */
bool app_window_get_pos(void *native_window, const char *backend, int *x, int *y);
bool app_window_set_pos(void *native_window, const char *backend, int x, int y);
void app_window_center(void *native_window, const char *backend);
void app_window_apply_metadata(void *native_window, const char *backend);
void app_window_set_dark(void *native_window, const char *backend, bool dark);

#ifdef __APPLE__
/* Resolve the default per-user macOS settings.json path. */
bool macos_settings_path(char *path, size_t capacity);
#endif

/* Callback polled by the updater before swapping installed files on disk. */
typedef bool (*app_updater_abort_fn)(void *user);

/* Resolve HTTP(S) redirects via HEAD and write the final URL. */
bool app_http_resolve_redirect(const char *url, char *final_url, size_t capacity,
                               char *err, size_t err_cap);

/* True when the running installation can be updated in place. */
bool app_updater_can_self_update(void);

/* Download, extract, and atomically replace the running app on disk. */
bool app_updater_install_release(const char *repo, const char *tag,
                                 char *relaunch_path, size_t relaunch_cap,
                                 app_updater_abort_fn should_abort, void *abort_user,
                                 char *err, size_t err_cap);

/* Spawn a detached helper that waits for this process to exit and launches path. */
bool app_updater_relaunch(const char *relaunch_path);

#endif
