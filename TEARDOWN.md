# Teardown for 2026-09-06-omascribe

Bench root: `~/bench/2026-09-06-omascribe/`

## Outside the bench root (created when installed for daily use)

- `~/.local/bin/omascribe` (symlink or copy of `build/omascribe`)
- `~/.local/share/applications/omascribe.desktop`
- `~/.local/share/icons/hicolor/scalable/apps/omascribe.svg`
- `~/.local/share/omascribe/` (notes, `trash/`, plus `current.json` / `current.png` / `current.svg` / `current.omascribe`)
- `~/.local/share/willem.com/omascribe/` (first-run notes, before the path settled)
- `~/.config/omascribe/` or `~/.config/willem.com/omascribe.conf` (window geometry)

These are the app, not job residue. Keep them if the pen notes stay on this glass.
Remove only when retiring the app:

```
rm -f ~/.local/bin/omascribe
rm -f ~/.local/share/applications/omascribe.desktop
rm -f ~/.local/share/icons/hicolor/scalable/apps/omascribe.svg
rm -rf ~/.local/share/omascribe
rm -rf ~/.config/willem.com
```

## Borrowed

- `omawrite-ref/` is a shallow clone of https://github.com/omacom-io/omawrite for the chrome and theme. It is inside the bench root.

## Packages

None extra installed for this job. Uses `qt6-base`, `qt6-declarative`, `qt6-svg`,
`qt6-5compat` already on the machine.

Do not run this teardown for a pause. The bench and the install stay.
