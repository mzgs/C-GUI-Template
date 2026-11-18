#include "platform.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#if __has_include(<X11/Xlib.h>) && __has_include(<X11/Xatom.h>)
#define APP_HAVE_X11 1
#include <X11/Xatom.h>
#include <X11/Xlib.h>
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
