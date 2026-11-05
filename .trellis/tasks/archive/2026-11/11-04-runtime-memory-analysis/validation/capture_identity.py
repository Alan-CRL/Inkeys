"""Verify PE/PDB CodeView identity and freeze a deployment for comparison."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import uuid


def identities(executable, pdb):
    with executable.open("rb") as stream:
        pe = struct.unpack_from("<I", stream.read(64), 60)[0]
        stream.seek(pe)
        coff = stream.read(24)
        count, size = struct.unpack_from("<H", coff, 6)[0], struct.unpack_from("<H", coff, 20)[0]
        optional = stream.read(size)
        start = 112 if struct.unpack_from("<H", optional)[0] == 0x20B else 96
        debug_rva, debug_size = struct.unpack_from("<II", optional, start + 6 * 8)
        debug_file = None
        for _ in range(count):
            section = stream.read(40)
            virtual_size, virtual_address, raw_size, raw = struct.unpack_from("<IIII", section, 8)
            if virtual_address <= debug_rva < virtual_address + max(virtual_size, raw_size):
                debug_file = raw + debug_rva - virtual_address
        stream.seek(debug_file)
        directory = stream.read(debug_size)
        codeview = None
        for offset in range(0, len(directory) - 27, 28):
            entry = struct.unpack_from("<IIHHIIII", directory, offset)
            if entry[4] != 2:
                continue
            stream.seek(entry[7])
            value = stream.read(entry[5])
            if value[:4] == b"RSDS":
                codeview = {"guid": str(uuid.UUID(bytes_le=value[4:20])),
                            "age": struct.unpack_from("<I", value, 20)[0],
                            "pdb_path": value[24:].split(b"\0")[0].decode("utf-8", "replace")}
    with pdb.open("rb") as stream:
        block_size, _, _, directory_bytes, _, block_map = struct.unpack_from("<6I", stream.read(56), 32)
        block_count = (directory_bytes + block_size - 1) // block_size
        stream.seek(block_map * block_size)
        block_ids = struct.unpack("<" + "I" * block_count, stream.read(4 * block_count))
        chunks = []
        for block in block_ids:
            stream.seek(block * block_size)
            chunks.append(stream.read(block_size))
        directory = b"".join(chunks)[:directory_bytes]
        count = struct.unpack_from("<I", directory)[0]
        sizes = struct.unpack_from("<" + "I" * count, directory, 4)
        cursor = 4 + count * 4
        pdb_identity = None
        for index, size in enumerate(sizes):
            if size == 0xFFFFFFFF:
                continue
            blocks = (size + block_size - 1) // block_size
            block_ids = struct.unpack_from("<" + "I" * blocks, directory, cursor)
            cursor += blocks * 4
            if index == 1:
                chunks = []
                for block in block_ids:
                    stream.seek(block * block_size)
                    chunks.append(stream.read(block_size))
                value = b"".join(chunks)[:size]
                pdb_identity = {"guid": str(uuid.UUID(bytes_le=value[12:28])),
                                "age": struct.unpack_from("<I", value, 8)[0]}
                break
    assert codeview and pdb_identity
    assert all(codeview[key] == pdb_identity[key] for key in ("guid", "age"))
    return {"machine": hex(struct.unpack_from("<H", coff, 4)[0]), "codeview": codeview,
            "pdb_identity": pdb_identity, "pdb_matches_exe": True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--deployment", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--template")
    opts = parser.parse_args()
    executable = Path(opts.exe).resolve(strict=True)
    pdb = executable.with_suffix(".pdb")
    deployment = Path(opts.deployment).resolve()
    result = identities(executable, pdb)
    result.update({"head": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                   "configuration": "Debug|ARM64", "files": [],
                   "source_diff_sha256": hashlib.sha256(subprocess.check_output(
                       ["git", "diff", "--ignore-submodules=all", "--", "Inkeys", "inkStrokeModelerTest/draw3/uink_draw3_export.cpp"])).hexdigest()})
    if opts.template:
        shutil.copytree(Path(opts.template).resolve(strict=True), deployment, dirs_exist_ok=True)
    deployment.mkdir(parents=True, exist_ok=True)
    for path in (executable, pdb):
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        shutil.copy2(path, deployment / path.name)
        result["files"].append({"path": str(path), "bytes": path.stat().st_size, "sha256": digest})
    Path(opts.output).write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
