#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<X11/Xlib.h>) && __has_include(<X11/Xatom.h>) && __has_include(<X11/Xutil.h>)
#define APP_HAVE_X11 1
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#endif

int app_system_dark(void) {
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
}

bool app_os_info(char *out, size_t capacity) {
    if (!out || capacity == 0) return false;
    struct utsname u;
    if (uname(&u) == 0) {
        int n = snprintf(out, capacity, "%s %s (%s)", u.sysname, u.release, u.machine);
        return n > 0 && (size_t)n < capacity;
    }
    int n = snprintf(out, capacity, "Linux");
    return n > 0 && (size_t)n < capacity;
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
#ifdef APP_HAVE_X11
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
    if (x < APP_WINDOW_POS_MIN || x > APP_WINDOW_POS_MAX ||
        y < APP_WINDOW_POS_MIN || y > APP_WINDOW_POS_MAX)
        return false;
#ifdef APP_HAVE_X11
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
#ifdef APP_HAVE_X11
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

void app_window_apply_metadata(void *native_window, const char *backend) {
    if (!native_window || !backend) return;
#ifdef APP_HAVE_X11
    if (strcmp(backend, "x11") == 0 && APP_BUNDLE_ID[0] != '\0') {
        Display *dpy = x11_display();
        if (!dpy) return;
        Window win = (Window)(uintptr_t)native_window;
        XClassHint *hint = XAllocClassHint();
        if (!hint) return;
        char wm_class[256];
        snprintf(wm_class, sizeof wm_class, "%s", APP_BUNDLE_ID);
        hint->res_name = wm_class;
        hint->res_class = wm_class;
        XSetClassHint(dpy, win, hint);
        XFree(hint);
        XFlush(dpy);
    }
#endif
}

void app_window_set_dark(void *native_window, const char *backend, bool dark) {
    (void)native_window;
    (void)backend;
    (void)dark;
}

static void linux_set_err(char *err, size_t err_cap, const char *msg) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", msg ? msg : "Unknown error.");
}

static const char *linux_release_arch(void) {
#if defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(__x86_64__)
    return "x64";
#else
    return NULL;
#endif
}

static bool linux_run_process(char *const argv[], char *out, size_t out_cap) {
    int pipefd[2] = { -1, -1 };
    if (out && out_cap > 0) {
        out[0] = '\0';
        if (pipe(pipefd) != 0) return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (pipefd[0] >= 0) close(pipefd[0]);
        if (pipefd[1] >= 0) close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            (void)dup2(devnull, STDIN_FILENO);
            if (pipefd[1] < 0) (void)dup2(devnull, STDOUT_FILENO);
            (void)dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        if (pipefd[0] >= 0) close(pipefd[0]);
        if (pipefd[1] >= 0) {
            (void)dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
        }
        execvp(argv[0], argv);
        _exit(127);
    }

    if (pipefd[1] >= 0) close(pipefd[1]);
    bool truncated = false;
    if (pipefd[0] >= 0) {
        size_t len = 0;
        for (;;) {
            char chunk[256];
            ssize_t r = read(pipefd[0], chunk, sizeof chunk);
            if (r < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (r == 0) break;
            if (len + (size_t)r < out_cap) {
                memcpy(out + len, chunk, (size_t)r);
                len += (size_t)r;
            } else {
                truncated = true;
            }
        }
        close(pipefd[0]);
        out[len] = '\0';
        while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
            out[--len] = '\0';
        }
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return !truncated && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool linux_current_exe(char *exe_path, size_t cap) {
    if (!exe_path || cap < 2) return false;
    ssize_t n = readlink("/proc/self/exe", exe_path, cap - 1);
    if (n <= 0 || (size_t)n >= cap - 1) return false;
    exe_path[n] = '\0';
    if (strstr(exe_path, " (deleted)")) return false;
    return true;
}

static void linux_remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (!S_ISDIR(st.st_mode)) {
        (void)unlink(path);
        return;
    }
    DIR *dir = opendir(path);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[PATH_MAX];
            int n = snprintf(child, sizeof child, "%s/%s", path, ent->d_name);
            if (n > 0 && (size_t)n < sizeof child) linux_remove_tree(child);
        }
        closedir(dir);
    }
    (void)rmdir(path);
}

static bool linux_copy_executable(const char *src, const char *dst) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) return false;
    int out_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0755);
    if (out_fd < 0) {
        close(in_fd);
        return false;
    }

    bool ok = true;
    char buf[16384];
    for (;;) {
        ssize_t r = read(in_fd, buf, sizeof buf);
        if (r < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (r == 0) break;
        ssize_t written = 0;
        while (written < r) {
            ssize_t w = write(out_fd, buf + written, (size_t)(r - written));
            if (w < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            written += w;
        }
        if (!ok) break;
    }
    if (ok && fchmod(out_fd, 0755) != 0) ok = false;
    if (ok && fsync(out_fd) != 0) ok = false;
    close(in_fd);
    if (close(out_fd) != 0) ok = false;
    if (!ok) (void)unlink(dst);
    return ok;
}

bool app_http_resolve_redirect(const char *url, char *final_url, size_t capacity,
                               char *err, size_t err_cap) {
    if (!url || !final_url || capacity == 0) {
        linux_set_err(err, err_cap, "Invalid update check request.");
        return false;
    }
    char *argv[] = {
        "curl", "-fsSLI", "--max-time", "15",
        "-o", "/dev/null", "-w", "%{url_effective}",
        "--", (char *)url, NULL
    };
    if (!linux_run_process(argv, final_url, capacity) || !final_url[0]) {
        linux_set_err(err, err_cap, "Could not reach GitHub (check network connection and curl).");
        return false;
    }
    return true;
}

bool app_updater_can_self_update(void) {
    if (!linux_release_arch()) return false;
    char exe_path[PATH_MAX];
    if (!linux_current_exe(exe_path, sizeof exe_path)) return false;
    char *slash = strrchr(exe_path, '/');
    if (!slash) return false;
    if (slash == exe_path) {
        return access("/", W_OK) == 0;
    }
    *slash = '\0';
    return access(exe_path, W_OK) == 0;
}

bool app_updater_install_release(const char *repo, const char *tag,
                                 char *relaunch_path, size_t relaunch_cap,
                                 app_updater_abort_fn should_abort, void *abort_user,
                                 char *err, size_t err_cap) {
    if (!repo || !tag || !app_updater_can_self_update()) {
        linux_set_err(err, err_cap, "In-place update is unavailable for this installation.");
        return false;
    }
    const char *slash = strchr(repo, '/');
    const char *arch = linux_release_arch();
    if (!slash || !slash[1] || !arch) {
        linux_set_err(err, err_cap, "Invalid repository or architecture.");
        return false;
    }
    const char *repo_name = slash + 1;

    char exe_path[PATH_MAX];
    if (!linux_current_exe(exe_path, sizeof exe_path)) {
        linux_set_err(err, err_cap, "Could not locate current executable.");
        return false;
    }
    char parent_dir[PATH_MAX];
    const char *exe_slash = strrchr(exe_path, '/');
    if (!exe_slash || !exe_slash[1]) {
        linux_set_err(err, err_cap, "Invalid executable path.");
        return false;
    }
    const char *exe_name = exe_slash + 1;
    size_t parent_len = (size_t)(exe_slash - exe_path);
    if (parent_len == 0) {
        snprintf(parent_dir, sizeof parent_dir, "/");
    } else if (parent_len < sizeof parent_dir) {
        memcpy(parent_dir, exe_path, parent_len);
        parent_dir[parent_len] = '\0';
    } else {
        linux_set_err(err, err_cap, "Executable path is too long.");
        return false;
    }

    char url[1024];
    int un = snprintf(url, sizeof url,
                      "https://github.com/%s/releases/download/%s/%s-%s-linux-%s.tar.gz",
                      repo, tag, repo_name, tag, arch);
    if (un < 0 || (size_t)un >= sizeof url) {
        linux_set_err(err, err_cap, "Release download URL is too long.");
        return false;
    }

    const char *tmp_base = getenv("TMPDIR");
    if (!tmp_base || !tmp_base[0]) tmp_base = "/tmp";
    char tmp_dir[PATH_MAX];
    int tn = snprintf(tmp_dir, sizeof tmp_dir, "%s/app-update.XXXXXX", tmp_base);
    if (tn < 0 || (size_t)tn >= sizeof tmp_dir || !mkdtemp(tmp_dir)) {
        linux_set_err(err, err_cap, "Could not create a temporary directory.");
        return false;
    }

    char archive_path[PATH_MAX];
    char extract_dir[PATH_MAX];
    char staged_path[PATH_MAX];
    staged_path[0] = '\0';
    bool ok = false;

    do {
        if (snprintf(archive_path, sizeof archive_path, "%s/update.tar.gz", tmp_dir) >= (int)sizeof archive_path ||
            snprintf(extract_dir, sizeof extract_dir, "%s/extract", tmp_dir) >= (int)sizeof extract_dir) {
            linux_set_err(err, err_cap, "Temporary path is too long.");
            break;
        }

        char *curl_argv[] = {
            "curl", "-fsSL", "--max-time", "180",
            "-o", archive_path, "--", url, NULL
        };
        if (!linux_run_process(curl_argv, NULL, 0)) {
            linux_set_err(err, err_cap, "Could not download the Linux release archive.");
            break;
        }
        if (should_abort && should_abort(abort_user)) {
            linux_set_err(err, err_cap, "Update cancelled.");
            break;
        }
        if (mkdir(extract_dir, 0700) != 0) {
            linux_set_err(err, err_cap, "Could not create extraction directory.");
            break;
        }

        char *tar_argv[] = { "tar", "-xzf", archive_path, "-C", extract_dir, NULL };
        if (!linux_run_process(tar_argv, NULL, 0)) {
            linux_set_err(err, err_cap, "Could not extract update archive.");
            break;
        }

        char src_bin[PATH_MAX] = "";
        DIR *dir = opendir(extract_dir);
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != NULL) {
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
                char candidate[PATH_MAX];
                int cn = snprintf(candidate, sizeof candidate, "%s/%s", extract_dir, ent->d_name);
                if (cn <= 0 || (size_t)cn >= sizeof candidate) continue;
                struct stat st;
                if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111)) {
                    if (strcmp(ent->d_name, exe_name) == 0) {
                        snprintf(src_bin, sizeof src_bin, "%s", candidate);
                        break;
                    }
                    if (!src_bin[0]) snprintf(src_bin, sizeof src_bin, "%s", candidate);
                }
            }
            closedir(dir);
        }
        if (!src_bin[0]) {
            linux_set_err(err, err_cap, "Downloaded archive does not contain an executable.");
            break;
        }

        if (snprintf(staged_path, sizeof staged_path, "%s/.%s.updating.%ld",
                     parent_dir, exe_name, (long)getpid()) >= (int)sizeof staged_path) {
            staged_path[0] = '\0';
            linux_set_err(err, err_cap, "Destination path is too long.");
            break;
        }
        (void)unlink(staged_path);
        if (!linux_copy_executable(src_bin, staged_path)) {
            staged_path[0] = '\0';
            linux_set_err(err, err_cap, "Could not stage updated executable.");
            break;
        }
        if (should_abort && should_abort(abort_user)) {
            linux_set_err(err, err_cap, "Update cancelled.");
            break;
        }
        if (rename(staged_path, exe_path) != 0) {
            linux_set_err(err, err_cap, "Could not replace the installed executable.");
            break;
        }
        staged_path[0] = '\0';
        if (relaunch_path && relaunch_cap > 0) {
            snprintf(relaunch_path, relaunch_cap, "%s", exe_path);
        }
        ok = true;
    } while (0);

    if (staged_path[0]) (void)unlink(staged_path);
    linux_remove_tree(tmp_dir);
    return ok;
}

bool app_updater_relaunch(const char *relaunch_path) {
    if (!relaunch_path || !relaunch_path[0]) return false;
    char pid_str[32];
    snprintf(pid_str, sizeof pid_str, "%ld", (long)getpid());

    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        (void)setsid();
        pid_t grand = fork();
        if (grand != 0) _exit(0);

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            (void)dup2(devnull, STDIN_FILENO);
            (void)dup2(devnull, STDOUT_FILENO);
            (void)dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        static const char script[] =
            "pid=\"$1\"; bin=\"$2\"; i=0; "
            "while kill -0 \"$pid\" 2>/dev/null && [ \"$i\" -lt 100 ]; do "
            "sleep 0.05; i=$((i + 1)); "
            "done; "
            "exec \"$bin\"";
        execl("/bin/sh", "sh", "-c", script, "relaunch", pid_str, relaunch_path, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }
    return true;
}
