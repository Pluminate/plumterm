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
    if ! command -v cmake >/dev/null || ! command -v cl >/dev/null || ! command -v ninja >/dev/null; then
      VSWHERE="$(cygpath -u "${ProgramFiles:-C:/Program Files (x86)}")/Microsoft Visual Studio/Installer/vswhere.exe"
      [ -x "$VSWHERE" ] || VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
      if [ -x "$VSWHERE" ]; then
        VS_DIR="$("$VSWHERE" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | tr -d '\r' | head -n1)"
        [ -n "$VS_DIR" ] || VS_DIR="$("$VSWHERE" -latest -products '*' -property installationPath | tr -d '\r' | head -n1)"
        if [ -n "$VS_DIR" ]; then
          VS_DIR_U="$(cygpath -u "$VS_DIR")"
          VCVARS_DIR=""
          VCVARS_BAT=""
          if [ -f "$VS_DIR_U/VC/Auxiliary/Build/vcvars64.bat" ]; then
            VCVARS_DIR="$VS_DIR_U/VC/Auxiliary/Build"
            VCVARS_BAT="vcvars64.bat"
          elif [ -f "$VS_DIR_U/Common7/Tools/VsDevCmd.bat" ]; then
            VCVARS_DIR="$VS_DIR_U/Common7/Tools"
            VCVARS_BAT="VsDevCmd.bat -arch=amd64 -host_arch=amd64"
          fi
          if [ -n "$VCVARS_BAT" ]; then
            while IFS='=' read -r key val; do
              case "$key" in
                PATH|Path) PATH="$(cygpath -u -p "$val"):$PATH"; export PATH ;;
                INCLUDE|LIB|LIBPATH|VCToolsInstallDir|VCINSTALLDIR|VSINSTALLDIR|WindowsSdkDir|WindowsSDKVersion)
                  export "$key=$val" ;;
              esac
            done < <(cd "$VCVARS_DIR" && MSYS_NO_PATHCONV=1 cmd.exe /c "$VCVARS_BAT >nul 2>&1 && set" </dev/null | tr -d '\r')
          fi
          for extra in \
            "$VS_DIR_U/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin" \
            "$VS_DIR_U/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"; do
            [ -d "$extra" ] && PATH="$extra:$PATH"
          done
          export PATH
        fi
      fi
    fi
    for tool in cmake ninja cargo; do
      command -v "$tool" >/dev/null || { echo "error: $tool not found in PATH (run from a VS Developer shell)." >&2; exit 1; }
    done
    if [ -z "${QT_DIR:-}" ]; then
      for d in /c/Qt/*/msvc*_64; do
        [ -d "$d" ] && QT_DIR="$(cygpath -m "$d")"
      done
    fi
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
    DEST_WIN="$(cygpath -w "$DEST/plumterm.exe")"
    WORKDIR_WIN="$(cygpath -w "$DEST")"
    ICO_WIN="$(cygpath -w "$DEST/plumterm.ico")"
    PKG_WIN="$(cygpath -w "$ROOT/packaging")"
    SHORTCUT_WIN="$(cygpath -w "${APPDATA:-$HOME/AppData/Roaming}/Microsoft/Windows/Start Menu/Programs/PlumTerm.lnk")"
    powershell.exe -NoProfile -Command "
      \$sizes = @(48, 64, 128, 256);
      \$pngs = foreach (\$sz in \$sizes) {
        \$p = Join-Path '$PKG_WIN' \"plumterm-\$sz.png\";
        if (Test-Path \$p) { ,@(\$sz, [IO.File]::ReadAllBytes(\$p)) }
      };
      if (\$pngs.Count -gt 0) {
        \$fs = [IO.File]::Create('$ICO_WIN');
        \$bw = New-Object IO.BinaryWriter(\$fs);
        \$bw.Write([uint16]0); \$bw.Write([uint16]1); \$bw.Write([uint16]\$pngs.Count);
        \$offset = 6 + 16 * \$pngs.Count;
        foreach (\$item in \$pngs) {
          \$sz = \$item[0]; \$bytes = \$item[1];
          \$dim = if (\$sz -ge 256) { [byte]0 } else { [byte]\$sz };
          \$bw.Write(\$dim); \$bw.Write(\$dim); \$bw.Write([byte]0); \$bw.Write([byte]0);
          \$bw.Write([uint16]1); \$bw.Write([uint16]32);
          \$bw.Write([uint32]\$bytes.Length); \$bw.Write([uint32]\$offset);
          \$offset += \$bytes.Length;
        }
        foreach (\$item in \$pngs) { \$bw.Write(\$item[1]) }
        \$bw.Close(); \$fs.Close();
      }
      \$s = (New-Object -ComObject WScript.Shell).CreateShortcut('$SHORTCUT_WIN');
      \$s.TargetPath = '$DEST_WIN';
      \$s.WorkingDirectory = '$WORKDIR_WIN';
      if (Test-Path '$ICO_WIN') { \$s.IconLocation = '$ICO_WIN,0' }
      \$s.Save()
    " >/dev/null 2>&1 \
      && echo "==> Registered Start Menu shortcut: $SHORTCUT_WIN" \
      || echo "warning: could not create Start Menu shortcut." >&2
    echo "==> Successfully installed to $DEST/plumterm.exe"
    exit 0
    ;;
  *) echo "error: unsupported OS: $OS" >&2; exit 1 ;;
esac

echo "==> Configuring & Building PlumTerm..."
QT_DIR="$(brew --prefix qt)"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR"
cmake --build "$BUILD_DIR" --target plumterm

STAGE="$BUILD_DIR/bundle"
APP="$STAGE/$APP_NAME"
mkdir -p "$STAGE"
QT_DIR="$QT_DIR" "$ROOT/packaging/macos-bundle.sh" "$BUILD_DIR" "$STAGE"

echo "==> Installing to $DEST_DIR..."
if [ -d "$DEST_DIR/$APP_NAME" ]; then
  rm -rf "$DEST_DIR/$APP_NAME" 2>/dev/null || mv "$DEST_DIR/$APP_NAME" "$DEST_DIR/$APP_NAME.bak.$$" 2>/dev/null || true
fi
cp -R "$APP" "$DEST_DIR/$APP_NAME"
xattr -cr "$DEST_DIR/$APP_NAME" 2>/dev/null || true

echo "==> Successfully installed to $DEST_DIR/$APP_NAME"
