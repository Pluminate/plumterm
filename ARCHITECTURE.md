# PlumTerm: architecture

Native terminal for macOS and Windows: a Rust core (`core/`) behind a C ABI plus a Qt 6 Widgets shell (`src/`).
Replaces the earlier Electron/React app, which has been removed.
Modeled on ~/Work/pluminate. Plan: see the approved migration plan.

```
 Qt Widgets shell (C++20)   src/                    MainWindow, TerminalScreen, panes, dialogs
   |  CoreBridge.h                                  only place that calls the C ABI
 C ABI                      core/plumterm_core.h
   |
 Rust core (staticlib)      core/src/               pty.rs (portable-pty), later git/ai/tasks/settings/session/mobile
```

- Core never references Qt. Callbacks arrive on reader threads; `CoreBridge` re-posts to the GUI thread.
- `TerminalScreen.{h,cpp}` and `pty.rs` were copied from pluminate (`pluminate-free/modules/terminal`), symbols renamed `pl_` -> `tp_`.
- Settings/session JSON schemas stay compatible with the Electron app.

## Build / test
```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build build
ctest --test-dir build      # cargo tests + headless smoke (QT_QPA_PLATFORM=offscreen)
./build/plumterm
```

## Working agreements
- Work on `main`; do not commit or push without asking the user. No AI attribution in commits.
- Respect the layering: only `CoreBridge.h` includes `plumterm_core.h`.
