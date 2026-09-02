#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic compile-time-only pactl fixture for profile/port tests."""

import json
import os
from pathlib import Path
import sys
import time


ROOT = Path(os.environ["SYNAPSE_AUDIO_SELECTION_FIXTURES"])
MODE = os.environ.get("SYNAPSE_AUDIO_SELECTION_MODE", "success")
COUNT_PATH = Path(os.environ.get("SYNAPSE_AUDIO_SELECTION_COUNT", ROOT / "mutation-count"))
LOG_PATH = os.environ.get("SYNAPSE_AUDIO_SELECTION_LOG")
STATE_PATH = ROOT / "mutation-state.json"
READ_COUNT_PATH = ROOT / "selection-read-count"
LIST_COUNT_PATH = ROOT / "selection-list-count"
FAIL_NEXT_PATH = ROOT / "fail-next-selection-list"


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value):
    path.write_text(json.dumps(value, separators=(",", ":")) + "\n", encoding="utf-8")


def read_number(path: Path) -> int:
    try:
        return int(path.read_text(encoding="ascii").strip())
    except (FileNotFoundError, ValueError):
        return 0


def write_number(path: Path, value: int):
    path.write_text(f"{value}\n", encoding="ascii")


def option_items(entry, key):
    options = entry.get(key)
    if key == "profiles":
        if not isinstance(options, dict):
            raise SystemExit(64)
        return list(options.items())
    if not isinstance(options, list):
        raise SystemExit(64)
    items = []
    for metadata in options:
        if not isinstance(metadata, dict) or not isinstance(metadata.get("name"), str):
            raise SystemExit(64)
        items.append((metadata["name"], metadata))
    return items


def option_names(entry, key):
    return [name for name, _ in option_items(entry, key)]


def option_metadata(entry, key, name):
    matches = [metadata for candidate, metadata in option_items(entry, key)
               if candidate == name]
    if len(matches) != 1:
        raise SystemExit(64)
    return matches[0]


def option_selectable(key, metadata):
    if key == "profiles":
        return metadata.get("available") is not False
    return metadata.get("availability") != "not available"


def mutate_active(state, requested):
    path = ROOT / state["file"]
    values = read_json(path)
    selected = [item for item in values if item.get("name") == state["target"]]
    if len(selected) != 1:
        raise SystemExit(64)
    selected[0][state["field"]] = requested
    write_json(path, values)


def make_option_unavailable(state, option):
    path = ROOT / state["file"]
    values = read_json(path)
    selected = [item for item in values if item.get("name") == state["target"]]
    if len(selected) != 1:
        raise SystemExit(64)
    metadata = option_metadata(selected[0], state["options"], option)
    if state["options"] == "profiles":
        metadata["available"] = False
    else:
        metadata["availability"] = "not available"
    write_json(path, values)


def alias_option(state, option):
    mapping = os.environ.get("SYNAPSE_AUDIO_SELECTION_TEST_OPTION_ALIAS", "")
    parts = mapping.split("\t")
    if len(parts) != 2 or not parts[0] or parts[1] != option:
        raise SystemExit(64)
    alias = parts[0]
    path = ROOT / state["file"]
    values = read_json(path)
    selected = [item for item in values if item.get("name") == state["target"]]
    if len(selected) != 1:
        raise SystemExit(64)
    options = selected[0].get(state["options"])
    names = option_names(selected[0], state["options"])
    if option not in names or alias in names:
        raise SystemExit(64)
    if state["options"] == "profiles":
        options[alias] = options.pop(option)
    else:
        option_metadata(selected[0], state["options"], option)["name"] = alias
    if selected[0].get(state["field"]) == option:
        selected[0][state["field"]] = alias
    write_json(path, values)


def third_selection(state):
    path = ROOT / state["file"]
    values = read_json(path)
    selected = [item for item in values if item.get("name") == state["target"]]
    if len(selected) != 1:
        raise SystemExit(64)
    for choice, metadata in option_items(selected[0], state["options"]):
        if (choice not in (state["original"], state["requested"])
                and option_selectable(state["options"], metadata)):
            return choice
    raise SystemExit(64)


def load_hook(file_name: str):
    if FAIL_NEXT_PATH.exists():
        FAIL_NEXT_PATH.unlink()
        raise SystemExit(65)

    if MODE == "change-before-second":
        count = read_number(LIST_COUNT_PATH) + 1
        write_number(LIST_COUNT_PATH, count)
        if count == 2:
            state = json.loads(os.environ["SYNAPSE_AUDIO_SELECTION_DRIFT_STATE"])
            if state["file"] == file_name:
                mutate_active(state, state["requested"])

    if MODE not in ("external-restore-before-rollback", "intervene-before-rollback",
                    "original-unavailable-before-rollback",
                    "requested-alias-before-rollback",
                    "original-alias-before-rollback"):
        return
    if read_number(COUNT_PATH) != 1 or not STATE_PATH.exists():
        return
    state = read_json(STATE_PATH)
    if state["file"] != file_name:
        return
    count = read_number(READ_COUNT_PATH) + 1
    write_number(READ_COUNT_PATH, count)
    if count == 2:
        if MODE == "original-unavailable-before-rollback":
            make_option_unavailable(state, state["original"])
            return
        if MODE == "requested-alias-before-rollback":
            alias_option(state, state["requested"])
            return
        if MODE == "original-alias-before-rollback":
            alias_option(state, state["original"])
            return
        requested = state["original"]
        if MODE == "intervene-before-rollback":
            requested = third_selection(state)
        mutate_active(state, requested)


def emit_list(file_name: str):
    load_hook(file_name)
    text = (ROOT / file_name).read_text(encoding="utf-8")
    if file_name == "cards.json" and MODE in (
            "depth-limit", "depth-over-limit", "object-key-limit",
            "object-key-over-limit", "document-key-over-limit"):
        values = json.loads(text)
        if MODE in ("depth-limit", "depth-over-limit"):
            nested = None
            array_count = 62 if MODE == "depth-limit" else 63
            for _ in range(array_count):
                nested = [nested]
            values[0]["scanner_padding"] = nested
        elif MODE in ("object-key-limit", "object-key-over-limit"):
            key_count = 4096 if MODE == "object-key-limit" else 4097
            values[0]["scanner_padding"] = {
                f"key-{index:04d}": None for index in range(key_count)
            }
        else:
            values[0]["scanner_padding"] = [
                {f"key-{group}-{index:04d}": None for index in range(3500)}
                for group in range(5)
            ]
        text = json.dumps(values, separators=(",", ":")) + "\n"
    if file_name == "cards.json" and MODE == "duplicate-card-key":
        text = text.replace('"name":"card.alpha"',
                            '"name":"card.alpha","name":"card.shadow"', 1)
    elif file_name == "cards.json" and MODE == "escaped-duplicate-card-key":
        text = text.replace('"name":"card.alpha"',
                            '"name":"card.alpha","\\u006eame":"card.shadow"', 1)
    elif file_name == "cards.json" and MODE == "duplicate-option-key":
        text = text.replace('"description":"Off"',
                            '"description":"Off","description":"Shadow"', 1)
    elif file_name == "cards.json" and MODE == "trailing-json":
        text += "[]\n"
    elif file_name == "cards.json" and MODE == "trailing-comma":
        text = text.rstrip().removesuffix("]") + ",]\n"
    elif file_name == "cards.json" and MODE == "escaped-nul-ignored":
        text = text.replace('"index":40',
                            '"scanner_padding":"\\u0000","index":40', 1)
    elif file_name == "cards.json" and MODE == "invalid-primitive":
        text = text.replace('"index":40',
                            '"scanner_padding":NaN,"index":40', 1)
    elif file_name == "cards.json" and MODE == "leading-zero":
        text = text.replace('"index":40',
                            '"scanner_padding":01,"index":40', 1)
    elif file_name == "cards.json" and MODE == "invalid-escape":
        text = text.replace('"index":40',
                            '"scanner_padding":"\\x20","index":40', 1)
    elif file_name == "cards.json" and MODE == "unpaired-surrogate":
        text = text.replace('"index":40',
                            '"scanner_padding":"\\ud83d","index":40', 1)
    elif file_name == "cards.json" and MODE == "escaped-surrogate-pair":
        text = text.replace(
            '"index":40',
            '"scanner_padding":"\\ud83d\\ude00","index":40', 1)
    elif file_name == "cards.json" and MODE in ("exact-limit", "oversized"):
        limit = 1024 * 1024
        requested = limit if MODE == "exact-limit" else limit + 1
        encoded = text.encode("utf-8")
        if len(encoded) >= requested:
            raise SystemExit(64)
        sys.stdout.buffer.write(encoded + b" " * (requested - len(encoded)))
        return
    elif file_name == "cards.json" and MODE == "raw-nul":
        sys.stdout.buffer.write(text.encode("utf-8") + b"\0[]")
        return
    elif file_name == "cards.json" and MODE == "invalid-utf8":
        sys.stdout.buffer.write(text.encode("utf-8").replace(b"Primary", b"\xff", 1))
        return
    sys.stdout.write(text)


def mutate(operation: str, file_name: str, target: str, requested: str,
           field: str, options: str):
    count = read_number(COUNT_PATH) + 1
    write_number(COUNT_PATH, count)
    if LOG_PATH:
        with open(LOG_PATH, "a", encoding="utf-8") as stream:
            stream.write(f"{operation}\t{target}\t{requested}\n")
    if MODE == "fail" or (MODE == "rollback-fail" and count > 1):
        raise SystemExit(65)
    path = ROOT / file_name
    values = read_json(path)
    selected = [item for item in values if item.get("name") == target]
    if len(selected) != 1 or requested not in option_names(selected[0], options):
        raise SystemExit(64)
    original = selected[0].get(field)
    if not isinstance(original, str) or not original:
        raise SystemExit(64)
    state = {
        "file": file_name,
        "target": target,
        "field": field,
        "options": options,
        "original": original,
        "requested": requested,
    }
    write_json(STATE_PATH, state)
    if MODE == "no-mutate-success" and count == 1:
        return
    actual = requested
    if MODE == "unexpected-success" and count == 1:
        actual = third_selection(state)
    selected[0][field] = actual
    write_json(path, values)
    if count == 1 and MODE == "requested-alias-after-mutate":
        alias_option(state, state["requested"])

    if count == 1:
        if MODE in ("fail-after-mutate", "rollback-fail",
                    "external-restore-before-rollback",
                    "intervene-before-rollback",
                    "original-unavailable-before-rollback",
                    "requested-alias-before-rollback",
                    "original-alias-before-rollback",
                    "original-alias-after-rollback"):
            raise SystemExit(65)
        if MODE == "timeout-after-mutate":
            os.close(1)
            os.close(2)
            time.sleep(5)
            raise SystemExit(65)
        if MODE == "postflight-unavailable":
            FAIL_NEXT_PATH.touch()
        elif MODE == "identity-change":
            values = read_json(path)
            selected = [item for item in values if item.get("name") == target]
            selected[0]["index"] = int(selected[0]["index"]) + 1000
            write_json(path, values)
        elif MODE == "vanish-after-selection":
            values = [item for item in read_json(path) if item.get("name") != target]
            write_json(path, values)
    elif count == 2 and MODE == "original-alias-after-rollback":
        alias_option(state, state["requested"])


if MODE == "error-with-descendant":
    descendant = os.fork()
    if descendant == 0:
        for descriptor in (1, 2):
            try:
                os.close(descriptor)
            except OSError:
                pass
        time.sleep(1)
        Path(os.environ["SYNAPSE_AUDIO_SELECTION_SURVIVOR"]).touch()
        os._exit(0)
    Path(os.environ["SYNAPSE_AUDIO_SELECTION_CHILD_PID"]).write_text(
        f"{descendant}\n", encoding="ascii"
    )
    raise SystemExit(65)

if MODE == "hang-with-pid":
    Path(os.environ["SYNAPSE_AUDIO_SELECTION_CHILD_PID"]).write_text(
        f"{os.getpid()}\n", encoding="ascii"
    )
    time.sleep(60)
    raise SystemExit(0)

if MODE == "verify-envelope":
    observed = {
        "pid": os.getpid(),
        "pgrp": os.getpgrp(),
        "lc_all": os.environ.get("LC_ALL"),
        "lang": os.environ.get("LANG"),
        "stdin": os.readlink("/proc/self/fd/0"),
        "stdout": os.readlink("/proc/self/fd/1"),
        "stderr": os.readlink("/proc/self/fd/2"),
    }
    valid = (observed["pgrp"] == observed["pid"]
             and observed["lc_all"] == "C"
             and observed["lang"] == "C"
             and observed["stdin"] == "/dev/null"
             and observed["stderr"] == "/dev/null"
             and observed["stdout"].startswith("pipe:"))
    with open(os.environ["SYNAPSE_AUDIO_SELECTION_ENVELOPE_LOG"], "a",
              encoding="ascii") as stream:
        stream.write(json.dumps({"valid": valid, **observed},
                                separators=(",", ":")) + "\n")
    if not valid:
        raise SystemExit(70)

args = sys.argv[1:]
if args == ["--format=json", "list", "cards"]:
    emit_list("cards.json")
elif args == ["--format=json", "list", "sinks"]:
    emit_list("sinks.json")
elif args == ["--format=json", "list", "sources"]:
    emit_list("sources.json")
elif len(args) == 3 and args[0] == "set-card-profile":
    mutate(args[0], "cards.json", args[1], args[2], "active_profile", "profiles")
elif len(args) == 3 and args[0] == "set-sink-port":
    mutate(args[0], "sinks.json", args[1], args[2], "active_port", "ports")
elif len(args) == 3 and args[0] == "set-source-port":
    mutate(args[0], "sources.json", args[1], args[2], "active_port", "ports")
else:
    raise SystemExit(64)
