#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "updater.h"
#include "app_log.h"
#include "platform.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
typedef CRITICAL_SECTION updater_mutex;
static void mutex_init(updater_mutex *m) { InitializeCriticalSection(m); }
static void mutex_destroy(updater_mutex *m) { DeleteCriticalSection(m); }
static void mutex_lock(updater_mutex *m) { EnterCriticalSection(m); }
static void mutex_unlock(updater_mutex *m) { LeaveCriticalSection(m); }
#else
#include <pthread.h>
typedef pthread_mutex_t updater_mutex;
static void mutex_init(updater_mutex *m) { pthread_mutex_init(m, NULL); }
static void mutex_destroy(updater_mutex *m) { pthread_mutex_destroy(m); }
static void mutex_lock(updater_mutex *m) { pthread_mutex_lock(m); }
static void mutex_unlock(updater_mutex *m) { pthread_mutex_unlock(m); }
#endif

#ifndef APP_GITHUB_REPO
#define APP_GITHUB_REPO ""
#endif

struct app_updater {
    updater_mutex mu;
    int refcount;
    bool destroyed;
    char repo[128];
    char current_version[64];
    app_update_snapshot snap;
};

static bool is_slug_char(char c) {
    return isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-';
}

static bool is_tag_char(char c) {
    return is_slug_char(c) || c == '+';
}

bool app_updater_valid_repo(const char *repo) {
    if (!repo || !repo[0]) return false;
    const char *slash = strchr(repo, '/');
    if (!slash || slash == repo || !slash[1]) return false;
    if (strchr(slash + 1, '/')) return false;
    if (strstr(repo, "..")) return false;
    for (const char *p = repo; *p; ++p) {
        if (*p != '/' && !is_slug_char(*p)) return false;
    }
    size_t len = strlen(repo);
    return len < 128;
}

bool app_updater_extract_release_tag(const char *url, char *tag, size_t cap) {
    if (!url || !tag || cap == 0) return false;
    tag[0] = '\0';
    static const char marker[] = "/releases/tag/";
    const char *start = strstr(url, marker);
    if (!start) return false;
    start += sizeof(marker) - 1;

    const char *end = start;
    while (*end && *end != '/' && *end != '?' && *end != '#') ++end;
    size_t len = (size_t)(end - start);
    if (len == 0 || len >= cap) return false;

    for (size_t i = 0; i < len; ++i) {
        if (!is_tag_char(start[i])) return false;
    }
    memcpy(tag, start, len);
    tag[len] = '\0';
    if (strcmp(tag, ".") == 0 || strcmp(tag, "..") == 0 || strstr(tag, "..")) {
        tag[0] = '\0';
        return false;
    }
    return true;
}

typedef struct {
    unsigned long parts[4];
    bool prerelease;
} parsed_version;

static bool parse_version(const char *str, parsed_version *out) {
    if (!str || !out) return false;
    memset(out, 0, sizeof *out);
    if (*str == 'v' || *str == 'V') ++str;
    if (!isdigit((unsigned char)*str)) return false;

    int count = 0;
    const char *p = str;
    while (count < 4) {
        if (!isdigit((unsigned char)*p)) return false;
        unsigned long val = 0;
        while (isdigit((unsigned char)*p)) {
            unsigned long digit = (unsigned long)(*p - '0');
            if (val > (ULONG_MAX - digit) / 10UL) return false;
            val = val * 10UL + digit;
            ++p;
        }
        out->parts[count++] = val;
        if (*p == '.') {
            ++p;
            if (count == 4) return false;
            continue;
        }
        break;
    }
    if (count < 2) return false;
    if (*p == '-') {
        if (!p[1]) return false;
        out->prerelease = true;
        return true;
    }
    if (*p == '+') {
        return p[1] != '\0';
    }
    return *p == '\0';
}

int app_updater_compare_versions(const char *candidate, const char *current) {
    parsed_version a, b;
    if (!parse_version(candidate, &a) || !parse_version(current, &b)) return -2;
    for (int i = 0; i < 4; ++i) {
        if (a.parts[i] > b.parts[i]) return 1;
        if (a.parts[i] < b.parts[i]) return -1;
    }
    if (a.prerelease != b.prerelease) return a.prerelease ? -1 : 1;
    return 0;
}

bool app_updater_configured(void) {
    return app_updater_valid_repo(APP_GITHUB_REPO);
}

bool app_updater_check_due(int64_t last_checked, int64_t now) {
    if (last_checked <= 0 || now <= 0) return true;
    if (now < last_checked) return true;
    return (now - last_checked) >= APP_UPDATE_CHECK_INTERVAL_SEC;
}

static bool url_is_empty_releases_page(const char *url) {
    if (!url) return false;
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') --len;
    static const char suffix[] = "/releases";
    size_t slen = sizeof(suffix) - 1;
    return len >= slen && memcmp(url + len - slen, suffix, slen) == 0;
}

static void updater_release(app_updater *u) {
    bool should_free = false;
    mutex_lock(&u->mu);
    if (--u->refcount <= 0) should_free = true;
    mutex_unlock(&u->mu);
    if (should_free) {
        mutex_destroy(&u->mu);
        free(u);
    }
}

app_updater *app_updater_create(const char *repo, const char *current_version) {
    if (!app_updater_valid_repo(repo) || !current_version || !current_version[0]) return NULL;
    app_updater *u = calloc(1, sizeof *u);
    if (!u) return NULL;
    mutex_init(&u->mu);
    u->refcount = 1;
    snprintf(u->repo, sizeof u->repo, "%s", repo);
    snprintf(u->current_version, sizeof u->current_version, "%s", current_version);
    return u;
}

void app_updater_destroy(app_updater *updater) {
    if (!updater) return;
    mutex_lock(&updater->mu);
    updater->destroyed = true;
    mutex_unlock(&updater->mu);
    updater_release(updater);
}

static void run_check_worker(app_updater *u) {
    char repo[128];
    char current_ver[64];
    mutex_lock(&u->mu);
    snprintf(repo, sizeof repo, "%s", u->repo);
    snprintf(current_ver, sizeof current_ver, "%s", u->current_version);
    mutex_unlock(&u->mu);

    char latest_url[256];
    snprintf(latest_url, sizeof latest_url, "https://github.com/%s/releases/latest", repo);

    char resolved_url[512] = "";
    char err[256] = "";
    bool fetched = app_http_resolve_redirect(latest_url, resolved_url, sizeof resolved_url,
                                             err, sizeof err);

    app_update_snapshot next;
    memset(&next, 0, sizeof next);
    if (!fetched) {
        next.state = APP_UPDATE_ERROR;
        snprintf(next.error, sizeof next.error, "%s",
                 err[0] ? err : "Could not check for updates.");
    } else {
        char tag[64] = "";
        if (!app_updater_extract_release_tag(resolved_url, tag, sizeof tag)) {
            if (url_is_empty_releases_page(resolved_url)) {
                next.state = APP_UPDATE_UP_TO_DATE;
                snprintf(next.release_url, sizeof next.release_url, "%s", resolved_url);
            } else {
                next.state = APP_UPDATE_ERROR;
                snprintf(next.error, sizeof next.error, "Could not determine latest release version.");
            }
        } else {
            const char *ver = (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag;
            int cmp = app_updater_compare_versions(ver, current_ver);
            snprintf(next.latest_tag, sizeof next.latest_tag, "%s", tag);
            snprintf(next.latest_version, sizeof next.latest_version, "%s", ver);
            snprintf(next.release_url, sizeof next.release_url, "%s", resolved_url);
            if (cmp > 0) {
                next.state = APP_UPDATE_AVAILABLE;
                next.has_update = true;
            } else if (cmp == 0 || cmp == -1) {
                next.state = APP_UPDATE_UP_TO_DATE;
            } else {
                next.state = APP_UPDATE_ERROR;
                snprintf(next.error, sizeof next.error, "Unrecognized release version (%s).", tag);
            }
        }
    }

    if (next.state == APP_UPDATE_AVAILABLE) {
        app_log_info("Update available: %s (current %s)", next.latest_tag, current_ver);
    } else if (next.state == APP_UPDATE_ERROR) {
        app_log_warn("Update check failed: %s", next.error[0] ? next.error : "unknown error");
    }

    mutex_lock(&u->mu);
    if (!u->destroyed) {
        u->snap = next;
    }
    mutex_unlock(&u->mu);
    updater_release(u);
}

static bool install_should_abort(void *user) {
    app_updater *u = user;
    mutex_lock(&u->mu);
    bool aborted = u->destroyed;
    mutex_unlock(&u->mu);
    return aborted;
}

static void run_install_worker(app_updater *u) {
    char repo[128];
    char tag[64];
    mutex_lock(&u->mu);
    snprintf(repo, sizeof repo, "%s", u->repo);
    snprintf(tag, sizeof tag, "%s", u->snap.latest_tag);
    mutex_unlock(&u->mu);

    char relaunch_path[4096] = "";
    char err[256] = "";
    bool ok = app_updater_install_release(repo, tag, relaunch_path, sizeof relaunch_path,
                                          install_should_abort, u, err, sizeof err);
    if (ok) {
        app_log_info("Installed update %s (%s)", tag, relaunch_path);
    } else {
        app_log_error("Update install failed (%s): %s", tag, err[0] ? err : "unknown error");
    }

    mutex_lock(&u->mu);
    if (!u->destroyed) {
        if (ok) {
            u->snap.state = APP_UPDATE_READY_TO_RELAUNCH;
            u->snap.error[0] = '\0';
            snprintf(u->snap.relaunch_path, sizeof u->snap.relaunch_path, "%s", relaunch_path);
        } else {
            u->snap.state = APP_UPDATE_ERROR;
            snprintf(u->snap.error, sizeof u->snap.error, "%s",
                     err[0] ? err : "Could not install update.");
        }
    }
    mutex_unlock(&u->mu);
    updater_release(u);
}

#ifdef _WIN32
static DWORD WINAPI check_thread_entry(LPVOID param) {
    run_check_worker((app_updater *)param);
    return 0;
}

static DWORD WINAPI install_thread_entry(LPVOID param) {
    run_install_worker((app_updater *)param);
    return 0;
}

static bool spawn_detached(LPTHREAD_START_ROUTINE fn, app_updater *u) {
    HANDLE h = CreateThread(NULL, 0, fn, u, 0, NULL);
    if (!h) return false;
    CloseHandle(h);
    return true;
}
#else
static void *check_thread_entry(void *param) {
    run_check_worker((app_updater *)param);
    return NULL;
}

static void *install_thread_entry(void *param) {
    run_install_worker((app_updater *)param);
    return NULL;
}

static bool spawn_detached(void *(*fn)(void *), app_updater *u) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, fn, u) != 0) return false;
    pthread_detach(tid);
    return true;
}
#endif

bool app_updater_check(app_updater *updater) {
    if (!updater) return false;
    mutex_lock(&updater->mu);
    if (updater->destroyed ||
        updater->snap.state == APP_UPDATE_CHECKING ||
        updater->snap.state == APP_UPDATE_UPDATING ||
        updater->snap.state == APP_UPDATE_READY_TO_RELAUNCH) {
        mutex_unlock(&updater->mu);
        return false;
    }
    memset(&updater->snap, 0, sizeof updater->snap);
    updater->snap.state = APP_UPDATE_CHECKING;
    updater->refcount++;
    mutex_unlock(&updater->mu);

    if (!spawn_detached(check_thread_entry, updater)) {
        mutex_lock(&updater->mu);
        updater->snap.state = APP_UPDATE_ERROR;
        snprintf(updater->snap.error, sizeof updater->snap.error, "Could not start update check.");
        mutex_unlock(&updater->mu);
        updater_release(updater);
        return false;
    }
    return true;
}

bool app_updater_start_install(app_updater *updater) {
    if (!updater) return false;
    mutex_lock(&updater->mu);
    if (updater->destroyed || !updater->snap.has_update || !updater->snap.latest_tag[0] ||
        updater->snap.state == APP_UPDATE_CHECKING ||
        updater->snap.state == APP_UPDATE_UPDATING ||
        updater->snap.state == APP_UPDATE_READY_TO_RELAUNCH) {
        mutex_unlock(&updater->mu);
        return false;
    }
    updater->snap.state = APP_UPDATE_UPDATING;
    updater->snap.error[0] = '\0';
    updater->refcount++;
    mutex_unlock(&updater->mu);

    if (!spawn_detached(install_thread_entry, updater)) {
        mutex_lock(&updater->mu);
        updater->snap.state = APP_UPDATE_ERROR;
        snprintf(updater->snap.error, sizeof updater->snap.error, "Could not start update download.");
        mutex_unlock(&updater->mu);
        updater_release(updater);
        return false;
    }
    return true;
}

void app_updater_snapshot(app_updater *updater, app_update_snapshot *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!updater) return;
    mutex_lock(&updater->mu);
    *out = updater->snap;
    mutex_unlock(&updater->mu);
}

bool app_updater_is_busy(app_updater *updater) {
    if (!updater) return false;
    mutex_lock(&updater->mu);
    bool busy = updater->snap.state == APP_UPDATE_CHECKING ||
                updater->snap.state == APP_UPDATE_UPDATING;
    mutex_unlock(&updater->mu);
    return busy;
}
