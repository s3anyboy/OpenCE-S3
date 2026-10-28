#!/usr/bin/env python3

# Checks a Shadowrun prototype cache file's compression layout: whether the
# bytes at the file's data offset are a zlib stream, how many bytes inflate
# from there, and how that compares with the header's fields. Used to find
# what a build's header means by compressed_length when the decompressor
# reports "invalid block type".

import argparse
import struct
import sys
import zlib
from pathlib import Path


def header(data: bytes) -> dict:
    return {
        "version": struct.unpack_from("<i", data, 0x04)[0],
        "file_length": struct.unpack_from("<I", data, 0x08)[0],
        "compressed_length": struct.unpack_from("<I", data, 0x0C)[0],
        "tag_data_offset": struct.unpack_from("<I", data, 0x10)[0],
        "tag_data_size": struct.unpack_from("<I", data, 0x14)[0],
        "name": data[0x20:0x40].split(b"\0")[0].decode("ascii", "replace"),
        "build": data[0x40:0x60].split(b"\0")[0].decode("ascii", "replace"),
        "scenario_type": struct.unpack_from("<h", data, 0x60)[0],
    }


def inflate_from(data: bytes, offset: int, limit: int) -> tuple:
    """the bytes a zlib stream at `offset` makes, and whether it ended cleanly"""
    stream = zlib.decompressobj()
    out = bytearray()
    position = offset
    try:
        while position < len(data):
            chunk = data[position:position + 0x10000]
            out += stream.decompress(chunk)
            position += len(chunk)
            if stream.eof:
                break
    except zlib.error as error:
        return bytes(out), False, str(error)
    return bytes(out), stream.eof, ""


def probe(path: Path) -> int:
    data = path.read_bytes()
    fields = header(data)
    print(f"{path.name}: build {fields['build']}, type {fields['scenario_type']}")
    print(f"  header.file_length     = 0x{fields['file_length']:X} ({fields['file_length']})")
    print(f"  header.compressed_len  = 0x{fields['compressed_length']:X} ({fields['compressed_length']})")
    print(f"  header.tag_data_offset = 0x{fields['tag_data_offset']:X}")
    print(f"  header.tag_data_size   = 0x{fields['tag_data_size']:X}")
    print(f"  file size              = 0x{len(data):X} ({len(data)})")

    for offset in (0x800,):
        if offset >= len(data):
            continue
        first = data[offset:offset + 2].hex()
        print(f"  @0x{offset:X}: first bytes {first}", end="")
        out, ended, error = inflate_from(data, offset, 0)
        if ended:
            print(f" -> zlib stream OK, {len(out)} bytes out")
            print(f"     matches file_length {fields['file_length']}: {len(out) == fields['file_length']}")
            print(f"     matches file_length-0x800: {len(out) == fields['file_length'] - 0x800}")
        elif out:
            print(f" -> zlib ended early: {len(out)} bytes out, {error}")
        else:
            print(f" -> not a zlib stream: {error}")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="probe a Shadowrun cache file's compression layout")
    parser.add_argument("maps", type=Path, nargs="+")
    args = parser.parse_args()

    for path in args.maps:
        if not path.is_file():
            print(f"{path}: not found", file=sys.stderr)
            continue
        probe(path)

    return 0


if __name__ == "__main__":
    sys.exit(main())
