# TerminalPro

![TerminalPro](TerminalPro.png)

A modern, tabbed terminal application built with Electron, React, and node-pty.

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

## Installation (Linux)

```bash
sudo snap install terminalpro --classic
```

Classic confinement is required so the terminal can run your shell and tools on the host.

## Installation (macOS)

### From Release
1. Download the latest `.dmg` or `.zip` from the [Releases](https://github.com/Pluminate/plumterm/releases) page.
2. Open the downloaded folder/volume.
3. **Double-click `install.sh`** (or run `bash install.sh` in your terminal).

### From Source
```bash
npm run install-app
```

### 🚀 What the `install.sh` does:
*   Moves the app to your `/Applications` folder automatically.
*   Fixes the "App is damaged" error by setting it as a trusted application.
*   Opens the app for you immediately.

---

### Manual Fix (Alternative)
If you prefer to drag-and-drop manually and see the "App is damaged" error, run this in your terminal:

```bash
xattr -cr /Applications/TerminalPro.app
```

## Development

### Prerequisites
- Node.js (v18+)
- npm

### Setup
```bash
# Install dependencies
npm install

# Start development server (hot reload)
npm start

# Build the frontend
npm run build
```

### Development Commands
- `npm start` - Start dev server with Electron in watch mode
- `npm run dev` - Alternative dev command with concurrent build and server
- `npm run build` - Build frontend assets

## Creating a Release
To create a new versioned release and upload artifacts to GitHub:

```bash
# Auto-increment patch (1.0.x -> 1.0.x+1)
./release.sh patch "Your release notes here"

# Auto-increment minor (1.x.0 -> 1.x+1.0)
./release.sh minor "New feature release"

# Or use a specific version
./release.sh 1.1.0 "Major stable version"
```

This will:
1. Update `package.json` version.
2. Update `CHANGELOG.md`.
3. Create a Git tag and push to GitHub.
4. Trigger GitHub Actions to build and upload the DMG/ZIP.

## License

Copyright (c) 2024-2026 Pluminate. All rights reserved.

TerminalPro is proprietary software, free to use but not open source. See the [LICENSE](LICENSE) file for terms.
