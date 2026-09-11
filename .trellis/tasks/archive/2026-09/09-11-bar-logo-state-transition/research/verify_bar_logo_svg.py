"""Check the shared Logo geometry and untouched ordinary assets without a window."""

import argparse
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET


SVG = "{http://www.w3.org/2000/svg}"
MAIN = {"logo1.svg", "logo2.svg", "Frame 94.svg"}


def git(root, *arguments):
    return subprocess.check_output(["git", "-C", str(root), *arguments])


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def verify(root):
    ordinary_count = 0
    for relative in git(root, "ls-tree", "-r", "--name-only", "0606cbe0",
                        "Inkeys/src/UI").decode().splitlines():
        if not relative.endswith(".svg") or Path(relative).name in MAIN:
            continue
        baseline = git(root, "show", f"0606cbe0:{relative}")
        require((root / relative).read_bytes().replace(b"\r\n", b"\n")
                == baseline.replace(b"\r\n", b"\n"), f"ordinary SVG changed: {relative}")
        ordinary_count += 1

    source = (root / "Inkeys/src/UI/logo1.svg").read_bytes()
    require(source.count(b"\n") == source.count(b"\r\n"), "Logo must retain CRLF")
    document = ET.fromstring(source)
    original = ET.fromstring(git(root, "show", "4478887c:Inkeys/src/UI/logo1.svg"))
    old_ink = ET.fromstring(git(root, "show", "4478887c:Inkeys/src/UI/Frame 94.svg"))
    old_light = ET.fromstring(git(root, "show", "0606cbe0:Inkeys/src/UI/logo2.svg"))
    for attribute in ("width", "height", "viewBox"):
        require(document.get(attribute) == original.get(attribute), "Logo canvas changed")
    ids = [node.get("id") for node in document.iter() if node.get("id")]
    require(len(ids) == len(set(ids)), "duplicate SVG ID makes attribute application ambiguous")
    base_paths = original.findall(f"{SVG}g/{SVG}path")
    for node_id, original_path in zip(
            ("screen-1", "screen-2", "pen-cap", "pen-barrel", "pen-tip", "pen-nib"), base_paths):
        current = document.find(f".//*[@id='{node_id}']")
        require(current is not None and current.get("d") == original_path.get("d"),
                f"original base geometry changed: {node_id}")
        require(current.get("stroke", "none") == "none", f"internal cut is stroked: {node_id}")

    indicator = document.find(".//*[@id='ink-indicator']")
    require(indicator is not None, "missing original gradient indicator group")
    # 保存真正的旧渐变形状，不能用新的整笔实色资源冒充基线。
    old_nodes = list(old_ink.find(f"{SVG}g"))
    current_nodes = list(indicator)
    require(len(old_nodes) == len(current_nodes), "original indicator lost a screen/pen/highlight part")
    for old, current in zip(old_nodes, current_nodes):
        require(old.tag == current.tag, "original indicator node order changed")
        for attribute in ("d", "x", "y", "width", "height", "rx", "transform", "fill-opacity"):
            require(old.get(attribute) == current.get(attribute),
                    f"original gradient geometry/alpha changed: {attribute}")
    for gradient_tag in ("linearGradient", "radialGradient"):
        old_gradients = old_ink.findall(f".//{SVG}{gradient_tag}")
        new_gradients = document.findall(f".//{SVG}{gradient_tag}")
        require(len(old_gradients) == len(new_gradients), "original gradient definitions missing")
        for old, current in zip(old_gradients, new_gradients):
            require(old.attrib == current.attrib, "gradient coordinates or IDs changed")
            require([s.get("stop-opacity", "1") for s in old]
                    == [s.get("stop-opacity", "1") for s in current], "gradient alpha stops changed")

    outline = document.find(".//*[@id='pen-outline']")
    reference_outline = old_light.find(".//*[@id='pen-outline']")
    require(outline is not None and outline.get("d") == reference_outline.get("d"),
            "Light whole outline geometry changed")
    commands = re.findall("[A-Za-z]", outline.get("d", ""))
    require(commands.count("M") == 1 and commands.count("Z") == 1,
            "outline must be one outer contour without closing each segment")
    require(0 < float(outline.get("stroke-width")) <= 3, "outline must stay thin")
    print(f"PASS shared Logo: original 4478887c base/gradient/highlight geometry; "
          f"Light outline; {ordinary_count} ordinary SVGs unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    verify(parser.parse_args().root.resolve())
