#!/bin/bash
# Assemble a self-contained, ad-hoc-signed PlumTerm.app from an existing build.
# Usage: packaging/macos-bundle.sh <build-dir> <out-dir> [version]
# Prints nothing on success except progress; the app lands at <out-dir>/PlumTerm.app.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$1"
OUT_DIR="$2"
VERSION="${3:-1.1.0}"
APP="$OUT_DIR/PlumTerm.app"

BIN="$BUILD_DIR/plumterm"
[ -x "$BIN" ] || { echo "error: $BIN not found." >&2; exit 1; }

QT_DIR="${QT_DIR:-$(brew --prefix qt 2>/dev/null || true)}"
MACDEPLOYQT="$QT_DIR/bin/macdeployqt"
[ -x "$MACDEPLOYQT" ] || MACDEPLOYQT="$(command -v macdeployqt || true)"
[ -n "$MACDEPLOYQT" ] || { echo "error: macdeployqt not found (set QT_DIR)." >&2; exit 1; }

echo "==> Assembling PlumTerm.app ($VERSION)..."
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/plumterm"
cp "$ROOT/packaging/icon.icns" "$APP/Contents/Resources/icon.icns"

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
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
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
"$MACDEPLOYQT" "$APP" -always-overwrite >/dev/null 2>&1 || echo "warning: macdeployqt reported errors." >&2

for rp in $(otool -l "$APP/Contents/MacOS/plumterm" | awk '/LC_RPATH/{f=1} f&&/path /{print $2; f=0}' | grep -v '^@'); do
  install_name_tool -delete_rpath "$rp" "$APP/Contents/MacOS/plumterm" 2>/dev/null || true
done

echo "==> Signing ad-hoc..."
codesign --force --deep --sign - "$APP"
