#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

import gettext
import json
import sys
from pathlib import Path
from typing import NamedTuple

USAGE = """usage:
  kcm-json-l10n.py extract <output.pot> <kcm.json.in>...
  kcm-json-l10n.py merge <kcm.json.in> <output.json> <podir>"""


class TranslatableField(NamedTuple):
    parent_path: tuple
    key: str
    translator_comment: str


FIELDS = [
    TranslatableField(("KPlugin",), "Name", "name of the page in System Settings"),
    TranslatableField(("KPlugin",), "Description", "description of the page in System Settings"),
    TranslatableField(
        (),
        "X-KDE-Keywords",
        "comma-separated search keywords for the page in System Settings. "
        "Keep the English keywords and add translated ones, separated by commas",
    ),
]


def strings(data):
    for field in FIELDS:
        parent = data
        for part in field.parent_path:
            parent = parent.get(part, {})
        value = parent.get(field.key)
        if isinstance(value, str) and value:
            yield parent, field.key, value, field.translator_comment


def po_quote(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def extract(output, sources):
    lines = [
        'msgid ""',
        'msgstr ""',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        "",
    ]
    for source in sources:
        data = json.loads(Path(source).read_text(encoding="utf-8"))
        for _obj, key, value, comment in strings(data):
            lines += [
                f"#. {comment}",
                f"#: {source}",
                f"msgctxt {po_quote(key)}" if key == "X-KDE-Keywords" else None,
                f"msgid {po_quote(value)}",
                'msgstr ""',
                "",
            ]
    Path(output).write_text(
        "\n".join(line for line in lines if line is not None), encoding="utf-8"
    )


def merge(source, output, podir):
    podir = Path(podir)
    data = json.loads(Path(source).read_text(encoding="utf-8"))
    linguas = podir / "LINGUAS"
    languages = linguas.read_text(encoding="utf-8").split() if linguas.exists() else []

    for lang in sorted(languages):
        with open(podir / f"{lang}.mo", "rb") as mo:
            catalog = gettext.GNUTranslations(mo)
        for obj, key, value, _comment in strings(data):
            if key == "X-KDE-Keywords":
                translated = catalog.pgettext(key, value)
            else:
                translated = catalog.gettext(value)
            is_translated = translated != value
            if is_translated:
                obj[f"{key}[{lang}]"] = translated

    text = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    Path(output).write_text(text, encoding="utf-8")


def main(argv):
    if len(argv) >= 3 and argv[1] == "extract":
        extract(argv[2], argv[3:])
    elif len(argv) == 5 and argv[1] == "merge":
        merge(argv[2], argv[3], argv[4])
    else:
        print(USAGE, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
