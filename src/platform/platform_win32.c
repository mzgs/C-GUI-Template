#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <winhttp.h>

int app_system_dark(void) {
    HKEY key;
    DWORD value = 1, size = sizeof value;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) != ERROR_SUCCESS)
        return -1;
    LONG r = RegQueryValueExW(key, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS) return -1;
    return value == 0 ? 1 : 0;
}

bool app_window_get_pos(void *native_window, const char *backend, int *x, int *y) {
    if (!native_window || !backend || !x || !y || strcmp(backend, "win32") != 0) return false;
    RECT r;
    if (!GetWindowRect((HWND)native_window, &r)) return false;
    *x = (int)r.left;
    *y = (int)r.top;
    return true;
}

bool app_window_set_pos(void *native_window, const char *backend, int x, int y) {
    if (!native_window || !backend || strcmp(backend, "win32") != 0) return false;
    if (x < APP_WINDOW_POS_MIN || x > APP_WINDOW_POS_MAX ||
        y < APP_WINDOW_POS_MIN || y > APP_WINDOW_POS_MAX)
        return false;
    HWND hwnd = (HWND)native_window;
    RECT cur;
    if (!GetWindowRect(hwnd, &cur)) return false;
    LONG w = cur.right - cur.left;
    LONG h = cur.bottom - cur.top;
    LONG bar_h = h < 32 ? h : 32;
    RECT titlebar = { x, y, x + w, y + bar_h };
    HMONITOR mon = MonitorFromRect(&titlebar, MONITOR_DEFAULTTONULL);
    if (!mon) return false;
    MONITORINFO mi = { .cbSize = sizeof mi };
    if (!GetMonitorInfoW(mon, &mi)) return false;
    RECT overlap;
    if (!IntersectRect(&overlap, &titlebar, &mi.rcWork) ||
        (overlap.right - overlap.left) < 64 || (overlap.bottom - overlap.top) < 16)
        return false;
    return SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
}

void app_window_center(void *native_window, const char *backend) {
    if (!native_window || !backend || strcmp(backend, "win32") != 0) return;
    HWND hwnd = (HWND)native_window;
    RECT r;
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { .cbSize = sizeof mi };
    if (GetWindowRect(hwnd, &r) && mon && GetMonitorInfoW(mon, &mi)) {
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        int cx = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
        int cy = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
        SetWindowPos(hwnd, NULL, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void app_window_apply_metadata(void *native_window, const char *backend) {
    if (!native_window || !backend || strcmp(backend, "win32") != 0) return;
    HWND hwnd = (HWND)native_window;
    HINSTANCE inst = GetModuleHandleW(NULL);
    HANDLE small_icon = LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON),
                                   LR_DEFAULTCOLOR | LR_SHARED);
    if (small_icon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }
    HANDLE big_icon = LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON),
                                 LR_DEFAULTCOLOR | LR_SHARED);
    if (big_icon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big_icon);
    }
}

static void win32_set_err(char *err, size_t err_cap, const char *msg) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", msg ? msg : "Unknown error.");
}

bool app_http_resolve_redirect(const char *url, char *final_url, size_t capacity,
                               char *err, size_t err_cap) {
    if (!url || !final_url || capacity == 0) {
        win32_set_err(err, err_cap, "Invalid update check request.");
        return false;
    }
    final_url[0] = '\0';

    wchar_t wide_url[2048];
    if (MultiByteToWideChar(CP_UTF8, 0, url, -1, wide_url,
                            (int)(sizeof wide_url / sizeof wide_url[0])) <= 0) {
        win32_set_err(err, err_cap, "Invalid update URL.");
        return false;
    }

    wchar_t host[256] = { 0 };
    wchar_t path[1536] = { 0 };
    URL_COMPONENTSW parts;
    memset(&parts, 0, sizeof parts);
    parts.dwStructSize = sizeof parts;
    parts.lpszHostName = host;
    parts.dwHostNameLength = (DWORD)(sizeof host / sizeof host[0]);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = (DWORD)(sizeof path / sizeof path[0]);
    if (!WinHttpCrackUrl(wide_url, 0, 0, &parts) || !host[0]) {
        win32_set_err(err, err_cap, "Invalid update URL.");
        return false;
    }

    bool ok = false;
    HINTERNET session = NULL;
    HINTERNET connect = NULL;
    HINTERNET request = NULL;

    do {
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
        DWORD access_type = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#else
        DWORD access_type = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#endif
        session = WinHttpOpen(L"C-GUI-Template", access_type,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            win32_set_err(err, err_cap, "Could not initialize HTTP client.");
            break;
        }
        WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);

        connect = WinHttpConnect(session, host, parts.nPort, 0);
        if (!connect) {
            win32_set_err(err, err_cap, "Could not connect to GitHub.");
            break;
        }

        DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connect, L"HEAD", path[0] ? path : L"/",
                                     NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request) {
            win32_set_err(err, err_cap, "Could not create HTTP request.");
            break;
        }

        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, NULL)) {
            win32_set_err(err, err_cap, "Could not reach GitHub.");
            break;
        }

        DWORD status = 0;
        DWORD status_size = sizeof status;
        if (!WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                 WINHTTP_NO_HEADER_INDEX) ||
            status < 200 || status >= 400) {
            char buf[64];
            snprintf(buf, sizeof buf, "Server returned HTTP %lu.", (unsigned long)status);
            win32_set_err(err, err_cap, buf);
            break;
        }

        wchar_t resolved[2048];
        DWORD resolved_bytes = (DWORD)sizeof resolved;
        if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, resolved, &resolved_bytes)) {
            win32_set_err(err, err_cap, "Could not read resolved update URL.");
            break;
        }
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, resolved, -1,
                                           final_url, (int)capacity, NULL, NULL);
        if (utf8_len <= 0) {
            final_url[0] = '\0';
            win32_set_err(err, err_cap, "Resolved URL is too long.");
            break;
        }
        ok = true;
    } while (0);

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

bool app_updater_can_self_update(void) {
    return false;
}

bool app_updater_install_release(const char *repo, const char *tag,
                                 char *relaunch_path, size_t relaunch_cap,
                                 app_updater_abort_fn should_abort, void *abort_user,
                                 char *err, size_t err_cap) {
    (void)repo;
    (void)tag;
    (void)relaunch_path;
    (void)relaunch_cap;
    (void)should_abort;
    (void)abort_user;
    win32_set_err(err, err_cap, "In-place update is not supported on Windows.");
    return false;
}

bool app_updater_relaunch(const char *relaunch_path) {
    (void)relaunch_path;
    return false;
}
