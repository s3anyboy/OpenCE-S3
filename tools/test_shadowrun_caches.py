"""Tests for running the Shadowrun prototype's maps: Xbox caches of another
build of the engine, whose tags and structure BSPs are linked higher than
this build's (port/linux/game/cache_file_formats.c, docs/shadowrun_caches.md).

A small driver calls the format module's functions on tag data built here,
so no game data is needed or stored: where tag data is linked
(xbox_cache_tag_data_address), where a structure BSP loads
(xbox_cache_structure_bsp_address), and the conversion of Shadowrun's actor
types (shadowrun_cache_convert). The driver is compiled with clang, warnings
as errors and undefined behaviour trapping, for 32 and 64 bits, so a crash or
an undefined operation on malformed input fails the test that caused it.

When the Shadowrun prototype's maps are present (HALO_SHADOWRUN_MAPS, a folder
of them, and build/linux/map_validate built), test_real_maps checks each as
the game loads it: its tags linked where it says (0x8069E000, or
recomp's 0x806D0000), its structure BSPs moved and their pointers with them,
and nothing refused.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "port/linux/game/cache_file_formats.c"

XBOX_TAG_CACHE = 0x803A6000
XBOX_TAG_CACHE_BYTES = 0x01600000
SHADOWRUN_TAG_DATA = 0x8069E000
TAG_HEADER_BYTES = 0x24
TAG_INSTANCE_BYTES = 0x20

STRICT_FLAGS = ["-Wall", "-Wextra", "-Wpedantic", "-Werror"]
UB_TRAP_FLAGS = ["-fsanitize=undefined", "-fsanitize-trap=undefined"]

DRIVER = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cache_file_formats.h"

/* the tag header's signature, as a map holds it, which this build reads */
#define THIS_BUILDS_SIGNATURE 0x73676174UL

/* usage: driver address TAG_DATA_FILE CACHE_ADDRESS CACHE_BYTES
          driver shadowrun TAG_DATA_FILE ADDRESS (any of Shadowrun's builds')
          driver bsp LINKED READ FREE_ADDRESS FREE_BYTES
          driver convert TAG_DATA_FILE ADDRESS (writes the converted data back)
          driver header HEADER_FILE */


static unsigned char *slurp(char const *path, unsigned long *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *bytes;
	long length;

	if (!file || fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET))
		exit(2);
	bytes = malloc(length ? (size_t)length : 1);
	if (!bytes || (length && fread(bytes, (size_t)length, 1, file) != 1))
		exit(2);
	fclose(file);
	*size = (unsigned long)length;
	return bytes;
}

int main(int argc, char **argv)
{
	uint32_t result = 0;
	enum cache_file_status status;

	if (argc == 5 && !strcmp(argv[1], "address"))
	{
		unsigned long size;
		unsigned char *bytes = slurp(argv[2], &size);

		status = xbox_cache_tag_data_address(bytes, (uint32_t)size, (uint32_t)strtoul(argv[3], NULL, 0),
			(uint32_t)strtoul(argv[4], NULL, 0), &result);
		free(bytes);
	}
	else if (argc == 3 && !strcmp(argv[1], "shadowrun"))
	{
		unsigned long size;
		unsigned char *bytes = slurp(argv[2], &size);
		uint32_t signature = (uint32_t)bytes[0x20] | ((uint32_t)bytes[0x21] << 8) |
			((uint32_t)bytes[0x22] << 16) | ((uint32_t)bytes[0x23] << 24);

		/* (a map's tag data, its tag header at its start: a map that holds
		it as "tags" is not the prototype's, and one whose other four bytes
		are the prototype's is) */
		result = signature != THIS_BUILDS_SIGNATURE &&
			(signature == SHADOWRUN_TAG_SIGNATURE_GROUP_TAG || signature == SHADOWRUN_TAG_SIGNATURE_GROUP_TAG_SWAPPED);
		free(bytes);
		status = _cache_file_status_ok;
	}
	else if (argc == 3 && !strcmp(argv[1], "signature"))
	{
		unsigned long size;
		unsigned char *bytes = slurp(argv[2], &size);

		/* (the tag header the map has, as the loader's check of it reads it) */
		result = (uint32_t)shadowrun_cache_tag_header_signature(bytes) ||
			((uint32_t)bytes[0x20] | ((uint32_t)bytes[0x21] << 8) | ((uint32_t)bytes[0x22] << 16) |
				((uint32_t)bytes[0x23] << 24)) == THIS_BUILDS_SIGNATURE;
		free(bytes);
		status = _cache_file_status_ok;
	}
	else if (argc == 6 && !strcmp(argv[1], "bsp"))
	{
		status = xbox_cache_structure_bsp_address((uint32_t)strtoul(argv[2], NULL, 0),
			(uint32_t)strtoul(argv[3], NULL, 0), (uint32_t)strtoul(argv[4], NULL, 0),
			(uint32_t)strtoul(argv[5], NULL, 0), &result);
	}
	else if (argc == 4 && !strcmp(argv[1], "convert"))
	{
		struct shadowrun_conversion_report report;
		unsigned long size;
		unsigned char *bytes = slurp(argv[2], &size);
		FILE *file;

		status = shadowrun_cache_convert(bytes, (uint32_t)size, (uint32_t)strtoul(argv[3], NULL, 0), &report);
		result = (uint32_t)report.actors_retyped;
		file = fopen(argv[2], "wb");
		if (!file || (size && fwrite(bytes, size, 1, file) != 1) || fclose(file))
			return 2;
		free(bytes);
	}
	else if (argc == 3 && !strcmp(argv[1], "header"))
	{
		unsigned long size;
		unsigned char *bytes = slurp(argv[2], &size);

		result = (uint32_t)shadowrun_cache_header_campaign(bytes, (uint32_t)size);
		status = _cache_file_status_ok;
		free(bytes);
	}
	else
	{
		return 2;
	}
	printf("%s %08lx\n", cache_file_status_describe(status), (unsigned long)result);
	return 0;
}
"""


def find_clang():
    for candidate in (shutil.which("clang"), r"C:\Program Files\LLVM\bin\clang.exe"):
        if candidate and Path(candidate).is_file():
            return candidate
    return None


def target_flag_sets():
    if sys.platform == "win32":
        return [["--target=i686-pc-windows-msvc", "-fuse-ld=lld", "-D_CRT_SECURE_NO_WARNINGS"]]
    # the game is 32-bit; the module is written for any width
    return [["-m32"], []]


@pytest.fixture(scope="module", params=target_flag_sets(), ids=lambda flags: " ".join(flags) or "native")
def driver(request, tmp_path_factory):
    clang = find_clang()
    if clang is None:
        pytest.skip("clang is needed to build the driver")
    directory = tmp_path_factory.mktemp("shadowrun")
    source = directory / "driver.c"
    source.write_text(DRIVER)
    output = directory / ("driver.exe" if sys.platform == "win32" else "driver")
    command = [clang, "-std=c99", "-O1", *STRICT_FLAGS, *UB_TRAP_FLAGS, *request.param,
               f"-I{MODULE.parent}", str(source), str(MODULE), "-o", str(output)]
    built = subprocess.run(command, capture_output=True, text=True)
    if built.returncode != 0:
        if request.param == ["-m32"] and "cannot find" in built.stderr:
            pytest.skip("no 32-bit C library to link with")
        pytest.fail(built.stderr)

    def run(*arguments):
        completed = subprocess.run([str(output), *map(str, arguments)], capture_output=True, text=True)
        assert completed.returncode == 0, completed
        status, value = completed.stdout.strip().rsplit(" ", 1)
        return status, int(value, 16)

    run.directory = directory
    return run


def tag_data(address, instances=(), tail=b"", signature=b"sgat"):
    """Tag data as a cache file holds it, linked to address: the header (its
    signature's four bytes as a map holds them: "sgat", the literal 'tags'
    the game compares it with), its tag table of (group, definition address)
    instances,
    then tail (the definitions, at address + TAG_HEADER_BYTES + the table's
    size)."""
    table = address + TAG_HEADER_BYTES
    header = struct.pack("<IiIi", table, 0, 0, len(instances)) + b"\0" * 16 + signature
    entries = b"".join(
        struct.pack("<4sIIIIIII", group[::-1].encode("latin-1"), 0xFFFFFFFF, 0xFFFFFFFF, 0xE1740000 + index,
                    0, definition, 0, 0)
        for index, (group, definition) in enumerate(instances))
    return header + entries + tail


def write(driver, name, data):
    path = driver.directory / name
    path.write_bytes(data)
    return path


# ---------- where tag data is linked


def test_this_builds_maps_are_linked_to_the_tag_caches_base(driver):
    path = write(driver, "retail.bin", tag_data(XBOX_TAG_CACHE))
    assert driver("address", path, XBOX_TAG_CACHE, XBOX_TAG_CACHE_BYTES) == ("ok", XBOX_TAG_CACHE)


def test_shadowrun_maps_are_linked_higher_in_the_tag_cache(driver):
    path = write(driver, "shadowrun.bin", tag_data(SHADOWRUN_TAG_DATA))
    assert driver("address", path, XBOX_TAG_CACHE, XBOX_TAG_CACHE_BYTES) == ("ok", SHADOWRUN_TAG_DATA)


RECOMP_TAG_DATA = 0x806D0000


def test_a_map_linked_higher_still_is(driver):
    # recomp.map: the same tags, linked where a larger game state ends
    path = write(driver, "recomp.bin", tag_data(RECOMP_TAG_DATA))
    assert driver("address", path, XBOX_TAG_CACHE, XBOX_TAG_CACHE_BYTES) == ("ok", RECOMP_TAG_DATA)


# ---------- which maps are Shadowrun's


def test_the_prototypes_maps_are_shadowruns(driver):
    # (missionlet's tags, and recomp's, which are the same: where a map links
    # them is not what makes it the prototype's, its signature is)
    for address in (SHADOWRUN_TAG_DATA, RECOMP_TAG_DATA):
        path = write(driver, "missionlet.bin", tag_data(address, signature=b"sgat"))
        assert driver("shadowrun", path) == ("ok", 1), hex(address)


def test_a_map_with_this_builds_signature_is_not_shadowruns(driver):
    # (the bytes the literal 'tags' has, which no map holds as they come: a
    # map holds them the other way, "sgat")
    path = write(driver, "retail.bin", tag_data(XBOX_TAG_CACHE, signature=b"tags"))
    assert driver("shadowrun", path) == ("ok", 0)


# ---------- the tag header the game reads


def test_a_tag_header_this_build_reads_has_its_signature(driver):
    # (every map, this build's and the prototype's, holds "sgat" there)
    path = write(driver, "signed.bin", tag_data(SHADOWRUN_TAG_DATA))
    assert driver("signature", path) == ("ok", 1)


@pytest.mark.parametrize("signature", [b"gat!", b"\0\0\0\0", b"\x1b\0\0\0"],
                         ids=["near", "zeroes", "a count"])
def test_other_tag_headers_are_not_this_builds(driver, signature):
    data = bytearray(tag_data(SHADOWRUN_TAG_DATA))
    data[0x20:0x24] = signature
    path = write(driver, "unsigned.bin", bytes(data))
    assert driver("signature", path) == ("ok", 0)


@pytest.mark.parametrize("table", [
    XBOX_TAG_CACHE - 4,                                   # below the tag cache
    XBOX_TAG_CACHE + TAG_HEADER_BYTES - 1,               # the header would start before it
    SHADOWRUN_TAG_DATA + TAG_HEADER_BYTES + 2,           # not aligned
    XBOX_TAG_CACHE + XBOX_TAG_CACHE_BYTES,               # past its end
    XBOX_TAG_CACHE + XBOX_TAG_CACHE_BYTES - 0x10,        # the data would run past its end
    0xFFFFFFFF,                                           # no wrapping around
    0,
])
def test_tag_data_linked_outside_the_tag_cache_is_refused(driver, table):
    data = bytearray(tag_data(SHADOWRUN_TAG_DATA) + b"\0" * 0x100)
    struct.pack_into("<I", data, 0, table)
    path = write(driver, "outside.bin", bytes(data))
    status, _ = driver("address", path, XBOX_TAG_CACHE, XBOX_TAG_CACHE_BYTES)
    assert status != "ok"


def test_tag_data_too_small_for_its_header_is_refused(driver):
    path = write(driver, "small.bin", tag_data(XBOX_TAG_CACHE)[:TAG_HEADER_BYTES - 1])
    status, _ = driver("address", path, XBOX_TAG_CACHE, XBOX_TAG_CACHE_BYTES)
    assert status != "ok"


# ---------- where a structure BSP loads


def test_a_bsp_that_fits_where_it_is_linked_stays_there(driver):
    assert driver("bsp", 0x81000000, 0x100000, 0x80900000, 0x819A6000 - 0x80900000) == ("ok", 0x81000000)


def test_missionlets_bsp_loads_after_its_tags_keeping_its_page_offset(driver):
    # missionlet.map: tags at 0x8069E000 (0xFFE834 bytes), its BSP linked to
    # 0x81EBB800, past the tag cache (0x819A6000), 0x15F800 bytes read
    free = SHADOWRUN_TAG_DATA + 0xFFE834
    status, address = driver("bsp", 0x81EBB800, 0x15F800, free, 0x819A6000 - free)
    assert (status, address) == ("ok", 0x8169D800)
    assert address & 0xFFF == 0x800 and address >= free and address + 0x15F800 <= 0x819A6000


def test_b30srs_bsps_load_clear_of_where_they_are_linked(driver):
    # b30sr.map: tags at 0x806D0000 to 0x82504878, its BSPs linked inside them
    # (b30a 0x64A000 bytes at 0x82086000, b30b 0x3D4800 at 0x822FB800). The
    # first room after the tags, 0x82505000, overlaps b30a's linked range, and
    # a BSP moved onto that cannot have its pointers moved
    # (tag_validate_relocate_structure_bsp): each loads past it instead
    free, end = 0x82504878, 0x82E00000
    for linked, read, expected in ((0x82086000, 0x64A000, 0x826D0000), (0x822FB800, 0x3D4800, 0x826D0800)):
        status, address = driver("bsp", linked, read, free, end - free)
        assert (status, address) == ("ok", expected)
        assert address >= linked + read and address + read <= end
    # (and with less room than that, refused rather than overlapping)
    status, _ = driver("bsp", 0x82086000, 0x64A000, free, 0x82C00000 - free)
    assert status != "ok"


@pytest.mark.parametrize("linked, read, free, room", [
    (0x81EBB800, 0x400000, 0x8169C834, 0x819A6000 - 0x8169C834),  # too large for the room
    (0x81EBB800, 0, 0x8169C834, 0x100000),                          # nothing to read
    (0x81EBB800, 0x1000, 0xFFFFFF00, 0x100),                        # no wrapping around
    (0x81EBB800, 0x1000, 0xFFFFF000, 0xFFF),
])
def test_a_bsp_that_fits_nowhere_is_refused(driver, linked, read, free, room):
    status, _ = driver("bsp", linked, read, free, room)
    assert status != "ok"


# ---------- Shadowrun's actor types


def actors(types, extra=()):
    """Tag data linked to 0x8069E000 with an actor definition of each type, and
    extra (group, address) instances after them."""
    count = len(types) + len(extra)
    first = SHADOWRUN_TAG_DATA + TAG_HEADER_BYTES + count * TAG_INSTANCE_BYTES
    definitions = b"".join(b"\0" * 0x14 + struct.pack("<h", type_) + b"\0" * 0x0A for type_ in types)
    instances = [("actr", first + index * 0x20) for index in range(len(types))] + list(extra)
    return tag_data(SHADOWRUN_TAG_DATA, instances, definitions)


def actor_types(data, count):
    """the types of the count actor definitions at the end of data"""
    return [struct.unpack_from("<h", data, len(data) - count * 0x20 + index * 0x20 + 0x14)[0]
            for index in range(count)]


def test_shadowrun_actor_types_get_this_builds_nearest(driver):
    types = [16, 17, 18, 19, 20, 21, 22, 23, 0, 7, 15, 24, -1]
    path = write(driver, "actors.bin", actors(types))
    assert driver("convert", path, SHADOWRUN_TAG_DATA) == ("ok", 8)
    # hologram, dog, ninja, mook, guard, necro, dwarf, the unnamed one; then
    # this build's own types and values past Shadowrun's, which the tag
    # validator corrects, untouched
    assert actor_types(path.read_bytes(), len(types)) == [7, 9, 0, 7, 7, 7, 7, 7, 0, 7, 15, 24, -1]


def test_converting_twice_changes_nothing_more(driver):
    path = write(driver, "twice.bin", actors([16, 17, 18]))
    driver("convert", path, SHADOWRUN_TAG_DATA)
    once = path.read_bytes()
    assert driver("convert", path, SHADOWRUN_TAG_DATA) == ("ok", 0)
    assert path.read_bytes() == once


def test_definitions_outside_the_tag_data_are_left_to_the_validator(driver):
    data = actors([18], extra=[("actr", 0xFFFFFFF0), ("actr", SHADOWRUN_TAG_DATA - 0x100), ("weap", 0)])
    path = write(driver, "outside_actors.bin", data)
    assert driver("convert", path, SHADOWRUN_TAG_DATA) == ("ok", 1)


@pytest.mark.parametrize("change", ["signature", "count", "huge count", "table"])
def test_a_damaged_tag_table_is_refused(driver, change):
    data = bytearray(actors([16, 17]))
    if change == "signature":
        data[0x20:0x24] = b"head"
    elif change == "count":
        struct.pack_into("<i", data, 0x0C, -1)
    elif change == "huge count":
        struct.pack_into("<i", data, 0x0C, 0x7FFFFFFF)
    else:
        struct.pack_into("<I", data, 0x00, SHADOWRUN_TAG_DATA + len(data))
    path = write(driver, "damaged.bin", bytes(data))
    status, _ = driver("convert", path, SHADOWRUN_TAG_DATA)
    assert status != "ok"


# ---------- the map lists' check of a header


def cache_header(build=b"01.12.07.0132", version=5, scenario_type=0, head=b"daeh", foot=b"toof", size=0x800):
    header = bytearray(size)
    header[0:4] = head
    struct.pack_into("<i", header, 4, version)
    header[0x20:0x20 + 10] = b"missionlet"
    header[0x40:0x40 + len(build)] = build
    struct.pack_into("<h", header, 0x60, scenario_type)
    if size >= 0x800:
        header[0x7FC:0x800] = foot
    return bytes(header)


def test_a_shadowrun_campaign_maps_header_is_listed(driver):
    path = write(driver, "header.bin", cache_header())
    assert driver("header", path) == ("ok", 1)


@pytest.mark.parametrize("header", [
    cache_header(build=b"01.10.12.2276"),    # this build's maps' (and an edited copy's)
    cache_header(build=b"01.12.07.01320"),   # longer
    cache_header(build=b"01.12.07.013"),     # shorter
    cache_header(scenario_type=1),           # multiplayer
    cache_header(scenario_type=2),           # the menus (ui.map)
    cache_header(version=609),               # Custom Edition
    cache_header(head=b"head"),
    cache_header(foot=b"foot"),
    cache_header(size=0x7FF),
], ids=["retail build", "longer build", "shorter build", "multiplayer", "ui", "custom edition",
        "head", "foot", "short"])
def test_other_headers_are_not_listed(driver, header):
    path = write(driver, "other_header.bin", header)
    assert driver("header", path) == ("ok", 0)


# ---------- the real maps


def shadowrun_maps():
    folder = os.environ.get("HALO_SHADOWRUN_MAPS")
    return sorted(Path(folder).glob("*.map")) if folder and Path(folder).is_dir() else []


@pytest.mark.skipif(not shadowrun_maps(), reason="HALO_SHADOWRUN_MAPS names no folder of the prototype's maps")
@pytest.mark.parametrize("path", shadowrun_maps(), ids=lambda path: path.name)
def test_real_maps(path):
    validator = ROOT / "build/linux/map_validate"
    if not validator.is_file():
        pytest.skip("build/linux/map_validate is not built (ninja linux)")
    completed = subprocess.run([str(validator), str(path)], capture_output=True, text=True)
    assert completed.returncode == 0, completed.stdout
    # (missionlet's and ui.map's to 8069e000, recomp's to 806d0000)
    assert any(f"its tag data is linked to {address}" in completed.stdout
               for address in ("8069e000", "806d0000")), completed.stdout
    assert "a Shadowrun prototype map" in completed.stdout, completed.stdout
    assert "cannot be moved" not in completed.stdout
    # (what the schema names is every pointer of a bsp: what still looks
    # like one where it was linked is a number or two among its vertices)
    for line in completed.stdout.splitlines():
        if "still has" in line:
            assert int(line.split("still has ")[1].split()[0]) <= 4, line
