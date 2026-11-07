# Publishing an Electron app to the Snap Store

A reusable checklist, written while publishing PlumTerm. Steps marked ✅ are done for PlumTerm; the rest are filled in as we go.

## 0. Prerequisites
- Ubuntu machine with `snap` installed.
- A Snap Store account (https://snapcraft.io/account).
- App built with electron-builder.

## 1. Prepare the project (✅ done)
1. **package.json metadata**: `description`, `homepage`, `repository`, `author` (name + email), `license`.
2. **Linux build config** in `build.linux`:
   ```json
   "linux": {
     "target": ["snap", "dir"],
     "executableName": "myapp",
     "category": "Development;System;TerminalEmulator",
     "icon": "build/icon.png",
     "synopsis": "Short summary (<= 78 chars)",
     "description": "Longer description"
   }
   ```
   The icon must be PNG, at least 512x512, and committed.
3. **Snap config** in `build.snap`:
   ```json
   "snap": {
     "confinement": "classic",
     "grade": "stable",
     "base": "core22",
     "summary": "Short summary",
     "allowNativeWayland": true
   }
   ```
   - Use `classic` when the app must run arbitrary host programs (terminals, IDEs). It needs manual store approval (step 5). Use `strict` (the default) for ordinary apps and declare `plugs`.
4. **Native modules** (e.g. `node-pty`): add `"asarUnpack": ["**/node_modules/<module>/**"]` so the `.node` binary is outside the asar. Verify with a `dir` build:
   `npx electron-builder build --linux dir` then look for the `.node` file under `resources/app.asar.unpacked/`.
5. **Don't hardcode host paths** like `/bin/zsh`. Use `$SHELL` with fallbacks (`/bin/bash`, `/bin/sh`).
6. **Sandbox flags**: pass `--no-sandbox --disable-dev-shm-usage` on the snap `command:` line in `snapcraft.yaml`. Do NOT set them with `app.commandLine.appendSwitch` in `main.js`: that runs too late and the renderer shows a black window (log: `Creating shared memory ... failed`, `Render frame was disposed`).
7. **README**: add `sudo snap install <name> --classic`.

## 2. Install snapcraft and build locally
```bash
sudo snap install snapcraft --classic
npm run build
npx electron-builder build --linux snap --publish never
```
The result is in `dist/*.snap`.

> **Gotcha found (2026-10):** with snapcraft 9.x, electron-builder 26.8–26.15 fails: it calls the removed `snapcraft snap` command, and the `snap` target does a full snapcraft build that needs a build environment (LXD or Multipass). Fix: install LXD (`sudo snap install lxd && sudo lxd init --auto`, user in the `lxd` group) and build with a hand-written `snap/snapcraft.yaml` (dump plugin over the electron-builder `dir` output) using `snapcraft pack`, instead of the electron-builder `snap` target. This is now implemented: `snap/snapcraft.yaml` + `scripts/build-snap.sh` (run `./scripts/build-snap.sh`; output `dist/<name>_<version>_<arch>.snap`). Snapcraft's classic linter prints many ELF/rpath warnings for stage-packages; they are warnings, the build still succeeds.

## 3. Test the snap locally
```bash
sudo snap install --dangerous --classic dist/*.snap
myapp            # launch it; check the main features work
snap remove myapp
```
Check: app starts, native modules load, spawned shells and tools work.

## 4. Register the name
```bash
snapcraft login
snapcraft register <name>
```

## 5. Request classic confinement (only if using `classic`)
Post on https://forum.snapcraft.io (category: store-requests) explaining why the app needs classic. Terminal emulators and IDEs are the standard approved cases. Uploads are held until approved.

## 6. Upload
Manually:
```bash
snapcraft upload --release=stable dist/*.snap
```
Via CI: export a store credential and save it as a GitHub secret.
```bash
snapcraft export-login --snaps <name> --channels stable -
```
Secret name: `SNAPCRAFT_STORE_CREDENTIALS`. The workflow job (see `.github/workflows/release.yml`) runs `snapcore`-free: install snapcraft, build with electron-builder, `snapcraft upload`.

## 7. Store listing
Add screenshots, a long description, a category, and contact/website links at https://snapcraft.io/<name>/listing.

## 8. Release flow
Bump version, commit, tag `vX.Y.Z`, push the tag. CI builds and uploads. Users get updates automatically.

## Gotchas
- **Black window in the snap but fine outside it**: compare `/snap/<name>/current/<binary>` run directly vs `/snap/bin/<name>`. If the direct run works, the problem is flags/env, not the build.
- Always check `snap list <name>` shows a new revision after installing; a failed `sudo` install silently leaves the old build running.
- Never run the app from inside itself (the terminal app's own terminal) when testing; use another terminal.
- Use `base: core24` (noble `t64` package names) on Ubuntu 26.04 hosts; `core22` also launched but was not the fix.
- `snapcraft` needs sudo to install, so run it yourself.
- Build on the architecture you target (CI on `ubuntu-22.04` gives amd64; an ARM machine gives arm64).
- Classic snaps cannot be installed without `--classic`.
