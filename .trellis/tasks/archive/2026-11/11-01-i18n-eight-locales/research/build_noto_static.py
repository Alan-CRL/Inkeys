"""Reproduce the embedded Noto Sans JP/KR static TrueType assets.

Install fonttools==4.60.1 into an isolated directory, then provide that directory
as --fonttools and the pinned variable downloads as --inputs. No project build
dependency or system font installation is introduced.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("--fonttools", type=Path, required=True)
parser.add_argument("--inputs", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--manifest", type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.fonttools.resolve()))

import fontTools
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


sources = {
    "NotoSansJP": ("notosansjp", "66a36c8c94b1a5d992ee4e7f392fccfe4945767c"),
    "NotoSansKR": ("notosanskr", "b38c5c93af322c45f633e17ac440ec1e6c94d489"),
}
manifest = {"fonttools": fontTools.__version__, "fonts": []}
for basename, (folder, commit) in sources.items():
    source = args.inputs / f"{basename}-VF.ttf"
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    for style, weight in (("Regular", 400), ("Bold", 700)):
        # 固定所有轴，避免旧版 DWrite / ImGui 依赖可变字体支持。
        font = TTFont(source, recalcTimestamp=False)
        assert {axis.axisTag for axis in font["fvar"].axes} == {"wght"}
        instantiateVariableFont(
            font, {"wght": weight}, inplace=True, updateFontNames=True
        )
        assert "fvar" not in font and "gvar" not in font and "glyf" in font
        assert font["OS/2"].usWeightClass == weight
        output = args.output / f"{basename}-{style}.ttf"
        font.save(output)
        manifest["fonts"].append(
            {
                "file": output.name,
                "family": font["name"].getBestFamilyName(),
                "style": font["name"].getBestSubFamilyName(),
                "weight": weight,
                "size": output.stat().st_size,
                "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                "source_sha256": source_hash,
                "source": f"https://github.com/google/fonts/blob/{commit}/ofl/{folder}/{basename}%5Bwght%5D.ttf",
                "license": f"{basename}-OFL.txt",
            }
        )
        print(f"{output.name}: {font['name'].getBestFamilyName()} / {style}", flush=True)
        font.close()
args.manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
