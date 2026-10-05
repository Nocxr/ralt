# rAlt

Windows application switching with Right Alt plus a letter, plus Right Alt + / to toggle back to the previously focused app/window, using the current engine extracted from Dashboard.

The standalone executable is a tray app. Right-click its tray icon to enable/disable switching, edit or reload ralt_config.json, or exit. The engine retains Dashboard's current matching, grouping, recent-window cycling, overlay and configuration behavior. The previous Python implementation remains in Git history.

Build with `make`, launch with `make run`, and register the `ralt` command with `make install`. See [BUILDING.md](BUILDING.md) for the shared commands.

## Dashboard integration

`RAltCore` / `rAlt::Core` is a standalone C++ library with no Dashboard or ImGui dependency. Dashboard pins this repo as `third_party/ralt`, links the core into its RAlt module DLL, and owns its settings/plugin adapter. Set `RALT_BUILD_STANDALONE=OFF` when embedding.

Use either Dashboard's enabled rAlt module or the standalone app as the active hook host to avoid processing the same shortcut twice. The standalone build keeps its config beside its executable, matching the current engine's config-location behavior.
