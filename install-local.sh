#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
APP_NAME="PlumTerm.app"
DEST_DIR="${1:-/Applications}"

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
