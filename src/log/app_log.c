#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "app_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <wchar.h>
#else
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

enum {
    APP_LOG_DEFAULT_MAX_BYTES = 1024 * 1024, /* 1 MiB */
    APP_LOG_PATH_CAP = 4096,
};

#ifdef _WIN32
static INIT_ONCE g_lock_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_lock;

static BOOL CALLBACK init_lock_cb(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&g_lock);
    return TRUE;
}

static void log_lock(void) {
    InitOnceExecuteOnce(&g_lock_once, init_lock_cb, NULL, NULL);
    EnterCriticalSection(&g_lock);
}

static void log_unlock(void) {
    LeaveCriticalSection(&g_lock);
}
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_lock(void) {
    pthread_mutex_lock(&g_lock);
}

static void log_unlock(void) {
    pthread_mutex_unlock(&g_lock);
}
#endif

static FILE *g_file = NULL;
static char g_path[APP_LOG_PATH_CAP] = "";
static size_t g_max_bytes = APP_LOG_DEFAULT_MAX_BYTES;
static size_t g_bytes_written = 0;
static app_log_level g_level = APP_LOG_INFO;
static bool g_mirror_all_stderr = false;

static const char *level_tag(app_log_level level) {
    switch (level) {
    case APP_LOG_DEBUG: return "DEBUG";
    case APP_LOG_INFO:  return "INFO";
    case APP_LOG_WARN:  return "WARN";
    case APP_LOG_ERROR: return "ERROR";
    default:            return "INFO";
    }
}

#ifdef _WIN32
static bool utf8_to_wide(const char *src, wchar_t *dst, size_t cap) {
    if (!src || !src[0] || !dst || cap == 0) return false;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, (int)cap) > 0;
}

static FILE *open_log_file(const char *path, size_t *out_size) {
    wchar_t wpath[APP_LOG_PATH_CAP];
    if (!utf8_to_wide(path, wpath, APP_LOG_PATH_CAP)) return NULL;
    FILE *f = _wfopen(wpath, L"ab");
    if (!f) return NULL;
    if (out_size) {
        *out_size = 0;
        if (_fseeki64(f, 0, SEEK_END) == 0) {
            __int64 pos = _ftelli64(f);
            if (pos > 0) *out_size = (size_t)pos;
        }
    }
    return f;
}

static bool rotate_log_file_locked(void) {
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
    char backup[APP_LOG_PATH_CAP];
    int n = snprintf(backup, sizeof backup, "%s.1", g_path);
    if (n > 0 && (size_t)n < sizeof backup) {
        wchar_t wsrc[APP_LOG_PATH_CAP];
        wchar_t wdst[APP_LOG_PATH_CAP];
        if (utf8_to_wide(g_path, wsrc, APP_LOG_PATH_CAP) &&
            utf8_to_wide(backup, wdst, APP_LOG_PATH_CAP)) {
            MoveFileExW(wsrc, wdst, MOVEFILE_REPLACE_EXISTING);
        }
    }
    g_bytes_written = 0;
    g_file = open_log_file(g_path, &g_bytes_written);
    return g_file != NULL;
}
#else
static FILE *open_log_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "ab");
    if (!f) return NULL;
    if (out_size) {
        *out_size = 0;
        struct stat st;
        if (fstat(fileno(f), &st) == 0 && st.st_size > 0) {
            *out_size = (size_t)st.st_size;
        }
    }
    return f;
}

static bool rotate_log_file_locked(void) {
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
    char backup[APP_LOG_PATH_CAP];
    int n = snprintf(backup, sizeof backup, "%s.1", g_path);
    if (n > 0 && (size_t)n < sizeof backup) {
        (void)rename(g_path, backup);
    }
    g_bytes_written = 0;
    g_file = open_log_file(g_path, &g_bytes_written);
    return g_file != NULL;
}
#endif

bool app_log_init(const char *path, size_t max_bytes) {
    if (!path || !path[0]) return false;
    size_t len = strlen(path);
    if (len + 3 >= sizeof g_path) return false;

    log_lock();
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
    memcpy(g_path, path, len + 1);
    g_max_bytes = max_bytes > 0 ? max_bytes : APP_LOG_DEFAULT_MAX_BYTES;
    g_bytes_written = 0;

    g_file = open_log_file(g_path, &g_bytes_written);
    if (g_file && g_bytes_written >= g_max_bytes) {
        (void)rotate_log_file_locked();
    }
    bool ok = (g_file != NULL);
    if (!ok) {
        g_path[0] = '\0';
    }
    log_unlock();
    return ok;
}

void app_log_shutdown(void) {
    log_lock();
    if (g_file) {
        fflush(g_file);
        fclose(g_file);
        g_file = NULL;
    }
    g_path[0] = '\0';
    g_bytes_written = 0;
    log_unlock();
}

void app_log_set_level(app_log_level level) {
    log_lock();
    g_level = level;
    log_unlock();
}

app_log_level app_log_get_level(void) {
    log_lock();
    app_log_level level = g_level;
    log_unlock();
    return level;
}

void app_log_set_stderr(bool enabled) {
    log_lock();
    g_mirror_all_stderr = enabled;
    log_unlock();
}

const char *app_log_path(void) {
    return g_path;
}

static void format_timestamp(char *buf, size_t cap) {
    time_t now = time(NULL);
    struct tm tm_buf;
#ifdef _WIN32
    if (localtime_s(&tm_buf, &now) != 0) {
        snprintf(buf, cap, "0000-00-00 00:00:00");
        return;
    }
#else
    if (!localtime_r(&now, &tm_buf)) {
        snprintf(buf, cap, "0000-00-00 00:00:00");
        return;
    }
#endif
    if (strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tm_buf) == 0) {
        snprintf(buf, cap, "0000-00-00 00:00:00");
    }
}

void app_log_write(app_log_level level, const char *fmt, ...) {
    if (!fmt) return;

    char msg_buf[2048];
    va_list args;
    va_start(args, fmt);
    int msg_len = vsnprintf(msg_buf, sizeof msg_buf, fmt, args);
    va_end(args);
    if (msg_len < 0) return;

    size_t actual_len = (size_t)msg_len < sizeof msg_buf ? (size_t)msg_len : sizeof msg_buf - 1;
    while (actual_len > 0 && (msg_buf[actual_len - 1] == '\n' || msg_buf[actual_len - 1] == '\r')) {
        msg_buf[--actual_len] = '\0';
    }

    char ts[32];
    format_timestamp(ts, sizeof ts);

    char line[2128];
    int line_len = snprintf(line, sizeof line, "%s [%s] %s\n", ts, level_tag(level), msg_buf);
    if (line_len <= 0) return;
    size_t write_len = (size_t)line_len < sizeof line ? (size_t)line_len : sizeof line - 1;

    log_lock();
    if (level < g_level) {
        log_unlock();
        return;
    }
    bool to_stderr = g_mirror_all_stderr || level >= APP_LOG_WARN;
    if (to_stderr) {
        fwrite(line, 1, write_len, stderr);
        fflush(stderr);
    }
    if (g_file) {
        if (g_bytes_written > 0 && g_bytes_written + write_len > g_max_bytes) {
            (void)rotate_log_file_locked();
        }
        if (g_file) {
            size_t n = fwrite(line, 1, write_len, g_file);
            g_bytes_written += n;
            fflush(g_file);
        }
    }
    log_unlock();
}
