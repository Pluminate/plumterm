#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
APP_NAME="PlumTerm.app"
DEST_DIR="${1:-/Applications}"

OS="$(uname -s)"
case "$OS" in
  Darwin) ;;
  Linux)
    # Build and install the binary under ~/.local (or $1 as prefix).
    PREFIX="${1:-$HOME/.local}"
    for tool in cmake ninja cargo; do
      command -v "$tool" >/dev/null || {
        echo "error: $tool not found. On Debian/Ubuntu: sudo apt install cmake ninja-build qt6-base-dev qt6-svg-dev libgl1-mesa-dev (and rustup for cargo)" >&2
        exit 1
      }
    done
    echo "==> Configuring & Building PlumTerm..."
    cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --target plumterm
    mkdir -p "$PREFIX/bin"
    install -m 755 "$BUILD_DIR/plumterm" "$PREFIX/bin/plumterm"
    echo "==> Successfully installed to $PREFIX/bin/plumterm"
    # Register in the desktop app launcher.
    DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
    mkdir -p "$DATA/applications" "$DATA/icons/hicolor/512x512/apps"
    install -m 644 "$ROOT/PlumTerm.png" "$DATA/icons/hicolor/512x512/apps/plumterm.png"
    cat > "$DATA/applications/plumterm.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=PlumTerm
Comment=Terminal emulator
Exec=$PREFIX/bin/plumterm
Icon=plumterm
Terminal=false
Categories=System;TerminalEmulator;
StartupWMClass=plumterm
DESKTOP
    command -v update-desktop-database >/dev/null && update-desktop-database "$DATA/applications" 2>/dev/null || true
    command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -qf "$DATA/icons/hicolor" 2>/dev/null || true
    echo "==> Registered launcher: $DATA/applications/plumterm.desktop"
    case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "note: $PREFIX/bin is not in your PATH" >&2 ;; esac
    exit 0
    ;;
  MINGW*|MSYS*|CYGWIN*)
    # Windows via Git Bash/MSYS2. Set QT_DIR to your Qt prefix (e.g. C:/Qt/6.7.0/msvc2019_64).
    DEST="${1:-${LOCALAPPDATA:-$HOME/AppData/Local}/PlumTerm}"
    for tool in cmake ninja cargo; do
      command -v "$tool" >/dev/null || { echo "error: $tool not found in PATH (run from a VS Developer shell)." >&2; exit 1; }
    done
    [ -n "${QT_DIR:-}" ] || { echo "error: set QT_DIR to your Qt 6 install prefix." >&2; exit 1; }
    echo "==> Configuring & Building PlumTerm..."
    cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR"
    cmake --build "$BUILD_DIR" --target plumterm
    BIN="$BUILD_DIR/plumterm.exe"
    [ -f "$BIN" ] || { echo "error: $BIN not found." >&2; exit 1; }
    mkdir -p "$DEST"
    cp "$BIN" "$DEST/plumterm.exe"
    echo "==> Bundling Qt libraries with windeployqt..."
    "$QT_DIR/bin/windeployqt.exe" "$DEST/plumterm.exe" >/dev/null 2>&1 || echo "warning: windeployqt failed; DLLs not bundled." >&2
    echo "==> Successfully installed to $DEST/plumterm.exe"
    exit 0
    ;;
  *) echo "error: unsupported OS: $OS" >&2; exit 1 ;;
esac

echo "==> Configuring & Building PlumTerm..."
QT_DIR="$(brew --prefix qt)"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR"
cmake --build "$BUILD_DIR" --target plumterm

BIN="$BUILD_DIR/plumterm"
[ -x "$BIN" ] || { echo "error: $BIN not found." >&2; exit 1; }

MACDEPLOYQT="$QT_DIR/bin/macdeployqt"
[ -x "$MACDEPLOYQT" ] || MACDEPLOYQT="$(command -v macdeployqt || true)"

STAGE="$BUILD_DIR/bundle"
APP="$STAGE/$APP_NAME"
echo "==> Assembling $APP_NAME..."
rm -rf "$STAGE"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/plumterm"

if [ -f "$ROOT/packaging/icon.icns" ]; then
  cp "$ROOT/packaging/icon.icns" "$APP/Contents/Resources/icon.icns"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>PlumTerm</string>
  <key>CFBundleDisplayName</key><string>PlumTerm</string>
  <key>CFBundleIdentifier</key><string>com.pluminate.plumterm</string>
  <key>CFBundleExecutable</key><string>plumterm</string>
  <key>CFBundleIconFile</key><string>icon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.1.0</string>
  <key>CFBundleVersion</key><string>1.1.0</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
  <key>NSServices</key><array>
    <dict>
      <key>NSMenuItem</key><dict><key>default</key><string>Open in PlumTerm</string></dict>
      <key>NSMessage</key><string>openFile</string>
      <key>NSSendFileTypes</key><array><string>public.folder</string></array>
    </dict>
  </array>
</dict></plist>
PLIST

echo "==> Bundling Qt libraries with macdeployqt..."
"$MACDEPLOYQT" "$APP" -always-overwrite >/dev/null 2>&1 || true

for rp in $(otool -l "$APP/Contents/MacOS/plumterm" | awk '/LC_RPATH/{f=1} f&&/path /{print $2; f=0}' | grep -v '^@'); do
  install_name_tool -delete_rpath "$rp" "$APP/Contents/MacOS/plumterm" 2>/dev/null || true
done

echo "==> Signing ad-hoc..."
codesign --force --deep --sign - "$APP"

echo "==> Installing to $DEST_DIR..."
if [ -d "$DEST_DIR/$APP_NAME" ]; then
  rm -rf "$DEST_DIR/$APP_NAME" 2>/dev/null || mv "$DEST_DIR/$APP_NAME" "$DEST_DIR/$APP_NAME.bak.$$" 2>/dev/null || true
fi
cp -R "$APP" "$DEST_DIR/$APP_NAME"
xattr -cr "$DEST_DIR/$APP_NAME" 2>/dev/null || true

echo "==> Successfully installed to $DEST_DIR/$APP_NAME"
