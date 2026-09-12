#!/usr/bin/env python3
"""Compare guarded encoders, then execute the shipping trampoline in Musashi."""
import pathlib
import subprocess
import sys


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def main():
    encoder, runner, cc, assembler, objcopy = sys.argv[1:]
    root = pathlib.Path(__file__).resolve().parent.parent
    out = root / "build/host/runtime-guards"
    out.mkdir(parents=True, exist_ok=True)
    images = {}
    # Locations that cannot use MOVEQ exercise the long-immediate fault path.
    long_location = out / "long-location.lua"
    long_location.write_text("\n" * 128 + "function main(): void\n" +
                             " " * 130 + "while true do\n  end\nend\n")
    for name, source in (
        ("mandelbrot", "assets/demo/default.lua"),
        ("budget", "tests/runtime/budget.lua"),
        ("infinite", "tests/runtime/infinite.lua"),
        ("forced-fault", "tests/runtime/forced-fault.lua"),
        ("long-location", str(long_location)),
    ):
        direct = out / f"{name}.bin"
        assembly = out / f"{name}.s"
        with (out / f"{name}-encoder.txt").open("w") as report:
            run(encoder, "--guarded", source, str(direct), str(assembly),
                stdout=report)
        assembled = out / f"{name}-assembly.bin"
        run(assembler, "-m68020", str(assembly), "-o", str(out / f"{name}.o"))
        run(objcopy, "-O", "binary", "-j", ".text", str(out / f"{name}.o"),
            str(assembled))
        if direct.read_bytes() != assembled.read_bytes():
            raise RuntimeError(f"{name}: guarded direct/GNU bytes differ")
        print(f"PASS  {name} guarded direct/GNU bytes={direct.stat().st_size}", flush=True)
        image = out / f"{name}-runtime.bin"
        obj = out / f"{name}-runtime.o"
        flags = ["-DFORCE_FAULT"] if name == "forced-fault" else []
        run(cc, "-c", "-m68020", "-I.", *flags,
            f'-DCODE_FILE="{direct}"', "tests/runtime/entry.S", "-o", str(obj))
        run(objcopy, "-O", "binary", "-j", ".text", str(obj), str(image))
        images[name] = image

    # The finite loop performs exactly three backward transfers before pset.
    # The framebuffer hashes are independent canonical byte-buffer oracles.
    # A scalar Q16.16 source simulation counts 139680 Mandelbrot iterations:
    # 139680 + 20480 columns + 128 rows = 160288 backward transfers.
    empty_hash = "0x5e509dc5"
    for image, name, budget, fault, line, column, remaining, checksum in (
        ("budget", "exact-budget", 3, 0, 0, 0, 0, "0x481edf62"),
        ("budget", "budget-one-short", 2, 3, 3, 3, 0, empty_hash),
        ("budget", "budget-zero", 0, 3, 3, 3, 0, empty_hash),
        ("infinite", "empty-continue-loop", 8, 3, 2, 3, 0, empty_hash),
        ("forced-fault", "forced-fault-unwind", 0, 1, 2, 3, 0, empty_hash),
        ("long-location", "empty-loop-long-location", 1, 3, 130, 131, 0, empty_hash),
        ("mandelbrot", "guarded-mandelbrot", 1000000, 0, 0, 0, 839712,
         "0xc4604fc7"),
    ):
        run(runner, "--runtime", str(images[image]), name, str(budget),
            str(fault), str(line), str(column), str(remaining), checksum)


if __name__ == "__main__":
    main()
