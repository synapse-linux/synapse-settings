#!/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Deterministic fixture for the authoritative Synapse theme v3 contract."""

import json
import os
import sys


def payload(theme_id: str, background: str, accent: str) -> dict:
    palette = {
        "background": background,
        "surface": "#24283b" if theme_id == "tokyo-night" else "#0d0d0d",
        "surfaceHover": "#414868" if theme_id == "tokyo-night" else "#1e1e1e",
        "border": "#565f89" if theme_id == "tokyo-night" else "#333333",
        "accent": accent,
        "text": "#c0caf5" if theme_id == "tokyo-night" else "#bebebe",
        "muted": "#9aa5ce" if theme_id == "tokyo-night" else "#555555",
        "urgent": "#f7768e" if theme_id == "tokyo-night" else "#d35f5f",
    }
    return {
        "schema": "synapse.theme.current/v3",
        "id": theme_id,
        "theme": {
            "id": theme_id,
            "name": theme_id.replace("-", " ").title(),
            "description": "Deterministic first-party theme fixture.",
            "source": "synapse-test-fixture",
            "preview": f"/usr/share/synapse/themes/{theme_id}/preview.png",
            "wallpaper": f"/usr/share/synapse/themes/{theme_id}/background.jpg",
            "backgrounds": [
                f"/usr/share/synapse/themes/{theme_id}/background.jpg"
            ],
            "palette": palette,
        },
    }


def main() -> None:
    if sys.argv[1:] != ["current", "--format", "json"]:
        raise SystemExit(2)
    mode = os.environ.get("SYNAPSE_SETTINGS_THEME_FIXTURE", "valid")
    if mode.startswith("fork-timeout:"):
        import time
        pid_path = mode.removeprefix("fork-timeout:")
        child = os.fork()
        if child == 0:
            time.sleep(10)
            os._exit(0)
        with open(pid_path, "w", encoding="ascii") as stream:
            stream.write(f"{child}\n")
        time.sleep(10)
    if mode == "timeout":
        import time
        time.sleep(10)
    if mode == "oversized-stdout":
        sys.stdout.write("x" * (32 * 1024))
        return
    if mode == "oversized-stderr":
        sys.stderr.write("x" * (32 * 1024))
        return
    if mode == "stream-output":
        import time
        for _ in range(64):
            sys.stdout.write("x" * 1024)
            sys.stdout.flush()
            time.sleep(0.01)
        return
    if mode == "invalid":
        print('{"schema":"synapse.theme/current/v3"}')
        return
    if mode == "fail":
        raise SystemExit(1)
    if mode == "alternate":
        value = payload("matte-black", "#121212", "#e68e0d")
    elif mode == "unsafe-id":
        value = payload("unsafe\nid", "#1a1b26", "#7aa2f7")
    else:
        value = payload("tokyo-night", "#1a1b26", "#7aa2f7")
    encoded = json.dumps(value, separators=(",", ":"), sort_keys=True)
    if mode == "duplicate-key":
        needle = '"id":"tokyo-night"'
        encoded = encoded.replace(needle, f"{needle},{needle}", 1)
    print(encoded)


if __name__ == "__main__":
    main()
