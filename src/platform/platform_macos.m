#import <Cocoa/Cocoa.h>
#include "platform.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

bool macos_settings_path(char *path, size_t capacity) {
    @autoreleasepool {
        NSURL *support = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                               inDomains:NSUserDomainMask].firstObject;
        NSString *name = NSBundle.mainBundle.infoDictionary[@"CFBundleName"];
        if (!name.length) name = @APP_NAME;
        if (!support || !name.length) return false;
        NSURL *folder = [support URLByAppendingPathComponent:name isDirectory:YES];
        NSURL *file = [folder URLByAppendingPathComponent:@"settings.json" isDirectory:NO];
        return [file getFileSystemRepresentation:path maxLength:capacity];
    }
}

int app_system_dark(void) {
    if (@available(macOS 10.14, *)) {
        @autoreleasepool {
            NSAppearance *appearance = NSApp ? [NSApp effectiveAppearance] : nil;
            if (appearance) {
                NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:@[
                    NSAppearanceNameAqua, NSAppearanceNameDarkAqua
                ]];
                return [match isEqualToString:NSAppearanceNameDarkAqua] ? 1 : 0;
            }
            NSString *style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
            return [style isEqualToString:@"Dark"] ? 1 : 0;
        }
    }
    return -1;
}

static bool macos_rect_visible_on_any_screen(NSRect frame) {
    /* Ensure the top titlebar band is reachable on at least one active display. */
    CGFloat bar_h = frame.size.height < 32.0 ? frame.size.height : 32.0;
    NSRect titlebar = NSMakeRect(frame.origin.x, NSMaxY(frame) - bar_h, frame.size.width, bar_h);
    for (NSScreen *screen in [NSScreen screens]) {
        NSRect visible = screen.visibleFrame;
        NSRect overlap = NSIntersectionRect(titlebar, visible);
        if (overlap.size.width >= 64.0 && overlap.size.height >= 16.0) return true;
    }
    return false;
}

bool app_window_is_normal(void *native_window, const char *backend) {
    if (!native_window || !backend || strcmp(backend, "cocoa") != 0) return true;
    @autoreleasepool {
        NSWindow *win = (__bridge NSWindow *)native_window;
        return !win.isMiniaturized;
    }
}

bool app_window_get_pos(void *native_window, const char *backend, int *x, int *y) {
    if (!native_window || !backend || !x || !y || strcmp(backend, "cocoa") != 0) return false;
    @autoreleasepool {
        NSWindow *win = (__bridge NSWindow *)native_window;
        if (win.isMiniaturized) return false;
        NSRect frame = win.frame;
        *x = (int)llround(frame.origin.x);
        *y = (int)llround(frame.origin.y);
        return true;
    }
}

bool app_window_set_pos(void *native_window, const char *backend, int x, int y) {
    if (!native_window || !backend || strcmp(backend, "cocoa") != 0) return false;
    if (x < APP_WINDOW_POS_MIN || x > APP_WINDOW_POS_MAX ||
        y < APP_WINDOW_POS_MIN || y > APP_WINDOW_POS_MAX)
        return false;
    @autoreleasepool {
        NSWindow *win = (__bridge NSWindow *)native_window;
        NSRect frame = win.frame;
        frame.origin = NSMakePoint((CGFloat)x, (CGFloat)y);
        if (!macos_rect_visible_on_any_screen(frame)) return false;
        [win setFrameOrigin:frame.origin];
        return true;
    }
}

void app_window_center(void *native_window, const char *backend, int width, int height) {
    (void)width;
    (void)height;
    if (!native_window || !backend || strcmp(backend, "cocoa") != 0) return;
    @autoreleasepool {
        NSWindow *win = (__bridge NSWindow *)native_window;
        [win center];
    }
}

void app_window_apply_metadata(void *native_window, const char *backend) {
    (void)native_window;
    (void)backend;
}

void app_window_set_hooks(void *native_window, const char *backend, const app_window_hooks *hooks) {
    (void)native_window;
    (void)backend;
    (void)hooks;
}

void app_platform_shutdown(void) {
}

void app_window_set_dark(void *native_window, const char *backend, bool dark) {
    if (!native_window || !backend || strcmp(backend, "cocoa") != 0) return;
    if (@available(macOS 10.14, *)) {
        @autoreleasepool {
            NSWindow *win = (__bridge NSWindow *)native_window;
            NSAppearanceName desired = dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
            if (![win.appearance.name isEqualToString:desired]) {
                win.appearance = [NSAppearance appearanceNamed:desired];
            }
        }
    }
}

static void macos_set_err(char *err, size_t err_cap, const char *msg) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", msg ? msg : "Unknown error.");
}

static const char *macos_release_arch(void) {
#if defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(__x86_64__)
    return "x64";
#else
    return NULL;
#endif
}

bool app_os_info(char *out, size_t capacity) {
    if (!out || capacity == 0) return false;
    @autoreleasepool {
        NSOperatingSystemVersion v = NSProcessInfo.processInfo.operatingSystemVersion;
        const char *arch = macos_release_arch();
        int n = snprintf(out, capacity, "macOS %ld.%ld.%ld (%s)",
                         (long)v.majorVersion, (long)v.minorVersion, (long)v.patchVersion,
                         arch ? arch : "unknown");
        return n > 0 && (size_t)n < capacity;
    }
}

static NSString *macos_user_agent(void) {
    return [NSString stringWithFormat:@"%s/%s", APP_NAME, APP_VERSION];
}

static int macos_max_close_fd(void) {
    long limit = sysconf(_SC_OPEN_MAX);
    if (limit < STDERR_FILENO + 1) return 256;
    if (limit > 4096) return 4096;
    return (int)limit;
}

bool app_http_resolve_redirect(const char *url, char *final_url, size_t capacity,
                               app_updater_abort_fn should_abort, void *abort_user,
                               char *err, size_t err_cap) {
    if (!url || !final_url || capacity == 0) {
        macos_set_err(err, err_cap, "Invalid update check request.");
        return false;
    }
    final_url[0] = '\0';
    if (should_abort && should_abort(abort_user)) {
        macos_set_err(err, err_cap, "Update check cancelled.");
        return false;
    }

    @autoreleasepool {
        NSString *urlStr = [NSString stringWithUTF8String:url];
        NSURL *nsUrl = urlStr ? [NSURL URLWithString:urlStr] : nil;
        if (!nsUrl) {
            macos_set_err(err, err_cap, "Invalid update URL.");
            return false;
        }

        NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.timeoutIntervalForRequest = 15.0;
        cfg.timeoutIntervalForResource = 20.0;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg];

        NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:nsUrl];
        req.HTTPMethod = @"HEAD";
        req.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        [req setValue:macos_user_agent() forHTTPHeaderField:@"User-Agent"];

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block NSString *resolved = nil;
        __block NSInteger statusCode = 0;
        __block NSString *errMsg = nil;

        NSURLSessionDataTask *task =
            [session dataTaskWithRequest:req
                       completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                (void)data;
                if (error) {
                    errMsg = error.localizedDescription ?: @"Network error.";
                } else if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
                    NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
                    statusCode = http.statusCode;
                    resolved = http.URL.absoluteString;
                } else {
                    errMsg = @"Unexpected server response.";
                }
                dispatch_semaphore_signal(sem);
            }];
        [task resume];
        bool aborted = false;
        for (;;) {
            dispatch_time_t slice = dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC);
            if (dispatch_semaphore_wait(sem, slice) == 0) break;
            if (!aborted && should_abort && should_abort(abort_user)) {
                aborted = true;
                [task cancel];
                [session invalidateAndCancel];
            }
        }
        if (!aborted) [session finishTasksAndInvalidate];
        if (aborted) {
            macos_set_err(err, err_cap, "Update check cancelled.");
            return false;
        }

        if (errMsg) {
            macos_set_err(err, err_cap, errMsg.UTF8String);
            return false;
        }
        if (statusCode < 200 || statusCode >= 400) {
            char buf[64];
            snprintf(buf, sizeof buf, "Server returned HTTP %ld.", (long)statusCode);
            macos_set_err(err, err_cap, buf);
            return false;
        }
        if (!resolved.length) {
            macos_set_err(err, err_cap, "Empty redirect response.");
            return false;
        }
        const char *utf8 = resolved.UTF8String;
        if (!utf8 || strlen(utf8) >= capacity) {
            macos_set_err(err, err_cap, "Resolved URL is too long.");
            return false;
        }
        snprintf(final_url, capacity, "%s", utf8);
        return true;
    }
}

bool app_updater_can_self_update(void) {
    if (!macos_release_arch()) return false;
    @autoreleasepool {
        NSString *bundlePath = NSBundle.mainBundle.bundlePath;
        if (!bundlePath.length || ![bundlePath hasSuffix:@".app"]) return false;
        if ([bundlePath rangeOfString:@"/AppTranslocation/"].location != NSNotFound) return false;
        NSString *parentDir = [bundlePath stringByDeletingLastPathComponent];
        if (!parentDir.length) return false;
        if (access(parentDir.fileSystemRepresentation, W_OK) != 0) return false;
        if (access(bundlePath.fileSystemRepresentation, W_OK) != 0) return false;
        return true;
    }
}

static bool macos_run_tool(NSString *launchPath, NSArray<NSString *> *args,
                           app_updater_abort_fn should_abort, void *abort_user) {
    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:launchPath];
    task.arguments = args;
    NSFileHandle *nullHandle = [NSFileHandle fileHandleWithNullDevice];
    task.standardOutput = nullHandle;
    task.standardError = nullHandle;
    NSError *error = nil;
    if (![task launchAndReturnError:&error]) return false;
    bool aborted = false;
    while ([task isRunning]) {
        if (!aborted && should_abort && should_abort(abort_user)) {
            aborted = true;
            [task terminate];
            break;
        }
        usleep(25000);
    }
    [task waitUntilExit];
    return !aborted && task.terminationReason == NSTaskTerminationReasonExit && task.terminationStatus == 0;
}

bool app_updater_install_release(const char *repo, const char *tag,
                                 char *relaunch_path, size_t relaunch_cap,
                                 app_updater_abort_fn should_abort, void *abort_user,
                                 char *err, size_t err_cap) {
    if (!repo || !tag || !app_updater_can_self_update()) {
        macos_set_err(err, err_cap, "In-place update is unavailable for this installation.");
        return false;
    }
    const char *slash = strchr(repo, '/');
    const char *arch = macos_release_arch();
    if (!slash || !slash[1] || !arch) {
        macos_set_err(err, err_cap, "Invalid repository or architecture.");
        return false;
    }
    const char *repo_name = slash + 1;

    @autoreleasepool {
        NSFileManager *fm = [NSFileManager defaultManager];
        NSString *bundlePath = NSBundle.mainBundle.bundlePath;
        NSString *parentDir = [bundlePath stringByDeletingLastPathComponent];
        NSString *bundleName = [bundlePath lastPathComponent];

        NSString *urlStr = [NSString stringWithFormat:
            @"https://github.com/%s/releases/download/%s/%s-%s-macos-%s.zip",
            repo, tag, repo_name, tag, arch];
        NSURL *url = [NSURL URLWithString:urlStr];
        if (!url) {
            macos_set_err(err, err_cap, "Invalid release download URL.");
            return false;
        }

        NSString *tmpBase = NSTemporaryDirectory();
        if (!tmpBase.length) tmpBase = @"/tmp";
        char tmpTemplate[4096];
        int n = snprintf(tmpTemplate, sizeof tmpTemplate, "%s/app-update.XXXXXX", tmpBase.fileSystemRepresentation);
        if (n < 0 || (size_t)n >= sizeof tmpTemplate || !mkdtemp(tmpTemplate)) {
            macos_set_err(err, err_cap, "Could not create a temporary directory.");
            return false;
        }
        NSString *tmpDir = [fm stringWithFileSystemRepresentation:tmpTemplate length:strlen(tmpTemplate)];
        NSString *archivePath = [tmpDir stringByAppendingPathComponent:@"update.zip"];
        NSString *extractDir = [tmpDir stringByAppendingPathComponent:@"extract"];
        NSString *stagedPath = nil;
        bool ok = false;

        do {
            NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
            cfg.timeoutIntervalForRequest = 30.0;
            cfg.timeoutIntervalForResource = 180.0;
            NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg];

            NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
            req.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
            [req setValue:macos_user_agent() forHTTPHeaderField:@"User-Agent"];

            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block NSInteger statusCode = 0;
            __block NSString *dlErr = nil;

            NSURLSessionDownloadTask *task =
                [session downloadTaskWithRequest:req
                               completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
                    if (error) {
                        dlErr = error.localizedDescription ?: @"Download failed.";
                    } else if (![response isKindOfClass:[NSHTTPURLResponse class]]) {
                        dlErr = @"Unexpected download response.";
                    } else {
                        NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
                        statusCode = http.statusCode;
                        if (statusCode == 200 && location) {
                            NSError *moveErr = nil;
                            if (![[NSFileManager defaultManager] moveItemAtURL:location
                                                                         toURL:[NSURL fileURLWithPath:archivePath]
                                                                         error:&moveErr]) {
                                dlErr = moveErr.localizedDescription ?: @"Could not save update archive.";
                            }
                        } else if (statusCode == 404) {
                            dlErr = @"No macOS update archive was found for this release.";
                        } else {
                            dlErr = [NSString stringWithFormat:@"Download failed (HTTP %ld).", (long)statusCode];
                        }
                    }
                    dispatch_semaphore_signal(sem);
                }];
            [task resume];
            bool dlAborted = false;
            for (;;) {
                dispatch_time_t slice = dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC);
                if (dispatch_semaphore_wait(sem, slice) == 0) break;
                if (!dlAborted && should_abort && should_abort(abort_user)) {
                    dlAborted = true;
                    [task cancel];
                    [session invalidateAndCancel];
                }
            }
            if (!dlAborted) [session finishTasksAndInvalidate];

            if (dlAborted || (should_abort && should_abort(abort_user))) {
                macos_set_err(err, err_cap, "Update cancelled.");
                break;
            }
            if (dlErr) {
                macos_set_err(err, err_cap, dlErr.UTF8String);
                break;
            }

            if (![fm createDirectoryAtPath:extractDir withIntermediateDirectories:YES attributes:nil error:nil]) {
                macos_set_err(err, err_cap, "Could not prepare extraction directory.");
                break;
            }
            if (!macos_run_tool(@"/usr/bin/ditto", @[ @"-x", @"-k", archivePath, extractDir ],
                                should_abort, abort_user)) {
                macos_set_err(err, err_cap,
                              (should_abort && should_abort(abort_user))
                                  ? "Update cancelled."
                                  : "Could not extract update archive.");
                break;
            }

            NSArray<NSString *> *entries = [fm contentsOfDirectoryAtPath:extractDir error:nil];
            NSString *srcApp = nil;
            for (NSString *entry in entries) {
                if (![entry hasSuffix:@".app"]) continue;
                NSString *candidate = [extractDir stringByAppendingPathComponent:entry];
                NSString *plist = [candidate stringByAppendingPathComponent:@"Contents/Info.plist"];
                if ([fm fileExistsAtPath:plist]) {
                    srcApp = candidate;
                    break;
                }
            }
            if (!srcApp) {
                macos_set_err(err, err_cap, "Downloaded archive does not contain an app bundle.");
                break;
            }

            pid_t pid = getpid();
            stagedPath = [parentDir stringByAppendingPathComponent:
                [NSString stringWithFormat:@".%@.updating.%ld", bundleName, (long)pid]];
            NSString *backupPath = [parentDir stringByAppendingPathComponent:
                [NSString stringWithFormat:@".%@.backup.%ld", bundleName, (long)pid]];
            [fm removeItemAtPath:stagedPath error:nil];
            [fm removeItemAtPath:backupPath error:nil];

            if (!macos_run_tool(@"/usr/bin/ditto", @[ srcApp, stagedPath ],
                                should_abort, abort_user)) {
                macos_set_err(err, err_cap,
                              (should_abort && should_abort(abort_user))
                                  ? "Update cancelled."
                                  : "Could not stage updated app bundle.");
                break;
            }
            (void)macos_run_tool(@"/usr/bin/xattr", @[ @"-dr", @"com.apple.quarantine", stagedPath ],
                                 should_abort, abort_user);

            if (should_abort && should_abort(abort_user)) {
                macos_set_err(err, err_cap, "Update cancelled.");
                break;
            }

            const char *curFs = bundlePath.fileSystemRepresentation;
            const char *stagedFs = stagedPath.fileSystemRepresentation;
            const char *backupFs = backupPath.fileSystemRepresentation;
            if (rename(curFs, backupFs) != 0) {
                macos_set_err(err, err_cap, "Could not replace the installed app bundle.");
                break;
            }
            if (rename(stagedFs, curFs) != 0) {
                (void)rename(backupFs, curFs);
                macos_set_err(err, err_cap, "Could not move the updated app bundle into place.");
                break;
            }
            stagedPath = nil;
            [fm removeItemAtPath:backupPath error:nil];

            if (relaunch_path && relaunch_cap > 0) {
                if (![bundlePath getFileSystemRepresentation:relaunch_path maxLength:relaunch_cap]) {
                    relaunch_path[0] = '\0';
                }
            }
            ok = true;
        } while (0);

        if (stagedPath) [fm removeItemAtPath:stagedPath error:nil];
        [fm removeItemAtPath:tmpDir error:nil];
        return ok;
    }
}

bool app_updater_relaunch(const char *relaunch_path) {
    if (!relaunch_path || !relaunch_path[0]) return false;
    char pid_str[32];
    snprintf(pid_str, sizeof pid_str, "%ld", (long)getpid());
    int max_fd = macos_max_close_fd();

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
        for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) {
            close(fd);
        }
        static const char script[] =
            "pid=\"$1\"; app=\"$2\"; i=0; "
            "while kill -0 \"$pid\" 2>/dev/null && [ \"$i\" -lt 100 ]; do "
            "sleep 0.05; i=$((i + 1)); "
            "done; "
            "/usr/bin/open -n \"$app\"";
        execl("/bin/sh", "sh", "-c", script, "relaunch", pid_str, relaunch_path, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }
    return true;
}
