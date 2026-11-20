# C-GUI-Template

A cross-platform, resizable C99 desktop app template using [Better-C-UI](https://github.com/mzgs/Better-C-UI).
Add your state, action handlers, and UI calls inside `main.c` (`app_state`, `on_action()`, and `frame()`). Template scaffolding lives under `src/`.

## Quick start (macOS)

```sh
./run.sh
```

The script works from any directory, configures the first build, and lets CMake
rebuild changed files before launching. When nothing changed, it runs the existing
app without recompiling. A failed build stops the script before launch.

## Project layout

```text
main.c                 App entry point (state, action handler, frame UI)
resources/             App icons (app.png, app.icns, app.ico) & OS metadata templates
src/
  chrome/              Lifecycle, CLI parsing, Settings dialog, Command Palette
  settings/            JSON persistence, atomic writes, keybinding dispatch & conflicts
  platform/            OS dark-mode detection, window positioning, macOS native menu bar
```

## Built-in features

- **Clean app entry point**: `main.c` stays minimal—wrap your UI between `app_chrome_begin_frame()` and `app_chrome_end_frame()` and handle actions in `on_action()`.
- **App icons & OS metadata**: Ships with default icons and platform packaging wired in `CMakeLists.txt`—macOS `Info.plist` + `app.icns` (Retina + automatic graphics switching), Windows `app.ico` + `VS_VERSION_INFO` executable metadata, and Linux `.desktop` entry + icon (`WM_CLASS` matched to `APP_BUNDLE_ID`). Replace `resources/app.png` and run `./resources/generate_icons.sh` on macOS to regenerate `.icns` and `.ico`.
- **Native & cross-platform menu bar**: On macOS, native **App** (**About**, **Settings…** `⌘,`), **File** (**Save**), **Edit** (**Cut**, **Copy**, **Paste**, **Select All**, **Find**), **View** (**Command Palette…**, **Enter/Exit Full Screen** `⌃⌘F`), and **Window** menus stay dynamically synced with your keybindings. On Windows and Linux, the top menu bar provides **File**, **Edit**, **View**, and **Help** (plus **Ctrl+,** for Settings and **F11** for Fullscreen).
- **Appearance preferences**: Persisted theme mode (**System**, **Light**, **Dark**), accent color, text scale (`85%`–`135%`), and **Reduce motion**.
- **Window state persistence**: Restores the last non-fullscreen window position (validated against active displays so windows never open off-screen), size, and fullscreen state across launches.
- **Keybindings with dispatch & conflict warnings**: Filter by action or key combination (case-insensitive), click a shortcut to rebind, or press Backspace to clear. Duplicate bindings and conflicts with the reserved Settings shortcut are highlighted.
- **Command Palette**: Press **⌘K** on macOS or **Ctrl+K** on Windows/Linux (rebindable in Keybindings) to search navigation, theme switching, fullscreen, and app actions.
- **Toast notifications & Settings file tools**: Import, export, reset (with confirmation), or reveal the settings folder directly from Settings. Unknown fields in imported `settings.json` files are safely ignored across versions.
- **In-app update check**: Opening **Settings → About** checks GitHub Releases (`APP_GITHUB_REPO` in `CMakeLists.txt`, or empty to disable) and shows an **Update** button when a newer version is available. On macOS and Linux it downloads, installs in place, and relaunches (falling back to opening the release page on Windows or non-writable locations).
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

Verify window creation, rendering, and clean exit:

```sh
ctest --test-dir build -C Release --output-on-failure
```

The smoke test requires a graphical desktop session and saves `build/window-smoke.png`.

## Release

Cross-platform CI (`.github/workflows/ci.yml`) runs only when a `v*` tag is pushed via `release.sh`:

```sh
./release.sh
```

This auto-increments `VERSION` in `CMakeLists.txt` (`0.1.0` → `0.1.1`), commits, tags `v<version>`, and pushes to trigger GitHub Actions builds and smoke tests for macOS, Linux, and Windows, publishing packaged archives as a GitHub Release. Pass an explicit version (e.g. `./release.sh 1.0.0`) to jump to a specific version, or `--dry-run` to preview.

### Install latest release (macOS / Linux)

Install or update directly from GitHub Releases (avoids browser `com.apple.quarantine` Gatekeeper warnings for non-notarized macOS builds):

```sh
curl -fsSL https://raw.githubusercontent.com/mzgs/C-GUI-Template/main/install.sh | sh
```

Pass a specific tag with `sh -s -- v0.1.3`, or override `REPO`, `VERSION`, and `INSTALL_DIR` via environment variables. On macOS this installs into `/Applications` (or `~/Applications` if `/Applications` is not writable); on Linux it installs the binary into `~/.local/bin` along with its `.desktop` entry and icon into `~/.local/share`. Update `REPO` at the top of `install.sh` when forking this template.

### macOS code signing (optional)

To keep a consistent signature across updates (so macOS preserves granted permissions) without committing private keys to git, store your `.p12` certificate and password once on your machine:

```text
~/.macos-codesign/macos-codesign.p12
~/.macos-codesign/password
```

When those files exist and `gh` is logged in (`gh auth login`), `./release.sh` automatically uploads `MACOS_CERT_P12` and `MACOS_CERT_PASSWORD` to the current GitHub repo on the first release—so new apps forked from this template get signed automatically with zero manual secret setup. You can also set the two secrets manually in GitHub Actions settings. Works with either a free self-signed Code Signing `.p12` or an Apple Developer ID `.p12`; set a unique `APP_BUNDLE_ID` in `CMakeLists.txt` per app.

## First-time setup (new project checklist)

When creating a new project from this template, update these places first:

1. **`CMakeLists.txt`** — set your app identity and starting version:
   - `project(<your_project> VERSION 0.1.0 LANGUAGES C)` — rename the CMake project and reset `VERSION`.
   - `APP_NAME` — display name for the window title, macOS `.app` bundle, and per-user settings folder.
   - `APP_DESCRIPTION`, `APP_AUTHOR`, `APP_COPYRIGHT` — metadata used in **Settings → About**, macOS `Info.plist`, Windows `VS_VERSION_INFO`, and the Linux `.desktop` entry.
   - `APP_BUNDLE_ID` — unique reverse-DNS identifier (e.g. `com.yourname.my-app`) used for macOS signing/permissions and Linux desktop/icon matching.
   - `APP_GITHUB_REPO` — your `"owner/repo"` slug for **Settings → About** update checks (or `""` to disable).
   - *(Optional)* Rename the `empty_gui` target in `CMakeLists.txt` if you want a custom Linux/Windows executable name (keep `cui_app_target(...)` and `cui::cui`).
2. **App icon (`resources/`)** — replace `resources/app.png` (1024×1024 PNG) with your own icon, then run `./resources/generate_icons.sh` on macOS (or replace `resources/app.icns` and `resources/app.ico` directly).
3. **`install.sh` & `README.md`**:
   - In `install.sh`, change `REPO="${REPO:-mzgs/C-GUI-Template}"` (and the usage comments) to your `"owner/repo"`.
   - In `README.md`, update the title, description, and `install.sh` `curl` URL to your repository.
4. **Verify build & first release**:
   - Build and run locally (`./run.sh` on macOS, or `cmake -S . -B build && cmake --build build`) and run `ctest --test-dir build --output-on-failure`.
   - Ensure GitHub Actions is enabled for the new repo (and optionally configure `~/.macos-codesign/` + `gh auth login` for macOS code signing), then run `./release.sh 0.1.0` when ready for your first release.
