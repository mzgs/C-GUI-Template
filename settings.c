#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "settings.h"
#include <yyjson.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>
typedef wchar_t path_char;
/* shortcut: GetTempFileNameW uses MAX_PATH, switch to long-path-aware file creation if needed. */
enum { SETTINGS_PATH_CAP = MAX_PATH };
#else
#include <sys/stat.h>
#include <unistd.h>
typedef char path_char;
enum { SETTINGS_PATH_CAP = PATH_MAX };
#if !defined(__APPLE__) && defined(__has_include)
#if __has_include(<X11/Xlib.h>) && __has_include(<X11/Xatom.h>)
#define APP_HAVE_X11 1
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif
#endif
#endif

#ifdef __APPLE__
bool macos_settings_path(char *path, size_t capacity);
int macos_system_dark(void);
bool macos_window_get_pos(void *native_window, int *x, int *y);
bool macos_window_set_pos(void *native_window, int x, int y);
void macos_window_center(void *native_window);
#endif

static bool settings_path(path_char *path, const char *override) {
#ifdef _WIN32
    if (override) {
        if (!override[0]) return false;
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, override, -1, path, SETTINGS_PATH_CAP))
            return false;
        for (wchar_t *p = path; *p; ++p)
            if (*p == L'/') *p = L'\\';
        return true;
    }
    wchar_t name[SETTINGS_PATH_CAP];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, APP_NAME, -1, name, SETTINGS_PATH_CAP))
        return false;
    PWSTR base = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_CREATE, NULL, &base))) {
        CoTaskMemFree(base);
        return false;
    }
    int len = swprintf(path, SETTINGS_PATH_CAP, L"%ls\\%ls\\settings.json", base, name);
    CoTaskMemFree(base);
    return len >= 0 && len < SETTINGS_PATH_CAP;
#else
    if (override) {
        if (!override[0]) return false;
        int len = snprintf(path, SETTINGS_PATH_CAP, "%s", override);
        return len >= 0 && len < SETTINGS_PATH_CAP;
    }
#ifdef __APPLE__
    return macos_settings_path(path, SETTINGS_PATH_CAP);
#else
    const char *base = getenv("XDG_CONFIG_HOME");
    const char *suffix = "";
    if (!base || base[0] != '/') {
        base = getenv("HOME");
        suffix = "/.config";
    }
    if (!base || base[0] != '/') return false;
    int len = snprintf(path, SETTINGS_PATH_CAP, "%s%s/%s/settings.json", base, suffix, APP_NAME);
    return len >= 0 && len < SETTINGS_PATH_CAP;
#endif
#endif
}

#ifdef _WIN32
static bool create_directories(wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (CreateDirectoryW(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    if (GetLastError() != ERROR_PATH_NOT_FOUND) return false;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash || slash == path) return false;
    *slash = L'\0';
    bool parent = create_directories(path);
    *slash = L'\\';
    return parent && (CreateDirectoryW(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
}
#else
static bool create_directories(char *path) {
    if (mkdir(path, 0700) == 0 || errno == EEXIST) return true;
    if (errno != ENOENT) return false;
    char *slash = strrchr(path, '/');
    if (!slash || slash == path) return false;
    *slash = '\0';
    bool parent = create_directories(path);
    *slash = '/';
    return parent && (mkdir(path, 0700) == 0 || errno == EEXIST);
}
#endif

static bool parent_directory(const path_char *path, path_char *dir, bool create) {
#ifdef _WIN32
    wcscpy(dir, path);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash) {
        wcscpy(dir, L".");
        return true;
    }
    if (slash == dir || (slash == dir + 2 && dir[1] == L':')) slash[1] = L'\0';
    else *slash = L'\0';
    return !create || create_directories(dir);
#else
    strcpy(dir, path);
    char *slash = strrchr(dir, '/');
    if (!slash) {
        strcpy(dir, ".");
        return true;
    }
    if (slash == dir) slash[1] = '\0';
    else *slash = '\0';
    return !create || (strcmp(dir, "/") == 0) || create_directories(dir);
#endif
}

bool app_settings_dir(char *out, size_t capacity, const char *override, bool create) {
    if (!out || capacity == 0) return false;
    out[0] = '\0';
    path_char path[SETTINGS_PATH_CAP];
    path_char dir[SETTINGS_PATH_CAP];
    if (!settings_path(path, override) || !parent_directory(path, dir, create)) return false;
#ifdef _WIN32
    int len = WideCharToMultiByte(CP_UTF8, 0, dir, -1, out, (int)capacity, NULL, NULL);
    return len > 0 && (size_t)len <= capacity;
#else
    int len = snprintf(out, capacity, "%s", dir);
    return len >= 0 && (size_t)len < capacity;
#endif
}

int app_system_dark(void) {
#ifdef __APPLE__
    return macos_system_dark();
#elif defined(_WIN32)
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
#else
    const char *gtk = getenv("GTK_THEME");
    if (!gtk) return -1;
    for (const char *p = gtk; *p; ++p) {
        if (tolower((unsigned char)p[0]) == 'd' &&
            tolower((unsigned char)p[1]) == 'a' &&
            tolower((unsigned char)p[2]) == 'r' &&
            tolower((unsigned char)p[3]) == 'k')
            return 1;
    }
    return -1;
#endif
}

#ifdef APP_HAVE_X11
static Display *x11_display(void) {
    static Display *dpy;
    static bool tried;
    if (!tried) {
        tried = true;
        dpy = XOpenDisplay(NULL);
    }
    return dpy;
}

static void x11_frame_extents(Display *dpy, Window win, int *left, int *top) {
    *left = 0;
    *top = 0;
    Atom prop = XInternAtom(dpy, "_NET_FRAME_EXTENTS", True);
    if (!prop) return;
    Atom actual_type = 0;
    int actual_format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, win, prop, 0, 4, False, XA_CARDINAL,
                           &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success && data) {
        if (actual_format == 32 && nitems >= 4) {
            long *extents = (long *)data;
            *left = (int)extents[0];
            *top = (int)extents[2];
        }
        XFree(data);
    }
}
#endif

bool app_window_get_pos(void *native_window, const char *backend, int *x, int *y) {
    if (!native_window || !backend || !x || !y) return false;
#ifdef __APPLE__
    if (strcmp(backend, "cocoa") == 0) return macos_window_get_pos(native_window, x, y);
#elif defined(_WIN32)
    if (strcmp(backend, "win32") == 0) {
        RECT r;
        if (!GetWindowRect((HWND)native_window, &r)) return false;
        *x = (int)r.left;
        *y = (int)r.top;
        return true;
    }
#elif defined(APP_HAVE_X11)
    if (strcmp(backend, "x11") == 0) {
        Display *dpy = x11_display();
        if (!dpy) return false;
        Window win = (Window)(uintptr_t)native_window;
        XWindowAttributes attr;
        Window child = 0;
        int rx = 0, ry = 0;
        if (!XGetWindowAttributes(dpy, win, &attr) || !attr.root) return false;
        if (!XTranslateCoordinates(dpy, win, attr.root, 0, 0, &rx, &ry, &child)) return false;
        int left = 0, top = 0;
        x11_frame_extents(dpy, win, &left, &top);
        *x = rx - left;
        *y = ry - top;
        return true;
    }
#endif
    return false;
}

bool app_window_set_pos(void *native_window, const char *backend, int x, int y) {
    if (!native_window || !backend) return false;
    if (x < WINDOW_POS_MIN || x > WINDOW_POS_MAX || y < WINDOW_POS_MIN || y > WINDOW_POS_MAX)
        return false;
#ifdef __APPLE__
    if (strcmp(backend, "cocoa") == 0) return macos_window_set_pos(native_window, x, y);
#elif defined(_WIN32)
    if (strcmp(backend, "win32") == 0) {
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
#elif defined(APP_HAVE_X11)
    if (strcmp(backend, "x11") == 0) {
        Display *dpy = x11_display();
        if (!dpy) return false;
        Window win = (Window)(uintptr_t)native_window;
        XWindowAttributes attr;
        if (!XGetWindowAttributes(dpy, win, &attr)) return false;
        int scr = DefaultScreen(dpy);
        int sw = DisplayWidth(dpy, scr);
        int sh = DisplayHeight(dpy, scr);
        if (x + attr.width < 64 || x > sw - 64 || y < 0 || y > sh - 48) return false;
        XMoveWindow(dpy, win, x, y);
        XFlush(dpy);
        return true;
    }
#endif
    return false;
}

void app_window_center(void *native_window, const char *backend) {
    if (!native_window || !backend) return;
#ifdef __APPLE__
    if (strcmp(backend, "cocoa") == 0) macos_window_center(native_window);
#elif defined(_WIN32)
    if (strcmp(backend, "win32") == 0) {
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
#elif defined(APP_HAVE_X11)
    if (strcmp(backend, "x11") == 0) {
        Display *dpy = x11_display();
        if (!dpy) return;
        Window win = (Window)(uintptr_t)native_window;
        XWindowAttributes attr;
        if (!XGetWindowAttributes(dpy, win, &attr)) return;
        int scr = DefaultScreen(dpy);
        int cx = (DisplayWidth(dpy, scr) - attr.width) / 2;
        int cy = (DisplayHeight(dpy, scr) - attr.height) / 2;
        XMoveWindow(dpy, win, cx > 0 ? cx : 0, cy > 0 ? cy : 0);
        XFlush(dpy);
    }
#endif
}

bool app_action_pressed(cui_ctx *ui, const app_settings *settings, app_action action) {
    if (!ui || !settings || action < 0 || action >= APP_ACTION_COUNT) return false;
    if (cui_wants_keyboard(ui)) return false;
    cui_key_combo combo = settings->keybindings[action].combo;
    return combo.key != CUI_KEY_NONE && cui_shortcut(ui, combo.mods, combo.key);
}

int app_keybinding_conflict(const app_settings *settings, app_action action) {
    if (!settings || action < 0 || action >= APP_ACTION_COUNT) return APP_CONFLICT_NONE;
    cui_key_combo combo = settings->keybindings[action].combo;
    if (combo.key == CUI_KEY_NONE) return APP_CONFLICT_NONE;
    if (combo.key == CUI_KEY_COMMA && combo.mods == CUI_MOD_PRIMARY)
        return APP_CONFLICT_SETTINGS;
    for (int i = 0; i < APP_ACTION_COUNT; ++i) {
        if (i == (int)action) continue;
        cui_key_combo other = settings->keybindings[i].combo;
        if (other.key == combo.key && other.mods == combo.mods) return i;
    }
    return APP_CONFLICT_NONE;
}

static bool write_settings(const path_char *path, yyjson_mut_doc *doc) {
    path_char temp[SETTINGS_PATH_CAP];
    path_char dir[SETTINGS_PATH_CAP];
    if (!parent_directory(path, dir, true)) return false;
#ifdef _WIN32
    if (!GetTempFileNameW(dir, L"cfg", 0, temp)) return false;
    FILE *file = _wfopen(temp, L"wb");
#else
    int len = snprintf(temp, sizeof temp, "%s.XXXXXX", path);
    if (len < 0 || (size_t)len >= sizeof temp) return false;
    int fd = mkstemp(temp);
    if (fd < 0) return false;
    FILE *file = fdopen(fd, "wb");
    if (!file) close(fd);
#endif
    bool written = file && yyjson_mut_write_fp(file, doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL, NULL);
    if (file && fclose(file) != 0) written = false;
#ifdef _WIN32
    if (written && MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING)) return true;
    DeleteFileW(temp);
#else
    if (written && rename(temp, path) == 0) return true;
    unlink(temp);
#endif
    return false;
}

static const struct {
    int bit;
    const char *name;
} mod_entries[] = {
    { CUI_MOD_SHIFT, "shift" },
    { CUI_MOD_CTRL, "ctrl" },
    { CUI_MOD_ALT, "alt" },
    { CUI_MOD_SUPER, "super" },
};

static const char *const theme_names[APP_THEME_COUNT] = {
    "system",
    "light",
    "dark",
};

static double json_number(yyjson_val *value) {
    return yyjson_is_num(value) ? yyjson_get_num(value) : NAN;
}

static bool str_eq_ignore_case(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool parse_theme_mode(yyjson_val *value, app_theme_mode *out) {
    if (!yyjson_is_str(value)) return false;
    const char *text = yyjson_get_str(value);
    size_t len = yyjson_get_len(value);
    if (!text || memchr(text, '\0', len)) return false;
    for (int i = 0; i < APP_THEME_COUNT; ++i) {
        if (str_eq_ignore_case(text, theme_names[i])) {
            *out = (app_theme_mode)i;
            return true;
        }
    }
    return false;
}

static bool parse_accent(yyjson_val *value, cui_accent *out) {
    if (!yyjson_is_str(value)) return false;
    const char *text = yyjson_get_str(value);
    size_t len = yyjson_get_len(value);
    if (!text || memchr(text, '\0', len)) return false;
    for (int i = 0; i < CUI_ACCENT_COUNT; ++i) {
        if (str_eq_ignore_case(text, cui_accent_name((cui_accent)i))) {
            *out = (cui_accent)i;
            return true;
        }
    }
    return false;
}

static bool parse_key(yyjson_val *value, cui_key *out) {
    if (!yyjson_is_str(value)) return false;
    const char *text = yyjson_get_str(value);
    size_t len = yyjson_get_len(value);
    if (!text || memchr(text, '\0', len)) return false;
    if (len == 0) {
        *out = CUI_KEY_NONE;
        return true;
    }
    for (int k = 1; k < CUI_KEY_COUNT; ++k) {
        if (strcmp(cui_key_name((cui_key)k), text) == 0) {
            *out = (cui_key)k;
            return true;
        }
    }
    return false;
}

static bool parse_mods(yyjson_val *value, int *out) {
    if (!yyjson_is_str(value)) return false;
    const char *text = yyjson_get_str(value);
    size_t len = yyjson_get_len(value);
    if (!text || memchr(text, '\0', len)) return false;
    int bits = 0;
    const char *p = text;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ') ++p;
        size_t tok_len = (size_t)(p - start);
        int matched = 0;
        for (size_t i = 0; i < sizeof mod_entries / sizeof mod_entries[0]; ++i) {
            if (strlen(mod_entries[i].name) == tok_len &&
                memcmp(mod_entries[i].name, start, tok_len) == 0) {
                matched = mod_entries[i].bit;
                break;
            }
        }
        if (!matched) return false;
        bits |= matched;
    }
    *out = bits;
    return true;
}

static void format_mods(int mods, char *buf, size_t cap) {
    size_t used = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < sizeof mod_entries / sizeof mod_entries[0]; ++i) {
        if (!(mods & mod_entries[i].bit)) continue;
        int n = snprintf(buf + used, cap - used, "%s%s", used ? " " : "", mod_entries[i].name);
        if (n > 0 && (size_t)n < cap - used) used += (size_t)n;
    }
}

bool app_settings_load(app_settings *settings, const char *override) {
    path_char path[SETTINGS_PATH_CAP];
    if (!settings_path(path, override)) return false;
#ifdef _WIN32
    FILE *file = _wfopen(path, L"rb");
#else
    FILE *file = fopen(path, "rb");
#endif
    if (!file) {
        if (errno != ENOENT) fputs("Could not read settings.json\n", stderr);
        return false;
    }
    yyjson_doc *doc = yyjson_read_fp(file, YYJSON_READ_ALLOW_BOM, NULL, NULL);
    fclose(file);
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        fputs("Invalid settings.json\n", stderr);
        yyjson_doc_free(doc);
        return false;
    }

    app_settings loaded = *settings;
    bool applied = false;

    yyjson_val *value = yyjson_obj_get(root, "example_string");
    const char *text = yyjson_get_str(value);
    size_t len = yyjson_get_len(value);
    if (text && len < sizeof loaded.example_string && !memchr(text, '\0', len)) {
        memcpy(loaded.example_string, text, len + 1);
        applied = true;
    }
    double number = json_number(yyjson_obj_get(root, "example_number"));
    if (number >= EXAMPLE_NUMBER_MIN && number <= EXAMPLE_NUMBER_MAX) {
        loaded.example_number = number;
        applied = true;
    }

    yyjson_val *appearance = yyjson_obj_get(root, "appearance");
    if (yyjson_is_obj(appearance)) {
        app_theme_mode mode = loaded.theme_mode;
        if (parse_theme_mode(yyjson_obj_get(appearance, "theme"), &mode)) {
            loaded.theme_mode = mode;
            applied = true;
        }
        cui_accent accent = loaded.accent;
        if (parse_accent(yyjson_obj_get(appearance, "accent"), &accent)) {
            loaded.accent = accent;
            applied = true;
        }
        double scale = json_number(yyjson_obj_get(appearance, "font_scale"));
        if (isfinite(scale) && scale >= FONT_SCALE_MIN - 1e-4 && scale <= FONT_SCALE_MAX + 1e-4) {
            if (scale < FONT_SCALE_MIN) scale = FONT_SCALE_MIN;
            if (scale > FONT_SCALE_MAX) scale = FONT_SCALE_MAX;
            loaded.font_scale = (float)scale;
            applied = true;
        }
        yyjson_val *reduced = yyjson_obj_get(appearance, "reduced_motion");
        if (yyjson_is_bool(reduced)) {
            loaded.reduced_motion = yyjson_get_bool(reduced);
            applied = true;
        }
    }

    yyjson_val *window = yyjson_obj_get(root, "window");
    if (yyjson_is_obj(window)) {
        double wx = json_number(yyjson_obj_get(window, "x"));
        double wy = json_number(yyjson_obj_get(window, "y"));
        if (wx >= WINDOW_POS_MIN && wx <= WINDOW_POS_MAX && wx == floor(wx) &&
            wy >= WINDOW_POS_MIN && wy <= WINDOW_POS_MAX && wy == floor(wy)) {
            loaded.window_has_pos = true;
            loaded.window_x = (int)wx;
            loaded.window_y = (int)wy;
            applied = true;
        }
        double width = json_number(yyjson_obj_get(window, "width"));
        if (width >= WINDOW_WIDTH_MIN && width <= WINDOW_WIDTH_MAX && width == floor(width)) {
            loaded.window_width = (int)width;
            applied = true;
        }
        double height = json_number(yyjson_obj_get(window, "height"));
        if (height >= WINDOW_HEIGHT_MIN && height <= WINDOW_HEIGHT_MAX && height == floor(height)) {
            loaded.window_height = (int)height;
            applied = true;
        }
        yyjson_val *fullscreen = yyjson_obj_get(window, "fullscreen");
        if (yyjson_is_bool(fullscreen)) {
            loaded.window_fullscreen = yyjson_get_bool(fullscreen);
            applied = true;
        }
    }

    yyjson_val *keybindings = yyjson_obj_get(root, "keybindings");
    if (yyjson_is_obj(keybindings)) {
        for (size_t i = 0; i < sizeof loaded.keybindings / sizeof loaded.keybindings[0]; ++i) {
            yyjson_val *entry = yyjson_obj_get(keybindings, loaded.keybindings[i].id);
            if (!yyjson_is_obj(entry)) continue;
            cui_key key = CUI_KEY_NONE;
            int mods = 0;
            yyjson_val *mods_val = yyjson_obj_get(entry, "mods");
            bool mods_ok = mods_val ? parse_mods(mods_val, &mods) : true;
            if (parse_key(yyjson_obj_get(entry, "key"), &key) && mods_ok &&
                (key != CUI_KEY_NONE || mods == 0)) {
                loaded.keybindings[i].combo = (cui_key_combo){ mods, key };
                applied = true;
            }
        }
    }
    yyjson_doc_free(doc);
    if (!applied) return false;
    *settings = loaded;
    return true;
}

bool app_settings_save(const app_settings *settings, const char *override) {
    path_char path[SETTINGS_PATH_CAP];
    if (!settings_path(path, override)) return false;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return false;
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    bool ok = root &&
        yyjson_mut_obj_add_str(doc, root, "example_string", settings->example_string) &&
        yyjson_mut_obj_add_real(doc, root, "example_number", settings->example_number);

    const char *theme_name = (settings->theme_mode >= 0 && settings->theme_mode < APP_THEME_COUNT)
        ? theme_names[settings->theme_mode] : theme_names[APP_THEME_SYSTEM];
    yyjson_mut_val *appearance = ok ? yyjson_mut_obj(doc) : NULL;
    ok = ok && appearance &&
        yyjson_mut_obj_add_str(doc, appearance, "theme", theme_name) &&
        yyjson_mut_obj_add_str(doc, appearance, "accent", cui_accent_name(settings->accent)) &&
        yyjson_mut_obj_add_real(doc, appearance, "font_scale", round((double)settings->font_scale * 100.0) / 100.0) &&
        yyjson_mut_obj_add_bool(doc, appearance, "reduced_motion", settings->reduced_motion) &&
        yyjson_mut_obj_add_val(doc, root, "appearance", appearance);

    yyjson_mut_val *window = ok ? yyjson_mut_obj(doc) : NULL;
    ok = ok && window;
    if (ok && settings->window_has_pos) {
        ok = yyjson_mut_obj_add_int(doc, window, "x", settings->window_x) &&
             yyjson_mut_obj_add_int(doc, window, "y", settings->window_y);
    }
    ok = ok &&
        yyjson_mut_obj_add_int(doc, window, "width", settings->window_width) &&
        yyjson_mut_obj_add_int(doc, window, "height", settings->window_height) &&
        yyjson_mut_obj_add_bool(doc, window, "fullscreen", settings->window_fullscreen) &&
        yyjson_mut_obj_add_val(doc, root, "window", window);

    yyjson_mut_val *keybindings = ok ? yyjson_mut_obj(doc) : NULL;
    ok = ok && keybindings && yyjson_mut_obj_add_val(doc, root, "keybindings", keybindings);
    for (size_t i = 0; ok && i < sizeof settings->keybindings / sizeof settings->keybindings[0]; ++i) {
        cui_key_combo combo = settings->keybindings[i].combo;
        char mods_buf[32];
        format_mods(combo.mods, mods_buf, sizeof mods_buf);
        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        ok = entry &&
             yyjson_mut_obj_add_str(doc, entry, "key", cui_key_name(combo.key)) &&
             yyjson_mut_obj_add_strcpy(doc, entry, "mods", mods_buf) &&
             yyjson_mut_obj_add_val(doc, keybindings, settings->keybindings[i].id, entry);
    }
    bool saved = ok && write_settings(path, doc);
    yyjson_mut_doc_free(doc);
    if (!saved) fputs("Could not save settings.json\n", stderr);
    return saved;
}
