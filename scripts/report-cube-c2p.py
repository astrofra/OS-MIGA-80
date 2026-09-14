#!/usr/bin/env python3
"""Summarize complete, sequential CUBEPIXELTEST runs on the same ADF/config."""
import json
import pathlib


def main():
    out = pathlib.Path(__file__).resolve().parent.parent / 'build/reports'
    rows, configurations = [], []
    for backend in ('reference', 'mask32', 'kalms'):
        path = out / f'cube-chunky-{backend}-fs-uae.txt'
        values = dict(line.split('=', 1) for line in path.read_text().splitlines())
        assert values['result'] == 'pass' and values['c2p_backend'] == backend
        configurations.append(json.loads(path.with_suffix('.json').read_text()))
        for sample in range(2):
            v = {k.removeprefix(f'sample{sample}_'): int(n) for k, n in values.items()
                 if k.startswith(f'sample{sample}_')}
            assert v['frames'] == v['c2p_calls'] >= 2
            assert 10 * 65536 <= v['elapsed_q16'] < 11 * 65536
            rows.append(f"| {backend} | {sample + 1} | {v['frames']} | "
                        f"{v['elapsed_q16']/65536:.4f} | "
                        f"{1000*v['c2p_ticks']/v['c2p_calls']/v['eclock_hz']:.3f} | "
                        f"{1000*v['c2p_min_ticks']/v['eclock_hz']:.3f}–"
                        f"{1000*v['c2p_max_ticks']/v['eclock_hz']:.3f} |")
    assert all(c == configurations[0] for c in configurations), configurations
    report = ('# Cube chunky — C2P comparison\n\n'
              'FS-UAE A1200 PAL, accuracy=1, 2 MiB Chip; Fast KiB: '
              + str(configurations[0]['fast_kib']) + '.\n\n'
              'Same executable, Lua source, display and double buffering in all runs.\n'
              'Full 256×256 conversion to four PF1 planes on every flip.\n\n'
              '| Backend | Run | Frames | Elapsed (s) | Mean C2P (ms/frame) | C2P min–max (ms) |\n'
              '|---|---:|---:|---:|---:|---:|\n' + '\n'.join(rows) + '\n\n'
              'Elapsed starts at the first displayed frame. C2P includes that first conversion,\n'
              'so its total is not directly subtractable from elapsed. C2P brackets include\n'
              'dispatch/validation and timer overhead, exclude drawing, buffer swaps and final\n'
              'result publication/readback, and run with display DMA and AmigaOS active.\n'
              'The animation clock makes sampled orientations differ between backends.\n'
              'These are emulator observations, not physical A1200 throughput.\n\n'
              'ADF SHA-256: `' + configurations[0]['adf_sha256'] + '`\n')
    (out / 'cube-c2p-comparison.md').write_text(report)
    print(report)


if __name__ == '__main__':
    main()
