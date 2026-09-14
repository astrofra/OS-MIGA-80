#!/usr/bin/env python3
"""Add negative/scroll fixtures to the test COPY of the release ADF only."""
import pathlib
import subprocess
import sys
import tempfile

adf = sys.argv[1]
source = b"function main(): void\n  pset(1, 1, 3)\nend\n"
with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary) / "picker-test"
    root.mkdir()
    (root / "empty").mkdir()
    (root / "nested").mkdir()
    (root / "nested" / "inside.LuA").write_bytes(source)
    for i in range(13):
        (root / f"extra{i:02}.lua").write_bytes(source)
    for name, data in {
        "Broken.lua": b"bad\x00source\n",
        "Huge.lua": b"x" * 4100,
        "Long.lua": b"x" * 65 + b"\n",
        "Rows.lua": b"x\n" * 31,
        "Gone.lua": source,
        "note.txt": b"Not a Lua file\n",
    }.items():
        (root / name).write_bytes(data)
    subprocess.run(["xdftool", adf, "write", str(root)], check=True)
