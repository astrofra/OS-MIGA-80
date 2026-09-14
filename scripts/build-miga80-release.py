#!/usr/bin/env python3
"""Build the reference preview disk, including every assets/demo/*.lua file."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: build-miga80-release.py program font output.adf")
    program, font, output = map(pathlib.Path, sys.argv[1:])
    demos = sorted(pathlib.Path("assets/demo").glob("*.lua"))
    if not demos:
        raise SystemExit("No Lua demos found")
    files = {
        "MIGA80": program,
        "S/Startup-Sequence": pathlib.Path("assets/demo/Startup-Browser"),
        "DATA/FONT4X8.BIN": font,
        "README.TXT": pathlib.Path("assets/demo/README.TXT"),
        "LICENSE.TXT": pathlib.Path("LICENSE"),
        "PTPLAYER.TXT": pathlib.Path("third_party/ptplayer/LICENSE"),
        "mods/93_10_12_A_SYNTH_1.mod": pathlib.Path("works/mods/93_10_12_A_SYNTH_1.mod"),
        "KALMS.TXT": pathlib.Path("third_party/kalms-c2p/readme.txt"),
        **{f"demos/{p.name}": p for p in demos},
    }
    for path in files.values():
        if not path.is_file():
            raise SystemExit(f"Missing input: {path}")
    output.parent.mkdir(parents=True, exist_ok=True)
    # An interrupted build cannot destroy the previous reference disk.
    temporary = output.with_suffix(".building.adf")
    command = ["xdftool", "-f", str(temporary), "create", "+", "format", "MIGA80", "ofs"]
    for directory in ("S", "DATA", "demos", "mods"):
        command += ["+", "makedir", directory]
    for destination, source in files.items():
        command += ["+", "write", str(source), destination]
    command += ["+", "boot", "install"]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["xdfscan", str(temporary)], check=True)
    # Validate packaged bytes, not only the OFS structure.
    with tempfile.TemporaryDirectory() as check_dir:
        for index, (destination, source) in enumerate(files.items()):
            extracted = pathlib.Path(check_dir) / str(index)
            subprocess.run(["xdftool", str(temporary), "read", destination, str(extracted)],
                           check=True, stdout=subprocess.DEVNULL)
            if extracted.read_bytes() != source.read_bytes():
                raise SystemExit(f"ADF payload mismatch: {destination}")
    if temporary.stat().st_size != 901120:
        raise SystemExit("Expected a standard 880 KiB floppy")
    listing = subprocess.check_output(["xdftool", str(temporary), "list"], text=True)
    manifest = {
        "format": "miga80-reference-adf-1",
        "edition": "development preview",
        "adf": output.name,
        "adf_bytes": temporary.stat().st_size,
        "adf_sha256": sha256(temporary),
        "filesystem": "OFS",
        "volume": "MIGA80",
        "startup": "centered glitch logo + procedural jingle, then SYS:demos file selector",
        "embedded_logo": {"source": "works/logo.png", "sha256": sha256(pathlib.Path("works/logo.png")),
                          "width": 196, "height": 43},
        "runtime_report": "RAM:MIGA80-BOOTED.TXT",
        "files": {destination: {"source": str(source), "bytes": source.stat().st_size,
                                  "sha256": sha256(source)}
                  for destination, source in files.items()},
        "filesystem_listing": listing.splitlines(),
    }
    temporary.replace(output)
    output.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Built and verified {output} ({len(demos)} Lua demos)")


if __name__ == "__main__":
    main()
