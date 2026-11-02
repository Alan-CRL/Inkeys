"""Audit final locale text against the regular/bold embedded fallback cmaps."""

import argparse
import json
import re
import sys
import unicodedata
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--fonttools", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--locales", nargs="+")
args = parser.parse_args()
sys.path.insert(0, str(args.fonttools.resolve()))
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parents[4]
font_dir = root / "Inkeys/src/ttf"
source_dir = root / "Inkeys/src/i18n"
faces = {
    "regular": [
        "HarmonyOS_Sans_SC_Regular.ttf", "HarmonyOS_Sans_TC_Regular.ttf",
        "NotoSansJP-Regular.ttf", "NotoSansKR-Regular.ttf",
    ],
    "bold": [
        "HarmonyOS_SansSC_Bold.ttf", "HarmonyOS_SansTC_Bold.ttf",
        "NotoSansJP-Bold.ttf", "NotoSansKR-Bold.ttf",
    ],
}
coverage = {}
for style, filenames in faces.items():
    chars = set()
    for filename in filenames:
        with TTFont(font_dir / filename, lazy=True) as font:
            chars.update(font.getBestCmap())
    coverage[style] = chars

# 语言页始终包含全部母语名称，即使当前界面是英语也必须覆盖。
native_labels = "English 简体中文 繁體中文（台灣） 繁體中文（香港） 日本語 한국어 Français Deutsch Español Português (Brasil) Русский · Beta"
locales = ("zh-CN", "en-US", "zh-TW", "zh-HK", "ja-JP", "ko-KR", "fr-FR", "de-DE", "es-ES", "pt-BR", "ru-RU")
token_pattern = re.compile(r'"(?:\\.|[^"\\])*"|//[^\r\n]*|/\*[\s\S]*?\*/')


def parse_jsonc(path):
    text = path.read_text(encoding="utf-8-sig")
    text = token_pattern.sub(lambda match: match[0] if match[0].startswith('"') else "", text)
    return json.loads(text)


def leaves(node):
    if isinstance(node, dict):
        for child in node.values():
            yield from leaves(child)
    elif isinstance(node, str):
        yield node
    else:
        raise TypeError("Locale leaf is not text")


results = []
failed = False
for locale in args.locales or locales:
    path = source_dir / f"{locale}.jsonc"
    data = parse_jsonc(path)
    text = "".join(leaves(data)) + native_labels
    requested = {ord(char) for char in text if not unicodedata.category(char).startswith("C")}
    missing = {
        style: [f"U+{code:04X} {chr(code)}" for code in sorted(requested - chars)]
        for style, chars in coverage.items()
    }
    failed |= any(missing.values())
    results.append({"locale": locale, "unique_characters": len(requested), "missing": missing})
    print(f"{locale}: {len(requested)} unique characters, missing={sum(map(len, missing.values()))}")
args.output.write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
sys.exit(1 if failed else 0)
