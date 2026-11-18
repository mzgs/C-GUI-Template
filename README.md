# C-GUI-Template

A cross-platform, resizable C99 desktop app template using [Better-C-UI](https://github.com/mzgs/Better-C-UI).
Add your UI calls inside `frame()` in `main.c`.

## Quick start (macOS)

```sh
./run.sh
```

The script works from any directory, configures the first build, and lets CMake
rebuild changed files before launching. When nothing changed, it runs the existing
app without recompiling. A failed build stops the script before launch.

## Built-in features

- **Settings dialog**: On macOS, choose **C GUI Template → Settings…** (`⌘,`) or **About C GUI Template**. On Windows and Linux, use the top menu bar (**File → Settings…**, **Help → About**) or press **Ctrl+,**.
- **Appearance preferences**: Persisted theme mode (**System**, **Light**, **Dark**), accent color, text scale (`85%`–`135%`), and **Reduce motion**.
- **Window state persistence**: Restores the last non-fullscreen window position (validated against active displays so windows never open off-screen), size, and fullscreen state across launches.
- **Keybindings with dispatch & conflict warnings**: Filter by action or key combination (case-insensitive), click a shortcut to rebind, or press Backspace to clear. Duplicate bindings and conflicts with the reserved Settings shortcut are highlighted. Use `app_action_pressed()` to trigger actions from `frame()`.
- **Command Palette**: Press **⌘K** on macOS or **Ctrl+K** on Windows/Linux (rebindable in Keybindings) to search navigation, theme switching, fullscreen, and app actions.
- **Toast notifications & Settings file tools**: Import, export, reset (with confirmation), or reveal the settings folder directly from Settings or About.
- **CLI flags**: `--help` (`-h`), `--version` (`-v`), `--settings <path>`, and `--software` (force software renderer).

Settings, appearance, window state, and keybinding changes autosave to:

- macOS — `~/Library/Application Support/<app name>/settings.json`
- Windows — `%LOCALAPPDATA%\<app name>\settings.json`
- Linux — `$XDG_CONFIG_HOME/<app name>/settings.json`, defaulting to `~/.config/<app name>/settings.json`

Set `APP_NAME` in `CMakeLists.txt` or pass `-DAPP_NAME="My App"` when configuring
to name a new app and give it its own settings folder.

## Build and test

Requires Git, CMake 3.14+, and a C compiler with the system SDK. On macOS,
install Xcode Command Line Tools and CMake. Linux also needs X11 development
headers (`libx11-dev` on Debian/Ubuntu). Windows supports Visual Studio or MinGW.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The first configure downloads pinned versions of Better-C-UI and yyjson and needs internet
access. Its examples, tests, and optional add-ons are excluded by default.

Run on macOS:

```sh
open "build/C GUI Template.app"
```

Run on Linux: `./build/empty_gui`. With Visual Studio on Windows:
`build\Release\empty_gui.exe` (MinGW: `build\empty_gui.exe`).

Verify window creation, settings, keybindings, command palette, CLI parsing, and clean exit:

```sh
ctest --test-dir build -C Release --output-on-failure
```

The smoke test requires a graphical desktop session and saves `build/window-smoke.png`.
