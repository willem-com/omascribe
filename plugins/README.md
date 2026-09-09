# Omascribe plugins

Plugins are per-install, per-person buttons. The general source stays general;
the plugins live outside the repository, in the owner's config directory, and
carry whatever addresses, hosts and credentials that person needs.

```
~/.config/omascribe/plugins/<id>/plugin.json     one directory per plugin
~/.config/omascribe/plugins/<id>/...             its script and private files
```

Each plugin appears as a chip at the top right of the window, left of Export,
hidden in fullscreen (zen). Clicking the chip saves the note, exports it in the
format the plugin asks for, and runs the plugin's program with that file. The
program's result becomes a toast in the window.

## plugin.json

```json
{
  "name": "Send to notes",
  "hint": "Export as PDF and mail it to my notes address",
  "exec": "send-note.py",
  "input": "pdf",
  "success": "Sent"
}
```

| key | meaning |
|---|---|
| `name` | chip label. Required. |
| `hint` | tooltip. Optional. |
| `exec` | program to run: a path relative to the plugin directory, or absolute. Must be executable. Required. |
| `input` | what Omascribe hands over: `pdf` (default), `svg`, `png`, `json` (the raw `.omascribe` note) or `none`. |
| `success` | toast shown on exit code 0 when the program prints nothing. Optional. |

## The contract

The program runs with the plugin directory as working directory, the exported
file as its first argument, and these environment variables:

| variable | value |
|---|---|
| `OMASCRIBE_FILE` | path of the exported file (a temporary copy; removed afterwards) |
| `OMASCRIBE_INPUT` | `pdf`, `svg`, `png`, `json` or `none` |
| `OMASCRIBE_TITLE` | note title, may be empty |
| `OMASCRIBE_NOTE_ID` | note id |
| `OMASCRIBE_NOTE_PATH` | path of the live `.omascribe` file |
| `OMASCRIBE_CREATED` | creation time, ISO 8601, local time |
| `OMASCRIBE_MODIFIED` | last change, ISO 8601, local time |
| `OMASCRIBE_STROKES` | number of ink strokes |
| `OMASCRIBE_TEXTS` | number of typed text blocks |
| `OMASCRIBE_TYPED_TEXT` | the typed text, blocks separated by blank lines |
| `OMASCRIBE_HOST` | hostname of the machine running Omascribe |
| `OMASCRIBE_VERSION` | Omascribe version |
| `OMASCRIBE_PLUGIN_DIR` | the plugin's own directory |

Exit code 0 means success: the last non-empty line the program printed on
stdout is the toast, or `success` from `plugin.json`, or "`name`: done". Any
other exit code is a failure: the toast shows the last line of stderr. One
plugin runs at a time; the chip shows an ellipsis while it runs.

Keep secrets in a file inside the plugin directory with mode 600 and read it
from the script. Nothing under `~/.config/omascribe/plugins/` is part of this
repository or of any export.

## Example

`examples/mail-note/` mails the note as a PDF attachment over authenticated
SMTP, with the note title as subject and a short summary as body. Copy the
directory to `~/.config/omascribe/plugins/mail-note/`, copy `secrets.env.example`
to `secrets.env`, fill it in, `chmod 600 secrets.env`, and restart Omascribe.
