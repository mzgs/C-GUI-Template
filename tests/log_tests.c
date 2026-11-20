#include "app_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static bool read_all(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return true;
}

static void temp_log_paths(const char *tag, char *log_path, size_t cap, char *backup_path, size_t bcap) {
    const char *dir = getenv("TMPDIR");
#ifdef _WIN32
    if (!dir || !dir[0]) dir = getenv("TEMP");
    if (!dir || !dir[0]) dir = ".";
    snprintf(log_path, cap, "%s\\c_gui_log_test_%s_%d.log", dir, tag, (int)getpid());
#else
    if (!dir || !dir[0]) dir = "/tmp";
    snprintf(log_path, cap, "%s/c_gui_log_test_%s_%d.log", dir, tag, (int)getpid());
#endif
    snprintf(backup_path, bcap, "%s.1", log_path);
    remove(log_path);
    remove(backup_path);
}

static void test_write_and_filter(void) {
    char path[1024], backup[1024];
    temp_log_paths("basic", path, sizeof path, backup, sizeof backup);

    CHECK(strcmp(app_log_path(), "") == 0);
    CHECK(app_log_init(path, 4096));
    CHECK(strcmp(app_log_path(), path) == 0);

    app_log_set_level(APP_LOG_INFO);
    app_log_debug("hidden debug %d", 1);
    app_log_info("hello %s\n", "world");
    app_log_set_level(APP_LOG_DEBUG);
    app_log_debug("visible debug %d", 2);
    app_log_shutdown();
    CHECK(strcmp(app_log_path(), "") == 0);

    char content[2048];
    CHECK(read_all(path, content, sizeof content));
    CHECK(strstr(content, "hidden debug") == NULL);
    CHECK(strstr(content, "[INFO] hello world\n") != NULL);
    CHECK(strstr(content, "[INFO] hello world\n\n") == NULL);
    CHECK(strstr(content, "[DEBUG] visible debug 2\n") != NULL);

    remove(path);
    remove(backup);
}

static void test_rotation(void) {
    char path[1024], backup[1024];
    temp_log_paths("rotate", path, sizeof path, backup, sizeof backup);

    app_log_set_level(APP_LOG_INFO);
    CHECK(app_log_init(path, 100));
    app_log_info("first entry with enough bytes to take space 1111111111");
    app_log_info("second entry should trigger rotation into backup 2222222222");
    app_log_shutdown();

    char cur[2048], old[2048];
    CHECK(read_all(backup, old, sizeof old));
    CHECK(read_all(path, cur, sizeof cur));
    CHECK(strstr(old, "first entry") != NULL);
    CHECK(strstr(cur, "second entry") != NULL);
    CHECK(strstr(cur, "first entry") == NULL);

    remove(path);
    remove(backup);
}

int main(void) {
    test_write_and_filter();
    test_rotation();
    if (failures > 0) {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return 1;
    }
    puts("All log tests passed.");
    return 0;
}
