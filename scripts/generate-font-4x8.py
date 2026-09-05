#!/usr/bin/env python3
"""Convert the MIGA-80 4x8 PNG sheet into bounded runtime assets."""

from __future__ import annotations

import argparse
import binascii
import pathlib
import struct
import sys
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CELL_WIDTH = 4
CELL_HEIGHT = 8
ASCII_FIRST = 0x20
ASCII_LAST = 0x7E
SHEET_COLUMNS = 26
SHEET_ROWS = 4


class FontError(Exception):
    pass


def read_indexed_png(path: pathlib.Path) -> tuple[int, int, list[tuple[int, int, int]], bytes]:
    encoded = path.read_bytes()
    if not encoded.startswith(PNG_SIGNATURE):
        raise FontError(f"{path}: invalid PNG signature")

    offset = len(PNG_SIGNATURE)
    width = height = bit_depth = color_type = None
    palette: list[tuple[int, int, int]] | None = None
    compressed = bytearray()

    while offset < len(encoded):
        if offset + 12 > len(encoded):
            raise FontError(f"{path}: truncated PNG chunk")
        length = struct.unpack_from(">I", encoded, offset)[0]
        chunk_type = encoded[offset + 4 : offset + 8]
        data_start = offset + 8
        data_end = data_start + length
        crc_end = data_end + 4
        if crc_end > len(encoded):
            raise FontError(f"{path}: truncated PNG chunk payload")
        payload = encoded[data_start:data_end]
        expected_crc = struct.unpack_from(">I", encoded, data_end)[0]
        actual_crc = binascii.crc32(chunk_type + payload) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            name = chunk_type.decode("ascii", errors="replace")
            raise FontError(f"{path}: invalid CRC for {name} chunk")

        if chunk_type == b"IHDR":
            if length != 13:
                raise FontError(f"{path}: invalid IHDR size")
            (
                width,
                height,
                bit_depth,
                color_type,
                compression,
                filtering,
                interlace,
            ) = struct.unpack(">IIBBBBB", payload)
            if (bit_depth, color_type, compression, filtering, interlace) != (
                8,
                3,
                0,
                0,
                0,
            ):
                raise FontError(
                    f"{path}: expected non-interlaced 8-bit indexed PNG"
                )
        elif chunk_type == b"PLTE":
            if length == 0 or length % 3 != 0 or length > 768:
                raise FontError(f"{path}: invalid PLTE chunk")
            palette = [tuple(payload[i : i + 3]) for i in range(0, length, 3)]
        elif chunk_type == b"IDAT":
            compressed.extend(payload)
        elif chunk_type == b"IEND":
            break

        offset = crc_end

    if width is None or height is None or bit_depth is None or color_type is None:
        raise FontError(f"{path}: missing IHDR chunk")
    if palette is None:
        raise FontError(f"{path}: missing PLTE chunk")
    if not compressed:
        raise FontError(f"{path}: missing IDAT data")

    try:
        filtered = zlib.decompress(bytes(compressed))
    except zlib.error as error:
        raise FontError(f"{path}: invalid compressed pixels: {error}") from error

    row_size = width
    expected_size = height * (row_size + 1)
    if len(filtered) != expected_size:
        raise FontError(
            f"{path}: decoded {len(filtered)} bytes, expected {expected_size}"
        )

    pixels = bytearray(width * height)
    previous = bytearray(row_size)
    source_offset = 0
    for y in range(height):
        filter_type = filtered[source_offset]
        source_offset += 1
        source = filtered[source_offset : source_offset + row_size]
        source_offset += row_size
        current = bytearray(row_size)
        for x, value in enumerate(source):
            left = current[x - 1] if x > 0 else 0
            above = previous[x]
            upper_left = previous[x - 1] if x > 0 else 0
            if filter_type == 0:
                decoded = value
            elif filter_type == 1:
                decoded = (value + left) & 0xFF
            elif filter_type == 2:
                decoded = (value + above) & 0xFF
            elif filter_type == 3:
                decoded = (value + ((left + above) >> 1)) & 0xFF
            elif filter_type == 4:
                prediction = left + above - upper_left
                distance_left = abs(prediction - left)
                distance_above = abs(prediction - above)
                distance_upper_left = abs(prediction - upper_left)
                if distance_left <= distance_above and distance_left <= distance_upper_left:
                    paeth = left
                elif distance_above <= distance_upper_left:
                    paeth = above
                else:
                    paeth = upper_left
                decoded = (value + paeth) & 0xFF
            else:
                raise FontError(f"{path}: unsupported PNG filter {filter_type}")
            if decoded >= len(palette):
                raise FontError(f"{path}: palette index {decoded} is out of range")
            current[x] = decoded
        pixels[y * width : (y + 1) * width] = current
        previous = current

    return width, height, palette, bytes(pixels)


def extract_rows(
    width: int,
    height: int,
    palette: list[tuple[int, int, int]],
    pixels: bytes,
) -> list[tuple[int, ...]]:
    expected_width = SHEET_COLUMNS * CELL_WIDTH
    expected_height = SHEET_ROWS * CELL_HEIGHT
    if (width, height) != (expected_width, expected_height):
        raise FontError(
            f"font sheet is {width}x{height}, expected {expected_width}x{expected_height}"
        )

    cells: list[tuple[int, ...]] = []
    for cell_y in range(SHEET_ROWS):
        for cell_x in range(SHEET_COLUMNS):
            glyph = []
            for row in range(CELL_HEIGHT):
                bits = 0
                for column in range(CELL_WIDTH):
                    x = cell_x * CELL_WIDTH + column
                    y = cell_y * CELL_HEIGHT + row
                    red, green, blue = palette[pixels[y * width + x]]
                    luminance = red * 299 + green * 587 + blue * 114
                    if luminance < 128000:
                        bits |= 1 << (CELL_WIDTH - 1 - column)
                glyph.append(bits)
            cells.append(tuple(glyph))
    return cells


def read_order(path: pathlib.Path) -> str:
    try:
        text = path.read_text(encoding="ascii")
    except UnicodeDecodeError as error:
        raise FontError(f"{path}: glyph order must be ASCII") from error
    lines = text.splitlines()
    if len(lines) != SHEET_ROWS:
        raise FontError(f"{path}: expected {SHEET_ROWS} lines, found {len(lines)}")
    for line_number, line in enumerate(lines, start=1):
        if len(line) != SHEET_COLUMNS:
            raise FontError(
                f"{path}:{line_number}: expected {SHEET_COLUMNS} cells, found {len(line)}"
            )
    return "".join(lines)


def build_font(order: str, cells: list[tuple[int, ...]]) -> tuple[tuple[int, ...], list[tuple[int, ...]]]:
    if len(order) != len(cells):
        raise FontError("glyph order and sheet cell counts differ")

    cursor = cells[0]
    if cursor == (0,) * CELL_HEIGHT:
        raise FontError("the first sheet cell must contain the cursor")

    glyphs: dict[str, tuple[int, ...]] = {}
    blank = (0,) * CELL_HEIGHT
    for index, (character, glyph) in enumerate(zip(order[1:], cells[1:]), start=1):
        if character == " ":
            if glyph != blank:
                raise FontError(f"space cell {index} contains ink")
            glyphs.setdefault(character, glyph)
            continue
        if not 0x20 <= ord(character) <= 0x7E:
            raise FontError(f"cell {index} contains non-printable character")
        if character in glyphs:
            raise FontError(f"character {character!r} appears more than once")
        if glyph == blank:
            raise FontError(f"character {character!r} has an empty glyph")
        glyphs[character] = glyph

    if " " not in glyphs or "?" not in glyphs:
        raise FontError("the sheet must define blank space and question mark")

    fallback = glyphs["?"]
    ascii_glyphs = [
        glyphs.get(chr(codepoint), fallback)
        for codepoint in range(ASCII_FIRST, ASCII_LAST + 1)
    ]
    return cursor, ascii_glyphs


def format_rows(rows: tuple[int, ...]) -> str:
    return ", ".join(f"0x{value:02x}" for value in rows)


def header_text(cursor: tuple[int, ...], glyphs: list[tuple[int, ...]]) -> str:
    lines = [
        "/* Generated by scripts/generate-font-4x8.py. Do not edit. */",
        "#ifndef MIGA80_GENERATED_FONT4X8_DATA_H",
        "#define MIGA80_GENERATED_FONT4X8_DATA_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define MIGA80_FONT4X8_FIRST {ASCII_FIRST}U",
        f"#define MIGA80_FONT4X8_LAST {ASCII_LAST}U",
        f"#define MIGA80_FONT4X8_WIDTH {CELL_WIDTH}U",
        f"#define MIGA80_FONT4X8_HEIGHT {CELL_HEIGHT}U",
        "",
        "static const uint8_t miga80_font4x8_cursor[MIGA80_FONT4X8_HEIGHT] = {",
        f"    {format_rows(cursor)}",
        "};",
        "",
        "static const uint8_t miga80_font4x8_ascii",
        "    [MIGA80_FONT4X8_LAST - MIGA80_FONT4X8_FIRST + 1U]",
        "    [MIGA80_FONT4X8_HEIGHT] = {",
    ]
    for codepoint, rows in enumerate(glyphs, start=ASCII_FIRST):
        printable = chr(codepoint)
        if printable == "\\":
            printable = "backslash"
        elif printable == "'":
            printable = "apostrophe (fallback)"
        elif printable == '"':
            printable = "quote (fallback)"
        elif printable == "`":
            printable = "backtick (fallback)"
        lines.append(
            f"    {{ {format_rows(rows)} }}, /* 0x{codepoint:02x} {printable} */"
        )
    lines.extend(["};", "", "#endif", ""])
    return "\n".join(lines)


def binary_data(cursor: tuple[int, ...], glyphs: list[tuple[int, ...]]) -> bytes:
    header = b"M8F4" + bytes(
        [1, CELL_WIDTH, CELL_HEIGHT, ASCII_FIRST, ASCII_LAST, 0, 0, 0]
    )
    flattened = bytes(cursor) + b"".join(bytes(glyph) for glyph in glyphs)
    return header + flattened


def write_atomic(path: pathlib.Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(content)
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=pathlib.Path)
    parser.add_argument("order", type=pathlib.Path)
    parser.add_argument("--header", type=pathlib.Path)
    parser.add_argument("--binary", type=pathlib.Path)
    arguments = parser.parse_args()
    if arguments.header is None and arguments.binary is None:
        parser.error("at least one of --header or --binary is required")

    try:
        width, height, palette, pixels = read_indexed_png(arguments.image)
        cells = extract_rows(width, height, palette, pixels)
        order = read_order(arguments.order)
        cursor, glyphs = build_font(order, cells)
        if arguments.header is not None:
            write_atomic(arguments.header, header_text(cursor, glyphs).encode("ascii"))
        if arguments.binary is not None:
            write_atomic(arguments.binary, binary_data(cursor, glyphs))
    except (FontError, OSError) as error:
        print(f"font conversion failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
