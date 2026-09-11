"""Verify the Bar SVG recolor/outer-outline contract without creating a window."""

import argparse
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET


SVG = "{http://www.w3.org/2000/svg}"
PEN_SLOT = "rgba(10,0,7,0)"
OUTLINE_SLOT = "rgba(9,0,2,0)"
MAIN_LOGOS = {"logo1.svg", "logo2.svg", "Frame 94.svg"}
PEN_IDS = ("pen-cap", "pen-barrel", "pen-tip", "pen-nib")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(root, *arguments):
    return subprocess.check_output(["git", "-C", str(root), *arguments])


def paths(root):
    return root.findall(f"{SVG}g/{SVG}path")


def verify(root, baseline):
    baseline_paths = run(root, "ls-tree", "-r", "--name-only", baseline,
                         "Inkeys/src/UI").decode("utf-8").splitlines()
    ordinary_count = 0
    for relative in baseline_paths:
        if not relative.endswith(".svg"):
            continue
        path = Path(relative)
        original = run(root, "show", f"{baseline}:{relative}")
        current = (root / path).read_bytes()
        if path.name not in MAIN_LOGOS:
            # 普通图标只在运行时着色，资源连同几何和导出属性保持原样。
            require(current.replace(b"\r\n", b"\n") == original.replace(b"\r\n", b"\n"),
                    f"ordinary SVG changed: {relative}")
            ordinary_count += 1
        else:
            require(current.startswith(b"\xef\xbb\xbf")
                    == original.startswith(b"\xef\xbb\xbf"),
                    f"BOM changed: {relative}")
            require(current.count(b"\n") == current.count(b"\r\n"),
                    f"CRLF changed: {relative}")
            ET.fromstring(current)

    original = ET.fromstring(run(root, "show",
                                f"{baseline}:Inkeys/src/UI/logo1.svg"))
    original_paths = paths(original)
    require(len(original_paths) == 6, "baseline logo needs two screen/four pen paths")
    documents = {
        name: ET.fromstring((root / "Inkeys/src/UI" / name).read_bytes())
        for name in MAIN_LOGOS
    }
    for name, document in documents.items():
        for attribute in ("width", "height", "viewBox"):
            require(document.get(attribute) == original.get(attribute),
                    f"logo canvas changed: {name}/{attribute}")

    dark_paths = paths(documents["logo1.svg"])
    light_paths = paths(documents["logo2.svg"])
    ink_paths = paths(documents["Frame 94.svg"])
    require(len(dark_paths) == 2, "dark base must contain only the neutral screen")
    require(len(light_paths) == 7, "light logo must contain screen, outline and pen")
    require(len(ink_paths) == 4, "dark pen layer must not contain screen/indicator shapes")
    for index, original_screen in enumerate(original_paths[:2], start=1):
        for name in ("logo1.svg", "logo2.svg"):
            screen = documents[name].find(f".//*[@id='screen-{index}']")
            require(screen is not None, f"missing screen path: {name}/{index}")
            require(screen.get("d") == original_screen.get("d"),
                    f"screen geometry changed: {name}/{index}")
            require(screen.get("fill") not in (PEN_SLOT, OUTLINE_SLOT),
                    f"screen must not follow pen/outline: {name}/{index}")
            require(screen.get("fill") in ("white", "#BAC3C8"),
                    f"screen must retain neutral color: {name}/{index}")

    for name in ("logo2.svg", "Frame 94.svg"):
        for node_id, original_pen in zip(PEN_IDS, original_paths[2:]):
            pen = documents[name].find(f".//*[@id='{node_id}']")
            require(pen is not None, f"missing pen section: {name}/{node_id}")
            require(pen.get("d") == original_pen.get("d"),
                    f"original pen geometry changed: {name}/{node_id}")
            require(pen.get("fill") == PEN_SLOT and pen.get("fill-opacity", "1") == "1",
                    f"whole pen must use opaque actual/display RGB: {name}/{node_id}")
            require(pen.get("stroke", "none") == "none",
                    f"pen section must never stroke an internal cut: {name}/{node_id}")

    strokes = [node for document in documents.values() for node in document.iter()
               if node.get("stroke", "none") != "none"]
    require(len(strokes) == 1, "exactly one silhouette outline is allowed")
    outline = strokes[0]
    require(outline.get("id") == "pen-outline"
            and outline.get("stroke") == OUTLINE_SLOT
            and outline.get("fill") == "none", "outline must have its own color role")
    commands = re.findall("[A-Za-z]", outline.get("d", ""))
    require(commands.count("M") == 1 and commands.count("Z") == 1,
            "outline must be one complete contour, never closed individual segments")
    require(0 < float(outline.get("stroke-width")) <= 3.0,
            "outline must stay thin at the 256-unit source scale")
    require(all(node.tag not in (f"{SVG}linearGradient", f"{SVG}radialGradient")
                for document in documents.values() for node in document.iter()),
            "screen tint and partially colored pen gradients must not return")
    print(f"PASS SVG contract: {ordinary_count} ordinary SVGs unchanged; "
          "original pen/screen geometry, whole-pen slots, single outline, BOM/CRLF")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--baseline", default="4478887c")
    arguments = parser.parse_args()
    verify(arguments.root.resolve(), arguments.baseline)
