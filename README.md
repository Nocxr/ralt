# rAlt

A minimal, Windows-only application switcher inspired by macOS [rcmd](https://lowtechguys.com/rcmd/).

Hold **Right Alt**, then press the first letter of an application's name. Press the same letter again to cycle through matching windows. Release Right Alt or press Escape to dismiss the overlay.

## Run

```powershell
py -m venv .venv
.venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python main.py
```

The tray icon can open `ralt_config.json`, where executable display names and custom letters can be assigned. Restart rAlt after editing the file.

Some systems require running rAlt as administrator for global key suppression or for switching to elevated applications.
