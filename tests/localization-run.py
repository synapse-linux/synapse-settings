#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the pinned Synapse GUI locale catalogue inventory.

The inventory mirrors synapse.i18n.locales/v1 at CachyOS/cachyos-calamares
commit ce54421cf0a08008ccda59a4e5f2f4747ae762be.  Non-English/Italian
catalogues intentionally remain explicit en_US fallback catalogues for this
alpha and are marked unfinished rather than overstating translation coverage.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

LOCALES = (
    "ar", "as", "ast", "az", "az_AZ", "be", "bg", "bn", "ca",
    "ca@valencia", "cs_CZ", "da", "de", "el", "en_US", "en_GB", "eo",
    "es", "es_AR", "es_MX", "et", "eu", "fa", "fi_FI", "fr", "fur",
    "gl", "he", "hi", "hr", "hu", "ia", "id", "is", "it_IT", "ja",
    "ka", "ko", "lt", "ml", "mr", "nb", "nl", "oc", "pl", "pt_BR",
    "pt_PT", "ro", "ru", "si", "sk", "sl", "sq", "sr", "sr@latin",
    "sv", "tg", "th", "tr_TR", "uk", "uz", "vi", "zh_CN", "zh_TW",
)
COMPLETE_LOCALES = frozenset({"en_US", "it_IT"})
PLACEHOLDER = re.compile(r"%(?:L?[1-9][0-9]*|n)")


def text(element: ET.Element | None) -> str:
    return "" if element is None else "".join(element.itertext())


def active_messages(root: ET.Element) -> dict[tuple[str, str, str, str], ET.Element]:
    messages: dict[tuple[str, str, str, str], ET.Element] = {}
    for context in root.findall("context"):
        context_name = text(context.find("name"))
        for message in context.findall("message"):
            translation = message.find("translation")
            if translation is not None and translation.get("type") in {
                "vanished",
                "obsolete",
            }:
                continue
            key = (
                context_name,
                text(message.find("source")),
                text(message.find("comment")),
                message.get("numerus", "no"),
            )
            if key in messages:
                raise ValueError(f"duplicate active message {key!r}")
            messages[key] = message
    return messages


def makefile_locales(makefile: str) -> tuple[str, ...]:
    match = re.search(
        r"^GUI_LOCALES\s*:=\s*(.*?)^GUI_COMPLETE_LOCALES\s*:=",
        makefile,
        flags=re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise ValueError("GUI_LOCALES declaration missing")
    return tuple(match.group(1).replace("\\\n", " ").split())


def fail(message: str) -> None:
    print(f"localization validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def extracted_source_keys(root: Path) -> set[tuple[str, str, str, str]]:
    sources = sorted((root / "gui").glob("*.cpp"))
    sources.extend(sorted((root / "gui" / "qml").glob("*.qml")))
    lupdate = os.environ.get("LUPDATE6", "lupdate6")
    with tempfile.TemporaryDirectory(prefix="synapse-settings-lupdate-") as tmp:
        output = Path(tmp) / "source-extract.ts"
        try:
            result = subprocess.run(
                [
                    lupdate,
                    *(str(path.relative_to(root)) for path in sources),
                    "-no-obsolete",
                    "-locations",
                    "none",
                    "-ts",
                    str(output),
                ],
                cwd=root,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
        except OSError as error:
            raise ValueError(f"cannot execute {lupdate}: {error}") from error
        if result.returncode != 0:
            raise ValueError(
                f"{lupdate} source extraction failed: {result.stdout.strip()}"
            )
        try:
            return set(active_messages(ET.parse(output).getroot()))
        except (ET.ParseError, OSError) as error:
            raise ValueError(f"cannot parse {lupdate} source extraction: {error}") from error


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    i18n = root / "gui" / "i18n"
    if len(LOCALES) != 64 or len(set(LOCALES)) != 64:
        fail("pinned locale inventory is not 64 unique identifiers")

    expected_files = {f"synapse-settings_{locale}.ts" for locale in LOCALES}
    actual_files = {path.name for path in i18n.glob("synapse-settings_*.ts")}
    if actual_files != expected_files:
        fail(
            "catalogue set mismatch: missing="
            f"{sorted(expected_files - actual_files)!r} extra="
            f"{sorted(actual_files - expected_files)!r}"
        )
    try:
        declared = makefile_locales((root / "Makefile").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        fail(str(error))
    if declared != LOCALES:
        fail("Makefile locale order does not match the pinned inventory")

    parsed: dict[str, ET.Element] = {}
    try:
        for locale in LOCALES:
            catalogue = ET.parse(i18n / f"synapse-settings_{locale}.ts").getroot()
            if catalogue.tag != "TS" or catalogue.get("language") != locale:
                raise ValueError(f"{locale}: invalid TS root or language")
            parsed[locale] = catalogue
    except (ET.ParseError, OSError, ValueError) as error:
        fail(str(error))

    try:
        source_messages = active_messages(parsed["en_US"])
    except ValueError as error:
        fail(f"en_US: {error}")
    if len(source_messages) != 133:
        fail(f"en_US: expected 133 active messages, found {len(source_messages)}")
    try:
        if extracted_source_keys(root) != set(source_messages):
            fail("en_US catalogue differs from current lupdate6 source extraction")
    except ValueError as error:
        fail(str(error))

    coverage: dict[str, tuple[int, int]] = {}
    for locale in LOCALES:
        try:
            messages = active_messages(parsed[locale])
        except ValueError as error:
            fail(f"{locale}: {error}")
        if messages.keys() != source_messages.keys():
            fail(f"{locale}: active source message inventory differs from en_US")
        finished = 0
        unfinished = 0
        for key, message in messages.items():
            translation = message.find("translation")
            if translation is None:
                fail(f"{locale}: message {key!r} has no translation element")
            source_text = key[1]
            translated_text = text(translation)
            source_placeholders = sorted(PLACEHOLDER.findall(source_text))
            translated_placeholders = sorted(PLACEHOLDER.findall(translated_text))
            if source_placeholders != translated_placeholders:
                fail(f"{locale}: placeholder mismatch for {source_text!r}")
            is_unfinished = translation.get("type") == "unfinished"
            if locale in COMPLETE_LOCALES:
                if is_unfinished or not translated_text:
                    fail(f"{locale}: unfinished or empty translation for {source_text!r}")
                finished += 1
            else:
                if not is_unfinished or translated_text != source_text:
                    fail(
                        f"{locale}: provisional catalogue must use an explicit "
                        f"unfinished en_US fallback for {source_text!r}"
                    )
                unfinished += 1
        coverage[locale] = (finished, unfinished)

    print(
        "synapse-settings GUI localization: "
        f"{len(LOCALES)} catalogs, {len(source_messages)} active messages each; "
        "en_US/it_IT complete, 62 explicit en_US fallback catalogs: PASS"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
