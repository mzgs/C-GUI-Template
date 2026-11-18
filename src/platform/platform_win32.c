#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "platform.h"
#include <string.h>
#include <windows.h>

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
