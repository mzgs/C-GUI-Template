# C-GUI-Template

An empty, resizable C99 GUI app using [Better-C-UI](https://github.com/mzgs/Better-C-UI).
Add your UI calls inside `frame()` in `main.c`.

On macOS, build and run with:

```sh
./run.sh
```

The script works from any directory, configures the first build, and lets CMake
rebuild changed files before launching. When nothing changed, it runs the existing
app without recompiling. A failed build stops the script before launch.

On macOS, choose **C GUI Template → Settings…** or press **⌘,** to open
the Settings dialog. On its Keybindings page, filter by action or key combination
(case-insensitive), then click a shortcut and press a key
combination to change it, or press Backspace to clear it. Example bindings are
for demonstration only. General settings and keybinding changes autosave to
`~/Library/Application Support/<bundle name>/settings.json` and load at startup.
The folder name comes from `MACOSX_BUNDLE_BUNDLE_NAME` in `CMakeLists.txt`.

Requires Git, CMake 3.14+, and a C compiler with the system SDK. On macOS,
install Xcode Command Line Tools and CMake. Linux also needs X11 development
headers (`libx11-dev` on Debian/Ubuntu). Windows supports Visual Studio or MinGW.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The first configure downloads Better-C-UI at a pinned commit and needs internet
access. Its examples, tests, and optional add-ons are excluded by default.

Run on macOS:

```sh
open "build/C GUI Template.app"
```

Run on Linux: `./build/empty_gui`. With Visual Studio on Windows:
`build\Release\empty_gui.exe` (MinGW: `build\empty_gui.exe`).

Verify window creation, rendering, and clean exit:

```sh
ctest --test-dir build -C Release --output-on-failure
```

This smoke test requires a graphical desktop session and saves `build/window-smoke.png`.
