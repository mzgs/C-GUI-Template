#include "settings.h"
#include <yyjson.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
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
#endif

#ifdef __APPLE__
bool macos_settings_path(char *path, size_t capacity);
#endif

static bool settings_path(path_char *path, const char *override) {
#ifdef _WIN32
    if (override) {
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

#ifndef _WIN32
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

static bool write_settings(const path_char *path, yyjson_mut_doc *doc) {
    path_char temp[SETTINGS_PATH_CAP];
#ifdef _WIN32
    wchar_t dir[SETTINGS_PATH_CAP];
    wcscpy(dir, path);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash) return false;
    *slash = L'\0';
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    if (!GetTempFileNameW(dir, L"cfg", 0, temp)) return false;
    FILE *file = _wfopen(temp, L"wb");
#else
    char dir[SETTINGS_PATH_CAP];
    strcpy(dir, path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        if (!create_directories(dir)) return false;
    }
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

static double json_number(yyjson_val *value) {
    return yyjson_is_num(value) ? yyjson_get_num(value) : NAN;
}

void app_settings_load(app_settings *settings, const char *override) {
    path_char path[SETTINGS_PATH_CAP];
    if (!settings_path(path, override)) return;
#ifdef _WIN32
    FILE *file = _wfopen(path, L"rb");
#else
    FILE *file = fopen(path, "rb");
#endif
    if (!file) {
        if (errno != ENOENT) fputs("Could not read settings.json\n", stderr);
        return;
    }
    yyjson_doc *doc = yyjson_read_fp(file, YYJSON_READ_ALLOW_BOM, NULL, NULL);
    fclose(file);
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        fputs("Invalid settings.json\n", stderr);
        yyjson_doc_free(doc);
        return;
    }

    yyjson_val *value = yyjson_obj_get(root, "example_string");
    const char *text = yyjson_get_str(value);
    size_t len = yyjson_get_len(value);
    if (text && len < sizeof settings->example_string && !memchr(text, '\0', len))
        memcpy(settings->example_string, text, len + 1);
    double number = json_number(yyjson_obj_get(root, "example_number"));
    if (number >= EXAMPLE_NUMBER_MIN && number <= EXAMPLE_NUMBER_MAX)
        settings->example_number = number;

    const int mod_mask = CUI_MOD_SHIFT | CUI_MOD_CTRL | CUI_MOD_ALT | CUI_MOD_SUPER;
    for (size_t i = 0; i < sizeof settings->keybindings / sizeof settings->keybindings[0]; ++i) {
        double key = json_number(yyjson_obj_get(root, settings->keybindings[i].key_name));
        double mods = json_number(yyjson_obj_get(root, settings->keybindings[i].mods_name));
        if (key >= CUI_KEY_NONE && key < CUI_KEY_COUNT && key == floor(key) &&
            mods >= 0 && mods <= mod_mask && mods == floor(mods) &&
            (key != CUI_KEY_NONE || mods == 0))
            settings->keybindings[i].combo = (cui_key_combo){ (int)mods, (cui_key)key };
    }
    yyjson_doc_free(doc);
}

bool app_settings_save(const app_settings *settings, const char *override) {
    path_char path[SETTINGS_PATH_CAP];
    if (!settings_path(path, override)) return false;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return false;
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    bool ok = root && yyjson_mut_obj_add_str(doc, root, "example_string", settings->example_string) &&
        yyjson_mut_obj_add_real(doc, root, "example_number", settings->example_number);
    for (size_t i = 0; ok && i < sizeof settings->keybindings / sizeof settings->keybindings[0]; ++i) {
        cui_key_combo combo = settings->keybindings[i].combo;
        ok = yyjson_mut_obj_add_int(doc, root, settings->keybindings[i].key_name, combo.key) &&
             yyjson_mut_obj_add_int(doc, root, settings->keybindings[i].mods_name, combo.mods);
    }
    bool saved = ok && write_settings(path, doc);
    yyjson_mut_doc_free(doc);
    if (!saved) fputs("Could not save settings.json\n", stderr);
    return saved;
}
