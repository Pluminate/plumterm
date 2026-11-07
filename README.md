# PlumTerm

![PlumTerm](PlumTerm.png)

A modern, tabbed terminal application built with Qt 6 and Rust.

## Features
- **Tabbed Interface:** Multiple terminal sessions in one window.
- **Split Panes:** Vertical and horizontal splits with persistent resizing.
- **Renamable Tabs:** Double-click any tab to give it a custom name.
- **Global Hotkey:** Toggle app visibility from anywhere (default: `Cmd+Shift+T`).
- **Rerun Shortcut:** Quickly rerun the last command with `Cmd+R` (customizable).
- **macOS Optimized:** `Cmd+Q` quits the app, `Cmd+W` closes the window. Closing the last window also quits the app.
- **Git Integration:** Status bar shows current branch, dirty files, and PR info.
- **Change Count:** See the number of changed files at a glance directly in the status bar.
- **Open in GitHub Desktop:** Launch the current repo in GitHub Desktop with one click.
- **Open in IntelliJ:** Open the current project directly in IntelliJ IDEA from the terminal.
- **Open in Finder:** Reveal the current working directory in Finder instantly.
- **AI Commands:** Run AI-powered commands from within the terminal to assist with your workflow.
- **Background Commands:** Run long-running commands in the background and get notified when they finish.

## Installation

Prebuilt packages are not published yet, so PlumTerm is currently installed from source.

### macOS

```bash
brew install qt cmake ninja
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh   # if you don't have Rust yet
./install-local.sh
```

This builds the app and installs `PlumTerm.app` into `/Applications`. To install somewhere else, pass the folder: `./install-local.sh ~/Applications`.

### Windows

Install the build tools from PowerShell. Accept the admin prompts, and expect the Visual Studio step to take a while.

```powershell
winget install --id Rustlang.Rustup --exact
winget install --id Kitware.CMake --exact
winget install --id Ninja-build.Ninja --exact
winget install --id miurahr.aqtinstall --exact
```

Then open a new terminal for the C++ tools and Qt, using the block that matches your PC. Run `$env:PROCESSOR_ARCHITECTURE` if you're not sure: `ARM64` or `AMD64`.

```powershell
# Windows on ARM (ARM64)
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.ARM64 --add Microsoft.VisualStudio.Component.VC.Redist.14.Latest --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
aqt install-qt windows_arm64 desktop 6.10.3 win64_msvc2022_arm64 --outputdir C:\Qt --archives qtbase qtsvg

# Intel or AMD (AMD64)
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.Redist.14.Latest --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 --outputdir C:\Qt --archives qtbase qtsvg
```

Then build and install:

```powershell
powershell -ExecutionPolicy Bypass -File .\install-local.ps1
```

From Git Bash, `./install-local.sh` does the same thing. No admin rights are needed for this step. It installs to `%LOCALAPPDATA%\Programs\PlumTerm` and adds a Start Menu shortcut, a `plumterm` command and an "Open in PlumTerm" folder menu. Options: pass a folder to install somewhere else, `-QtDir` to use another Qt, `-NoLaunch` to skip starting the app, and `-Uninstall` to remove it all.

To skip the build tools, use a finished build from the [Windows build workflow](../../actions/workflows/windows-build.yml). Download the `PlumTerm-windows-arm64` or `PlumTerm-windows-x64` artifact, unzip it, and run this from inside the folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\install-local.ps1 -BundleDir .\app
```

Builds aren't code-signed yet, so Windows SmartScreen may warn when they start.

#### Windows shortcuts

Windows keeps `Ctrl+C`, `Ctrl+R`, `Ctrl+W` and the other control keys for the shell, so app shortcuts use `Ctrl+Shift`:

| Action | macOS | Windows |
|---|---|---|
| New tab / new window | `Cmd+T` / `Cmd+N` | `Ctrl+Shift+T` / `Ctrl+Shift+N` |
| Close pane / close tab | `Cmd+W` / `Cmd+Shift+W` | `Ctrl+Shift+W` / `Ctrl+Alt+Shift+W` |
| Copy / paste | `Cmd+C` / `Cmd+V` | `Ctrl+Shift+C` / `Ctrl+Shift+V` |
| Find | `Cmd+F` | `Ctrl+Shift+F` |
| Rerun the last command | `Cmd+R` | `Ctrl+Shift+E` |
| Switch to tab 1-9 | `Cmd+1`-`9` | `Alt+1`-`9` |
| Full screen | `Ctrl+Cmd+F` | `F11` |

`Ctrl+C` also copies when text is selected, and otherwise interrupts the running command. `Ctrl+V` pastes. The shell is PowerShell: `pwsh` if installed, otherwise Windows PowerShell. The global hotkey is macOS only.

## Development

### Prerequisites
- Qt 6 (Widgets, Concurrent, Test, Svg)
- CMake and Ninja
- Rust (cargo)

### Build, test and run
```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
ctest --test-dir build
./build/plumterm
```

On Windows, run the same commands from a Developer PowerShell, with `-DCMAKE_PREFIX_PATH` set to your Qt folder (for example `C:\Qt\6.10.3\msvc2022_arm64`) and that folder's `bin` on `PATH` so the tests and app can find the Qt DLLs.

See [ARCHITECTURE.md](ARCHITECTURE.md) for how the code is organised.

## License

PlumTerm is **source-available**, not open source.

- **Source code** (this repository): [PolyForm Strict License 1.0.0](LICENSE). You may read the code and use it for noncommercial purposes, but you may not modify it, redistribute it, or use it commercially.
- **Compiled app** (official releases): free to use under the [End-User Terms](EULA.md).

Copyright (c) 2024-2026 Pluminate. "PlumTerm" and "Pluminate" are names of the publisher; this license does not grant any right to use them.
