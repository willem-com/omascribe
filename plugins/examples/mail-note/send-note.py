#!/usr/bin/env python3
"""Omascribe plugin: mail the exported note as an attachment.

Subject is the note title. The body is a short summary. Credentials come from
secrets.env next to this script (mode 600), never from the source tree.
Prints the toast text on success; exits non-zero with a reason on stderr.
"""
import os
import smtplib
import ssl
import sys
from datetime import datetime
from email.message import EmailMessage
from email.utils import formatdate, make_msgid
from pathlib import Path


def load_env(path: Path) -> dict:
    values = {}
    if not path.exists():
        sys.exit(f"{path.name} missing next to the script; copy secrets.env.example")
    if path.stat().st_mode & 0o077:
        sys.exit(f"{path.name} must be mode 600")
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
            value = value[1:-1]
        values[key.strip()] = value
    return values


def pretty(iso: str) -> str:
    try:
        return datetime.fromisoformat(iso).strftime("%-d %B %Y, %H:%M")
    except ValueError:
        return iso or "unknown"


def main() -> None:
    here = Path(os.environ.get("OMASCRIBE_PLUGIN_DIR", Path(__file__).resolve().parent))
    env = load_env(here / "secrets.env")
    for key in ("SMTP_HOST", "SMTP_USER", "SMTP_PASS", "MAIL_FROM", "MAIL_TO"):
        if not env.get(key):
            sys.exit(f"{key} missing in secrets.env")

    file = Path(sys.argv[1] if len(sys.argv) > 1 else os.environ.get("OMASCRIBE_FILE", ""))
    if not file.is_file():
        sys.exit("no exported file to send")

    title = os.environ.get("OMASCRIBE_TITLE", "").strip() or "Note"
    created = pretty(os.environ.get("OMASCRIBE_CREATED", ""))
    modified = pretty(os.environ.get("OMASCRIBE_MODIFIED", ""))
    strokes = os.environ.get("OMASCRIBE_STROKES", "0")
    texts = os.environ.get("OMASCRIBE_TEXTS", "0")
    typed = os.environ.get("OMASCRIBE_TYPED_TEXT", "").strip()
    host = os.environ.get("OMASCRIBE_HOST", "unknown host")
    version = os.environ.get("OMASCRIBE_VERSION", "")

    lines = [
        title,
        f"Created on {created}, last change {modified}.",
        f"{strokes} pen strokes, {texts} typed text block(s).",
    ]
    if typed:
        excerpt = typed if len(typed) <= 600 else typed[:600].rstrip() + " ..."
        lines += ["", "Typed text:", "", excerpt]
    lines += ["", f"Sent from Omascribe {version} on {host}.".replace("  ", " ")]

    msg = EmailMessage()
    msg["From"] = f"{env.get('MAIL_FROM_NAME', 'Omascribe')} <{env['MAIL_FROM']}>"
    msg["To"] = env["MAIL_TO"]
    msg["Subject"] = title
    msg["Date"] = formatdate(localtime=True)
    msg["Message-ID"] = make_msgid(domain=env["MAIL_FROM"].split("@")[-1])
    msg.set_content("\n".join(lines))

    kind = os.environ.get("OMASCRIBE_INPUT", "pdf")
    subtype = {"pdf": "pdf", "svg": "svg+xml", "png": "png", "json": "json"}.get(kind, "octet-stream")
    maintype = "image" if kind in ("svg", "png") else "application"
    safe = "".join(c if c.isalnum() or c in "-_. " else "-" for c in title).strip() or "note"
    msg.add_attachment(file.read_bytes(), maintype=maintype, subtype=subtype,
                       filename=f"{safe}.{file.suffix.lstrip('.') or kind}")

    port = int(env.get("SMTP_PORT", "587"))
    context = ssl.create_default_context()
    try:
        if port == 465:
            server = smtplib.SMTP_SSL(env["SMTP_HOST"], port, context=context, timeout=30)
        else:
            server = smtplib.SMTP(env["SMTP_HOST"], port, timeout=30)
            server.starttls(context=context)
        with server:
            server.login(env["SMTP_USER"], env["SMTP_PASS"])
            server.send_message(msg)
    except (smtplib.SMTPException, OSError) as exc:
        sys.exit(f"mail failed: {exc}")

    print(f"Sent to {env['MAIL_TO']}")


if __name__ == "__main__":
    main()
