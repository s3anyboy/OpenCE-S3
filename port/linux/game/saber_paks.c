/*
SABER_PAKS.C

Halo: Combat Evolved Anniversary's Saber3D packages (.s3dpak), and the
templates (its remastered models) and textures in them, read for
saber_models.c.

A package is an entry directory and the entries' data, one stream:

	u32 entry count
	each entry: u32 offset, u32 size, u32 name length, the name, u32 type,
	two u32 (0)
	the entries' data, at their offsets from the stream's start

The Xbox 360's packages (paks360) compress the stream in blocks: a u32
block count, each block's offset in the file, then the blocks, each an
XMemCompress stream (saber_lzx.c). The Master Chief Collection's packages,
and its .ipak texture archives, compress the stream in zlib chunks: a u32
chunk count, each chunk's offset in the file, and at that offset a u32 of
the chunk's uncompressed size (128 KB, less for the last) then a zlib
stream (port/linux/src/zlib_inflate.c). As Reclaimer tells a 360 package
from an uncompressed one: a package whose fourth u32 is no name length (0
or less, or over 1024) is compressed. A chunk whose first bytes are a zlib
header is the Collection's, and the 360's blocks are not. Blocks are read
as their entries are, and the last one read is kept.

An .ipak's stream is not that entry directory. It is a u32 count, a u32 0,
then one 328-byte record a texture: a 264-byte name, and at 308 and 312
the size and offset of its pixels. A 58-byte TCIP header stands in front
of those pixels (Sabre-unPacker's ipak). Each record becomes an entry of
type Textures, so a texture is found as one in a package is.

A template is blocks, each a u16 type (big-endian), the offset of its end in
the template (i32, little-endian) and its contents, some of them blocks of
their own; its numbers are little-endian, but for its texture coordinates
on the Xbox 360, which are big-endian. What is read of one, as Reclaimer
(GPL-3.0) reads it:

	0xE402 the template: its blocks
	0x5501 its materials: an i32 count, then a 0x5601 block of each name
	0xF000 a node: its blocks, and the nodes under it
		0xB903 its mesh: its name, an i16 id ...
		0xF900 its transform, 16 reals (row vectors: its parent's is after)
		0x2B01 its parent's id
		0x2E01 its mesh's flags (an i16, then the flags: compressed
		positions, normals)
		0x2901 a level mesh drawn of another's buffers (its sharingObj):
		that mesh's id, then the first vertex and triangle there
		0x3501 a level mesh's placement: int16 translation then int16
		scale of each axis, applied after its (or its source's) positions
		0xF100 its positions: an i32 count; compressed, the i16 translation
		and scale of each axis, then four i16 a vertex (three of its
		position, of 32767 to the scale, and one its normal); otherwise
		three reals a vertex
		0x3001 its vertices' data: an i32 count, 8 bytes, the size of a
		vertex's data (a byte), then each vertex's data, two i16 of its
		texture coordinates (of 32767) where its size says
		0xF200 its triangles: an i32 count, three u16 each
		0x0701 its submeshes: in its 0x0401 (after an i32 count), for each
		a 0x0501 (its first triangle and count), and a 0x0B01 whose first
		0x0E01 is the material its base layer is drawn with
	0xEA01 a level's placed objects: an i32 count, then that many 0xE402
		templates (a skydome, the cloud layers). Each one's 0xE502 is
		"LPTA" and its name. Its materials are the level's.
	A mesh named .lod1, .lod2 ... is the template's at a lower level of
	detail.

Saber3D's axes are forward x, right z and up y, Halo's forward x, left y
and up z (a warthog's left wheels are at Saber3D's -z, at Halo's +y): a
point (x, y, z) of the template is Halo's (x, -z, y), turned, not mirrored.
Its triangles are turned to face as Halo's do (clockwise about the normals
the template gives). A template's own units vary (Halo's are not
among them): saber_models.c scales them to the map's model.

A texture (TexturesMips64, Textures) starts "TCIP" at 6 (little-endian:
its width at 16, height at 20, format at 38, mipmap count at 48, the pixels
at 58), or "PICT" at 8 (big-endian, the Xbox 360's streamed ones: width at
12, height at 16, format at 32, the pixels at 4096, their 16-bit words
byte-swapped, one mipmap). Only DXT textures (12 and 13 DXT1, 15 DXT3, 17
DXT5; the PC's 70, 73 and 76) are read.
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "rasterizer/rasterizer_model_types.h"
#include "saber.h"

#include <stdlib.h>
#include <string.h>

/* the platform layer's (port_config.c): this machine's files, by their own
paths (the game's fopen takes the Xbox's) */
void *config_host_file_open(const char *path, unsigned long *size);
int config_host_file_read(void *file, unsigned long offset, void *buffer, unsigned long size);
void config_host_file_close(void *file);
/* one zlib stream (port/linux/src/zlib_inflate.c): the bytes written, or -1 */
long port_zlib_inflate(void const *source, unsigned long source_size, void *destination,
	unsigned long destination_size);

/* ---------- constants */

/* the LZX window of a block (128 KB), and the largest a block can be
uncompressed and compressed */
#define WINDOW_BITS 17
#define FRAME_SIZE 32768UL
#define MAXIMUM_BLOCK_SIZE (1UL << WINDOW_BITS)
#define MAXIMUM_COMPRESSED_BLOCK_SIZE (2UL * MAXIMUM_BLOCK_SIZE)
#define MAXIMUM_BLOCKS 0x10000
#define MAXIMUM_ENTRIES 0x10000
#define MAXIMUM_NAME_LENGTH 1024
#define MAXIMUM_ENTRY_SIZE (256UL * 1024 * 1024)
/* an .ipak texture record: its name, then the texture's size and offset */
#define IPAK_RECORD_SIZE 328
#define IPAK_NAME_SIZE 264
#define IPAK_SIZE_AT 308
#define IPAK_OFFSET_AT 312
/* a TCIP header in front of the pixels the record's size counts */
#define IPAK_HEADER_SIZE 58

/* a template's blocks */
enum
{
	_block_template = 0xE402,
	/* a placed object's name: "LPTA" and then the name */
	_block_object_name = 0xE502,
	/* a level's placed objects: an i32 count, then that many templates */
	_block_objects = 0xEA01,
	_block_materials = 0x5501,
	_block_material = 0x5601,
	_block_node = 0xF000,
	_block_mesh = 0xB903,
	_block_transform = 0xF900,
	_block_parent = 0x2B01,
	_block_mesh_source = 0x2901,
	_block_mesh_bounds = 0x1D01,
	_block_scene_object_bounds = 0x3501,
	_block_mesh_flags = 0x2E01,
	/* a node's text properties, among them its texture coordinates' compression */
	_block_properties = 0x1501,
	_block_positions = 0xF100,
	_block_vertex_data = 0x3001,
	_block_triangles = 0xF200,
	_block_submeshes = 0x0701,
	_block_submesh_list = 0x0401,
	_block_triangle_range = 0x0501,
	_block_submesh_materials = 0x0B01,
	_block_material_index = 0x0E01,
	_block_bone_index = 0xFA00,
	_block_blend = 0x1601,
	_block_blend_indices = 0x3301,
	_block_blend_weights = 0x1A01,
};

enum
{
	_mesh_compressed_bit = 0,
	_mesh_normals_bit,
};

#define MAXIMUM_NODES 16384
#define MAXIMUM_NODE_DEPTH 64
#define MAXIMUM_SUBMESHES 256

/* the most triangles a part is made of, and vertices it has: its strip
(saber_models.c) is at most six indices a triangle, within
RASTERIZER_MAXIMUM_TRIANGLES_PER_TRIANGLE_BUFFER */
#define MAXIMUM_PART_TRIANGLES 4000
#define MAXIMUM_PART_VERTICES 0xFFFF

/* bitmaps.c's formats */
enum
{
	_bitmap_format_dxt1 = 14,
	_bitmap_format_dxt3 = 15,
	_bitmap_format_dxt5 = 16,
};

/* ---------- structures */

struct pak_entry
{
	unsigned long offset;
	unsigned long size;
	long type;
	char *name;
};

struct saber_pak
{
	void *file;
	unsigned long file_size;
	int compressed;
	/* the 360's LZX blocks, or the Collection's zlib chunks */
	int xbox360;
	int zlib_chunks;
	/* the blocks' offsets in the file, and in the stream (one more than
	there are blocks, the ends) */
	long block_count;
	unsigned long *block_offsets;
	unsigned long *block_starts;
	struct saber_lzx_state *lzx;
	unsigned char *window;
	unsigned char *compressed_block;
	/* the block read last, and its bytes */
	long cached_block;
	unsigned char *block;
	unsigned long stream_size;
	long entry_count;
	struct pak_entry *entries;
	char *names;
};

/* a template's block: its type, and where its contents start and end */
struct block
{
	unsigned long type;
	unsigned long start;
	unsigned long end;
};

/* a node of a template with a mesh */
struct template_node
{
	short id;
	long parent_id;
	real transform[16];
	struct block positions;
	struct block vertex_data;
	struct block triangles;
	struct block submeshes;
	unsigned short mesh_flags;
	boolean has_mesh_flags;
	/* its level of detail, of its name (".lod1.c": 1; none, 0) */
	short lod;
	/* its name, its bone (NONE: it is none) and its mesh's blending */
	unsigned long name_start;
	unsigned long name_length;
	long bone_index;
	struct block blend;
	/* a level's mesh drawn of another's buffers (its "sharingObj"): that
	mesh's id (NONE: its own), its first vertex and triangle there, and its
	own counts of them */
	long source_id;
	unsigned long source_vertex;
	unsigned long source_triangle;
	long own_vertex_count;
	long own_triangle_count;
	/* a level mesh's placement over its vertices (0x3501: int16
	translation then int16 scale, applied after the source's compressed
	positions and before the node's transform, as Reclaimer does) */
	boolean has_scene_bounds;
	real scene_translation[3];
	real scene_scale[3];
	/* its texture coordinates were stored compressed (unit * tile + shift) */
	boolean has_uv_decompress;
	/* marked hidden (visibility_hidden): a pelican's engine screens, which
	sit outside the pods when they are drawn */
	boolean hidden;
	real uv_shift[2];
	real uv_tile[2];
};

struct template_walk
{
	unsigned char const *bytes;
	unsigned long size;
	struct template_node *nodes;
	long node_count;
	boolean failed;
	/* whether it is a level's geometry (whose collision meshes are not
	drawn) */
	boolean level;
};

/* ---------- private code */

static unsigned long read_u32(
	unsigned char const *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static unsigned long read_u32_big(
	unsigned char const *bytes)
{
	return ((unsigned long)bytes[0] << 24) | ((unsigned long)bytes[1] << 16) | ((unsigned long)bytes[2] << 8) |
		(unsigned long)bytes[3];
}

static short read_short(
	unsigned char const *bytes)
{
	return (short)((unsigned short)bytes[0] | ((unsigned short)bytes[1] << 8));
}

static short read_short_big(
	unsigned char const *bytes)
{
	return (short)(((unsigned short)bytes[0] << 8) | (unsigned short)bytes[1]);
}

static real read_real(
	unsigned char const *bytes)
{
	unsigned long bits = read_u32(bytes);
	real value;

	memcpy(&value, &bits, sizeof(value));

	return value;
}

static boolean real_finite(
	real value)
{
	unsigned long bits = 0;

	memcpy(&bits, &value, sizeof(value));

	return (bits & 0x7F800000UL) != 0x7F800000UL;
}

static boolean file_read(
	struct saber_pak *pak,
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	return offset <= pak->file_size && size <= pak->file_size - offset &&
		(!size || config_host_file_read(pak->file, offset, buffer, size));
}

/* the size a block is uncompressed, by its chunks' headers, or -1 */
static long block_size(
	struct saber_pak *pak,
	long block_index)
{
	unsigned long position = pak->block_offsets[block_index];
	unsigned long end = pak->block_offsets[block_index + 1];
	long total = 0;

	while (position + 2 <= end)
	{
		unsigned char header[5];
		unsigned long uncompressed_size;
		unsigned long compressed_size;

		if (!file_read(pak, position, header, end - position >= 5 ? 5 : 2))
			return -1;
		if (header[0] == 0xFF)
		{
			if (end - position < 5)
				return -1;
			uncompressed_size = ((unsigned long)header[1] << 8) | header[2];
			compressed_size = ((unsigned long)header[3] << 8) | header[4];
			position += 5;
		}
		else
		{
			uncompressed_size = FRAME_SIZE;
			compressed_size = ((unsigned long)header[0] << 8) | header[1];
			position += 2;
		}
		if (!compressed_size)
			break;
		if (uncompressed_size > FRAME_SIZE || compressed_size > end - position)
			return -1;
		total += (long)uncompressed_size;
		if ((unsigned long)total > MAXIMUM_BLOCK_SIZE)
			return -1;
		position += compressed_size;
	}

	return total;
}

/* the bytes of a block, decompressed (the one read last, kept) */
static unsigned char *block_get(
	struct saber_pak *pak,
	long block_index)
{
	unsigned long file_offset = pak->block_offsets[block_index];
	unsigned long compressed_size = pak->block_offsets[block_index + 1] - file_offset;
	unsigned long size = pak->block_starts[block_index + 1] - pak->block_starts[block_index];
	long written;

	if (pak->cached_block == block_index)
		return pak->block;
	pak->cached_block = NONE;
	/* (a zlib chunk's first four bytes are its uncompressed size) */
	if (pak->zlib_chunks)
	{
		if (compressed_size < 4)
			return NULL;
		compressed_size -= 4;
		file_offset += 4;
	}
	if (compressed_size > MAXIMUM_COMPRESSED_BLOCK_SIZE ||
		!file_read(pak, file_offset, pak->compressed_block, compressed_size))
	{
		return NULL;
	}
	if (pak->zlib_chunks)
		written = port_zlib_inflate(pak->compressed_block, compressed_size, pak->block, size);
	else
		written = saber_lzx_decompress(pak->lzx, pak->compressed_block, compressed_size, pak->block, MAXIMUM_BLOCK_SIZE);
	if (written < 0 || (unsigned long)written != size)
		return NULL;
	pak->cached_block = block_index;

	return pak->block;
}

/* `size` bytes of the stream at `offset` */
static boolean stream_read(
	struct saber_pak *pak,
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	unsigned char *out = buffer;

	if (offset > pak->stream_size || size > pak->stream_size - offset)
		return FALSE;
	if (!pak->compressed)
		return file_read(pak, offset, buffer, size);
	while (size)
	{
		long low = 0;
		long high = pak->block_count;
		unsigned char *block;
		unsigned long within;
		unsigned long count;

		/* (the block it is in: block_starts[low] <= offset < block_starts[high]) */
		while (high - low > 1)
		{
			long middle = (low + high) / 2;

			if (pak->block_starts[middle] <= offset)
				low = middle;
			else
				high = middle;
		}
		block = block_get(pak, low);
		if (!block)
			return FALSE;
		within = offset - pak->block_starts[low];
		count = pak->block_starts[low + 1] - offset;
		if (count > size)
			count = size;
		memcpy(out, block + within, count);
		out += count;
		offset += count;
		size -= count;
	}

	return TRUE;
}

static boolean blocks_open(
	struct saber_pak *pak,
	char const **reason)
{
	unsigned char *table;
	long index;

	*reason = "its blocks are not a package's";
	if (pak->block_count <= 0 || pak->block_count > MAXIMUM_BLOCKS ||
		4 + 4 * (unsigned long)pak->block_count > pak->file_size)
	{
		return FALSE;
	}
	*reason = "out of memory";
	table = malloc(4 * (size_t)pak->block_count);
	pak->block_offsets = malloc(sizeof(unsigned long) * ((size_t)pak->block_count + 1));
	pak->block_starts = malloc(sizeof(unsigned long) * ((size_t)pak->block_count + 1));
	pak->lzx = malloc(saber_lzx_state_size());
	pak->window = malloc(MAXIMUM_BLOCK_SIZE);
	pak->block = malloc(MAXIMUM_BLOCK_SIZE);
	pak->compressed_block = malloc(MAXIMUM_COMPRESSED_BLOCK_SIZE);
	if (!table || !pak->block_offsets || !pak->block_starts || !pak->lzx || !pak->window || !pak->block ||
		!pak->compressed_block)
	{
		if (table)
			free(table);
		return FALSE;
	}
	*reason = "it cannot be read";
	if (!file_read(pak, 4, table, 4 * (unsigned long)pak->block_count))
	{
		free(table);
		return FALSE;
	}
	for (index = 0; index < pak->block_count; index++)
		pak->block_offsets[index] = read_u32(table + 4 * index);
	free(table);
	pak->block_offsets[pak->block_count] = pak->file_size;
	pak->block_starts[0] = 0;
	*reason = "its blocks are not a package's";
	for (index = 0; index < pak->block_count; index++)
	{
		long size;

		if (pak->block_offsets[index] < 4 + 4 * (unsigned long)pak->block_count ||
			pak->block_offsets[index] > pak->block_offsets[index + 1])
		{
			return FALSE;
		}
		size = block_size(pak, index);
		if (size < 0 || pak->block_starts[index] + (unsigned long)size < pak->block_starts[index])
			return FALSE;
		pak->block_starts[index + 1] = pak->block_starts[index] + (unsigned long)size;
	}
	pak->stream_size = pak->block_starts[pak->block_count];
	saber_lzx_initialize(pak->lzx, WINDOW_BITS, pak->window);
	pak->cached_block = NONE;

	return TRUE;
}

/* a zlib header: deflate, and the two bytes a multiple of 31 */
static boolean zlib_header(
	unsigned char const *bytes)
{
	unsigned long check = (unsigned long)bytes[0] * 256UL + bytes[1];

	return (bytes[0] & 0x0F) == 8 && check % 31UL == 0;
}

/* the Collection's chunks, not the 360's blocks: the first block's offset
holds a zlib chunk */
static boolean mcc_package(
	struct saber_pak *pak,
	unsigned long count,
	unsigned long first)
{
	unsigned char header[6];
	unsigned long size;

	if (count == 0 || count > MAXIMUM_BLOCKS || first < 4 + 4 * count || first > pak->file_size - 6)
		return FALSE;
	if (!file_read(pak, first, header, 6))
		return FALSE;
	size = read_u32(header);
	if (size == 0 || size > MAXIMUM_BLOCK_SIZE || !zlib_header(header + 4))
		return FALSE;

	return TRUE;
}

/* the Collection's zlib chunks, as blocks_open reads the 360's */
static boolean zlib_blocks_open(
	struct saber_pak *pak,
	char const **reason)
{
	unsigned char *table;
	long index;

	*reason = "its blocks are not a package's";
	if (pak->block_count <= 0 || pak->block_count > MAXIMUM_BLOCKS ||
		4 + 4 * (unsigned long)pak->block_count > pak->file_size)
	{
		return FALSE;
	}
	*reason = "out of memory";
	table = malloc(4 * (size_t)pak->block_count);
	pak->block_offsets = malloc(sizeof(unsigned long) * ((size_t)pak->block_count + 1));
	pak->block_starts = malloc(sizeof(unsigned long) * ((size_t)pak->block_count + 1));
	pak->block = malloc(MAXIMUM_BLOCK_SIZE);
	pak->compressed_block = malloc(MAXIMUM_COMPRESSED_BLOCK_SIZE);
	if (!table || !pak->block_offsets || !pak->block_starts || !pak->block || !pak->compressed_block)
	{
		if (table)
			free(table);
		return FALSE;
	}
	*reason = "it cannot be read";
	if (!file_read(pak, 4, table, 4 * (unsigned long)pak->block_count))
	{
		free(table);
		return FALSE;
	}
	for (index = 0; index < pak->block_count; index++)
		pak->block_offsets[index] = read_u32(table + 4 * index);
	free(table);
	pak->block_offsets[pak->block_count] = pak->file_size;
	pak->block_starts[0] = 0;
	*reason = "its blocks are not a package's";
	for (index = 0; index < pak->block_count; index++)
	{
		unsigned char header[6];
		unsigned long size;

		if (pak->block_offsets[index] < 4 + 4 * (unsigned long)pak->block_count ||
			pak->block_offsets[index] + 6 > pak->block_offsets[index + 1] ||
			!file_read(pak, pak->block_offsets[index], header, 6))
		{
			return FALSE;
		}
		size = read_u32(header);
		if (size == 0 || size > MAXIMUM_BLOCK_SIZE || !zlib_header(header + 4) ||
			pak->block_starts[index] + size < pak->block_starts[index])
		{
			return FALSE;
		}
		pak->block_starts[index + 1] = pak->block_starts[index] + size;
	}
	pak->stream_size = pak->block_starts[pak->block_count];
	pak->cached_block = NONE;

	return TRUE;
}

static boolean directory_read(
	struct saber_pak *pak,
	char const **reason)
{
	unsigned char words[12];
	unsigned long position = 4;
	unsigned long names_size = 0;
	char *name;
	long index;

	*reason = "its directory is not a package's";
	if (!stream_read(pak, 0, words, 4))
		return FALSE;
	pak->entry_count = (long)read_u32(words);
	if (pak->entry_count < 0 || pak->entry_count > MAXIMUM_ENTRIES)
		return FALSE;
	*reason = "out of memory";
	pak->entries = malloc(sizeof(*pak->entries) * ((size_t)pak->entry_count + 1));
	if (!pak->entries)
		return FALSE;
	*reason = "its directory is not a package's";
	for (index = 0; index < pak->entry_count; index++)
	{
		struct pak_entry *entry = &pak->entries[index];
		unsigned long name_length;

		if (!stream_read(pak, position, words, 12))
			return FALSE;
		entry->offset = read_u32(words);
		entry->size = read_u32(words + 4);
		name_length = read_u32(words + 8);
		if (name_length > MAXIMUM_NAME_LENGTH || entry->offset > pak->stream_size ||
			entry->size > pak->stream_size - entry->offset)
		{
			return FALSE;
		}
		/* (where its name is, until the names are read) */
		entry->name = (char *)(size_t)name_length;
		position += 12 + name_length;
		if (!stream_read(pak, position, words, 12))
			return FALSE;
		entry->type = (long)read_u32(words);
		position += 12;
		names_size += name_length + 1;
	}
	*reason = "out of memory";
	pak->names = malloc(names_size + 1);
	if (!pak->names)
		return FALSE;
	*reason = "its directory is not a package's";
	position = 4;
	name = pak->names;
	for (index = 0; index < pak->entry_count; index++)
	{
		struct pak_entry *entry = &pak->entries[index];
		unsigned long name_length = (unsigned long)(size_t)entry->name;

		position += 12;
		if (name_length && !stream_read(pak, position, name, name_length))
			return FALSE;
		name[name_length] = 0;
		entry->name = name;
		name += name_length + 1;
		position += name_length + 12;
	}

	return TRUE;
}

/* an .ipak: its first word a count and its second 0, then a texture's name */
static boolean ipak_stream(
	struct saber_pak *pak)
{
	unsigned char words[9];
	unsigned long count;

	if (!stream_read(pak, 0, words, 9))
		return FALSE;
	count = read_u32(words);
	if (read_u32(words + 4) != 0 || count == 0 || count > MAXIMUM_ENTRIES)
		return FALSE;

	return words[8] >= 32 && words[8] < 127;
}

/* the .ipak's records as texture entries */
static boolean ipak_entries(
	struct saber_pak *pak,
	char const **reason)
{
	unsigned char words[8];
	unsigned char record[IPAK_RECORD_SIZE];
	unsigned long count;
	unsigned long position;
	char *name;
	long index;

	*reason = "its directory is not a package's";
	if (!stream_read(pak, 0, words, 8))
		return FALSE;
	count = read_u32(words);
	if (read_u32(words + 4) != 0 || count == 0 || count > MAXIMUM_ENTRIES)
		return FALSE;
	*reason = "out of memory";
	pak->entries = malloc(sizeof(*pak->entries) * ((size_t)count + 1));
	pak->names = malloc((size_t)count * (IPAK_NAME_SIZE + 1));
	if (!pak->entries || !pak->names)
		return FALSE;
	*reason = "its directory is not a package's";
	position = 8;
	name = pak->names;
	pak->entry_count = 0;
	for (index = 0; (unsigned long)index < count; index++)
	{
		struct pak_entry *entry;
		unsigned long length = 0;
		unsigned long size;
		unsigned long offset;

		if (!stream_read(pak, position, record, IPAK_RECORD_SIZE))
			return FALSE;
		position += IPAK_RECORD_SIZE;
		while (length < IPAK_NAME_SIZE && record[length])
			length++;
		if (!length || length >= IPAK_NAME_SIZE)
			continue;
		size = read_u32(record + IPAK_SIZE_AT);
		offset = read_u32(record + IPAK_OFFSET_AT);
		/* (the record's size counts the pixels, and the header before them
		is part of what a texture reader takes) */
		if (!size || size > MAXIMUM_ENTRY_SIZE - IPAK_HEADER_SIZE || offset > pak->stream_size ||
			size + IPAK_HEADER_SIZE > pak->stream_size - offset)
		{
			continue;
		}
		size += IPAK_HEADER_SIZE;
		entry = &pak->entries[pak->entry_count++];
		memcpy(name, record, length);
		name[length] = 0;
		entry->name = name;
		entry->offset = offset;
		entry->size = size;
		entry->type = _saber_entry_textures;
		name += length + 1;
	}
	if (!pak->entry_count)
		return FALSE;

	return TRUE;
}

/* the block at `offset`, within `limit` */
static boolean block_at(
	unsigned char const *bytes,
	unsigned long offset,
	unsigned long limit,
	struct block *block)
{
	unsigned long end;

	if (offset > limit || limit - offset < 6)
		return FALSE;
	end = read_u32(bytes + offset + 2);
	if (end < offset + 6 || end > limit)
		return FALSE;
	block->type = ((unsigned long)bytes[offset] << 8) | bytes[offset + 1];
	block->start = offset + 6;
	block->end = end;

	return TRUE;
}

/* the first block of the type among the blocks from `start` to `end` */
static boolean block_find(
	unsigned char const *bytes,
	unsigned long start,
	unsigned long end,
	unsigned long type,
	struct block *found)
{
	struct block block;

	while (start < end && block_at(bytes, start, end, &block))
	{
		if (block.type == type)
		{
			*found = block;
			return TRUE;
		}
		start = block.end;
	}

	return FALSE;
}

/* whether `text` occurs in `block` */
static boolean block_contains(
	unsigned char const *bytes,
	struct block const *block,
	char const *text)
{
	unsigned long length = (unsigned long)strlen(text);
	unsigned long index;

	if (length == 0 || block->end < block->start + length)
		return FALSE;
	for (index = block->start; index + length <= block->end; index++)
	{
		if (memcmp(bytes + index, text, length) == 0)
			return TRUE;
	}

	return FALSE;
}

/* the number on the line after `key` in `block`, or FALSE */
static boolean property_real(
	unsigned char const *bytes,
	struct block const *block,
	char const *key,
	real *value)
{
	unsigned long length = (unsigned long)strlen(key);
	unsigned long index;
	unsigned long start;
	long sign;
	long whole;
	long frac;
	long denom;
	boolean any;

	if (block->end < block->start + length + 2)
		return FALSE;
	for (index = block->start; index + length + 1 <= block->end; index++)
	{
		if (memcmp(bytes + index, key, length) != 0 || bytes[index + length] != '\n')
			continue;
		start = index + length + 1;
		sign = 1;
		whole = 0;
		frac = 0;
		denom = 1;
		any = FALSE;
		if (start < block->end && bytes[start] == '-')
		{
			sign = -1;
			start++;
		}
		while (start < block->end && bytes[start] >= '0' && bytes[start] <= '9')
		{
			whole = whole * 10 + (long)(bytes[start] - '0');
			start++;
			any = TRUE;
		}
		if (start < block->end && bytes[start] == '.')
		{
			start++;
			while (start < block->end && bytes[start] >= '0' && bytes[start] <= '9')
			{
				frac = frac * 10 + (long)(bytes[start] - '0');
				denom *= 10;
				start++;
				any = TRUE;
			}
		}
		if (!any || denom == 0)
			return FALSE;
		*value = (real)sign * ((real)whole + (real)frac / (real)denom);
		return TRUE;
	}

	return FALSE;
}

static void node_read(
	struct template_walk *walk,
	struct block const *node_block,
	long depth);

/* the nodes among the blocks from `start` to `end` */
static void nodes_read(
	struct template_walk *walk,
	unsigned long start,
	unsigned long end,
	long depth)
{
	struct block block;

	while (!walk->failed && start < end)
	{
		if (!block_at(walk->bytes, start, end, &block))
		{
			walk->failed = TRUE;
			return;
		}
		if (block.type == _block_node)
			node_read(walk, &block, depth);
		start = block.end;
	}
}

static void node_read(
	struct template_walk *walk,
	struct block const *node_block,
	long depth)
{
	unsigned char const *bytes = walk->bytes;
	struct block block;

	if (depth > MAXIMUM_NODE_DEPTH)
	{
		walk->failed = TRUE;
		return;
	}
	if (block_find(bytes, node_block->start, node_block->end, _block_mesh, &block))
	{
		struct template_node *node;
		unsigned long name_end = block.start;
		long index;

		while (name_end < block.end && bytes[name_end])
			name_end++;
		if (walk->node_count >= MAXIMUM_NODES || block.end - name_end < 3)
		{
			walk->failed = TRUE;
			return;
		}
		node = &walk->nodes[walk->node_count++];
		csmemset(node, 0, sizeof(*node));
		node->id = read_short(bytes + name_end + 1);
		node->name_start = block.start;
		node->name_length = name_end - block.start;
		node->bone_index = NONE;
		/* (".lod1.w", or a name that is the level alone, "lod2") */
		for (index = (long)block.start; index + 4 <= (long)name_end; index++)
		{
			if ((index == (long)block.start || bytes[index] == '.') && index + 4 < (long)name_end)
			{
				unsigned long at = bytes[index] == '.' ? (unsigned long)index + 1 : (unsigned long)index;

				if (at + 3 < name_end && bytes[at] == 'l' && bytes[at + 1] == 'o' && bytes[at + 2] == 'd' &&
					bytes[at + 3] >= '0' && bytes[at + 3] <= '9')
				{
					node->lod = (short)(bytes[at + 3] - '0');
				}
			}
		}
		node->parent_id = NONE;
		node->source_id = NONE;
		if (block.end - name_end >= 18)
		{
			node->own_vertex_count = (long)read_u32(bytes + name_end + 10);
			node->own_triangle_count = (long)read_u32(bytes + name_end + 14);
		}
		for (index = 0; index < 16; index++)
			node->transform[index] = (index % 5) ? 0.f : 1.f;
		if (block_find(bytes, node_block->start, node_block->end, _block_transform, &block) &&
			block.end - block.start >= 64)
		{
			for (index = 0; index < 16; index++)
			{
				node->transform[index] = read_real(bytes + block.start + 4 * index);
				if (!real_finite(node->transform[index]))
					walk->failed = TRUE;
			}
		}
		if (block_find(bytes, node_block->start, node_block->end, _block_parent, &block) && block.end - block.start >= 4)
			node->parent_id = (long)read_u32(bytes + block.start);
		if (block_find(bytes, node_block->start, node_block->end, _block_mesh_source, &block) &&
			block.end - block.start >= 10)
		{
			node->source_id = read_short(bytes + block.start);
			node->source_vertex = read_u32(bytes + block.start + 2);
			node->source_triangle = read_u32(bytes + block.start + 6);
		}
		node->scene_scale[0] = node->scene_scale[1] = node->scene_scale[2] = 1.f;
		if (block_find(bytes, node_block->start, node_block->end, _block_scene_object_bounds, &block) &&
			block.end - block.start >= 12)
		{
			node->has_scene_bounds = TRUE;
			node->scene_translation[0] = (real)read_short(bytes + block.start);
			node->scene_translation[1] = (real)read_short(bytes + block.start + 2);
			node->scene_translation[2] = (real)read_short(bytes + block.start + 4);
			node->scene_scale[0] = (real)read_short(bytes + block.start + 6);
			node->scene_scale[1] = (real)read_short(bytes + block.start + 8);
			node->scene_scale[2] = (real)read_short(bytes + block.start + 10);
		}
		if (block_find(bytes, node_block->start, node_block->end, _block_bone_index, &block) &&
			block.end - block.start >= 4)
		{
			node->bone_index = (long)read_u32(bytes + block.start);
		}
		block_find(bytes, node_block->start, node_block->end, _block_blend, &node->blend);
		if (block_find(bytes, node_block->start, node_block->end, _block_mesh_flags, &block) &&
			block.end - block.start >= 4)
		{
			node->mesh_flags = (unsigned short)read_short(bytes + block.start + 2);
			node->has_mesh_flags = TRUE;
		}
		block_find(bytes, node_block->start, node_block->end, _block_positions, &node->positions);
		block_find(bytes, node_block->start, node_block->end, _block_vertex_data, &node->vertex_data);
		block_find(bytes, node_block->start, node_block->end, _block_triangles, &node->triangles);
		block_find(bytes, node_block->start, node_block->end, _block_submeshes, &node->submeshes);
		/* (its texture coordinates, packed into the vertex's unit range:
		unit * tile + shift. A mesh marked uncompressed already holds them) */
		if (block_find(bytes, node_block->start, node_block->end, _block_properties, &block) &&
			block_contains(bytes, &block, "visibility_hidden"))
			node->hidden = TRUE;
		if (block_find(bytes, node_block->start, node_block->end, _block_properties, &block) &&
			block_contains(bytes, &block, "__export_uv_compr_params") &&
			!block_contains(bytes, &block, "export_no_vertex_compression") &&
			property_real(bytes, &block, "shiftS", &node->uv_shift[0]) &&
			property_real(bytes, &block, "shiftT", &node->uv_shift[1]) &&
			property_real(bytes, &block, "tileS", &node->uv_tile[0]) &&
			property_real(bytes, &block, "tileT", &node->uv_tile[1]))
		{
			node->has_uv_decompress = TRUE;
		}
	}
	nodes_read(walk, node_block->start, node_block->end, depth + 1);
}

static struct template_node const *node_of_id(
	struct template_walk const *walk,
	long id)
{
	long index;

	for (index = 0; index < walk->node_count; index++)
	{
		if (walk->nodes[index].id == id)
			return &walk->nodes[index];
	}

	return NULL;
}

/* a = a * b, 4x4 row-major */
static void matrix_multiply(
	real *a,
	real const *b)
{
	real result[16];
	long row;
	long column;

	for (row = 0; row < 4; row++)
	{
		for (column = 0; column < 4; column++)
		{
			result[4 * row + column] = a[4 * row] * b[column] + a[4 * row + 1] * b[4 + column] +
				a[4 * row + 2] * b[8 + column] + a[4 * row + 3] * b[12 + column];
		}
	}
	memcpy(a, result, sizeof(result));
}

/* a node's transform to the template's: its own, then its parents' */
static void node_world_transform(
	struct template_walk const *walk,
	struct template_node const *node,
	real *transform)
{
	long depth;

	memcpy(transform, node->transform, sizeof(node->transform));
	for (depth = 0; depth < MAXIMUM_NODE_DEPTH && node->parent_id != NONE; depth++)
	{
		node = node_of_id(walk, node->parent_id);
		if (!node)
			break;
		matrix_multiply(transform, node->transform);
	}
}

/* whether a node, or a node it is under, is a damaged copy of the
template ("_damaged": a ghost's, beside its whole "__base"), which Halo
draws as permutations of its own, not over the whole one */
static boolean node_damaged(
	struct template_walk const *walk,
	struct template_node const *node)
{
	long depth;

	for (depth = 0; node && depth < MAXIMUM_NODE_DEPTH; depth++)
	{
		char name[64];
		unsigned long length = MIN(node->name_length, sizeof(name) - 1);

		memcpy(name, walk->bytes + node->name_start, length);
		name[length] = 0;
		if (strstr(name, "damaged"))
			return TRUE;
		node = node->parent_id == NONE ? NULL : node_of_id(walk, node->parent_id);
	}

	return FALSE;
}

/* the rank a node, or the nearest node it is under that is of one, is
drawn for (an Elite's "metal_major", a Grunt's "back_minor": Saber3D draws
each rank as meshes of its own), or _saber_rank_all */
static short node_rank(
	struct template_walk const *walk,
	struct template_node const *node)
{
	static struct
	{
		char const *word;
		short rank;
	} const ranks[] = {
		{ "minor", _saber_rank_minor },
		{ "major", _saber_rank_major },
		{ "officer", _saber_rank_major },
		{ "special", _saber_rank_spec_ops },
		{ "spec_ops", _saber_rank_spec_ops },
		{ "specops", _saber_rank_spec_ops },
		{ "stealth", _saber_rank_stealth },
		{ "zealot", _saber_rank_commander },
		{ "ultra", _saber_rank_commander },
		{ "commander", _saber_rank_commander },
		{ "honor", _saber_rank_commander },
	};
	long depth;

	for (depth = 0; node && depth < MAXIMUM_NODE_DEPTH; depth++)
	{
		char name[64];
		unsigned long length = MIN(node->name_length, sizeof(name) - 1);
		long rank;

		memcpy(name, walk->bytes + node->name_start, length);
		name[length] = 0;
		/* (a marine's eyepiece is named for a commander, and it is not one:
		the word would make every marine that rank's, and the eyepiece his) */
		if (!strstr(name, "monicle"))
		{
			for (rank = 0; rank < (long)NUMBEROF(ranks); rank++)
			{
				if (strstr(name, ranks[rank].word))
					return ranks[rank].rank;
			}
		}
		node = node->parent_id == NONE ? NULL : node_of_id(walk, node->parent_id);
	}

	return _saber_rank_all;
}

/* the head permutation a mesh is (the node under one named "head": a
marine's head_charles_helmet_10, its lods and its visor with it), or
empty when it is the body, drawn for every permutation. The name is lower
case, hyphens written as underscores, as the map's permutation names are
compared */
static void node_variant(
	struct template_walk const *walk,
	struct template_node const *node,
	char *variant,
	long variant_size)
{
	long depth;

	variant[0] = 0;
	if (variant_size <= 1)
		return;
	for (depth = 0; node && depth < MAXIMUM_NODE_DEPTH; depth++)
	{
		struct template_node const *parent = node->parent_id == NONE ? NULL :
			node_of_id(walk, node->parent_id);
		char parent_name[64];
		unsigned long length;
		long index;

		if (!parent)
			break;
		length = MIN(parent->name_length, sizeof(parent_name) - 1);
		memcpy(parent_name, walk->bytes + parent->name_start, length);
		parent_name[length] = 0;
		for (index = 0; parent_name[index]; index++)
		{
			if (parent_name[index] >= 'A' && parent_name[index] <= 'Z')
				parent_name[index] = (char)(parent_name[index] - 'A' + 'a');
		}
		if (!strcmp(parent_name, "head"))
		{
			long at;
			unsigned long copy = MIN(node->name_length, (unsigned long)variant_size - 1);

			memcpy(variant, walk->bytes + node->name_start, copy);
			variant[copy] = 0;
			for (at = 0; variant[at]; at++)
			{
				if (variant[at] >= 'A' && variant[at] <= 'Z')
					variant[at] = (char)(variant[at] - 'A' + 'a');
				else if (variant[at] == '-')
					variant[at] = '_';
			}
			return;
		}
		node = parent;
	}
}

/* whether a name (not 0-terminated) has a word in it */
static boolean memmem_name(
	unsigned char const *name,
	unsigned long length,
	char const *word)
{
	unsigned long word_length = (unsigned long)strlen(word);
	unsigned long at;

	for (at = 0; at + word_length <= length; at++)
	{
		if (!memcmp(name + at, word, word_length))
			return TRUE;
	}

	return FALSE;
}

/* the inverse of a 4x4 row-major matrix (Gauss-Jordan); FALSE if it has
none */
static boolean matrix_invert(
	real const *matrix,
	real *inverse)
{
	real work[4][8];
	long row;
	long column;

	for (row = 0; row < 4; row++)
	{
		for (column = 0; column < 4; column++)
		{
			work[row][column] = matrix[4 * row + column];
			work[row][4 + column] = row == column ? 1.f : 0.f;
		}
	}
	for (column = 0; column < 4; column++)
	{
		long pivot = column;
		real scale;

		for (row = column + 1; row < 4; row++)
		{
			real a = work[row][column] < 0.f ? -work[row][column] : work[row][column];
			real b = work[pivot][column] < 0.f ? -work[pivot][column] : work[pivot][column];

			if (a > b)
				pivot = row;
		}
		if (work[pivot][column] > -1e-12f && work[pivot][column] < 1e-12f)
			return FALSE;
		if (pivot != column)
		{
			real swap[8];

			memcpy(swap, work[pivot], sizeof(swap));
			memcpy(work[pivot], work[column], sizeof(swap));
			memcpy(work[column], swap, sizeof(swap));
		}
		scale = 1.f / work[column][column];
		for (row = 0; row < 8; row++)
			work[column][row] *= scale;
		for (row = 0; row < 4; row++)
		{
			long other;
			real factor = work[row][column];

			if (row == column)
				continue;
			for (other = 0; other < 8; other++)
				work[row][other] -= factor * work[column][other];
		}
	}
	for (row = 0; row < 4; row++)
	{
		for (column = 0; column < 4; column++)
			inverse[4 * row + column] = work[row][4 + column];
	}

	return TRUE;
}

/* The slot among the template's bones of the bone a node moves with: its
own, if it is one, else its nearest parent's that is, else (a template
whose parts are not bones, as the vehicles') its own; its name made as
Halo's nodes' are compared (lower case, spaces for underscores, without
Saber3D's leading "." and trailing ".x"), its parent's slot (the bone the
bone's parent moves with) and its bind: what takes a vertex of the
template, in Halo's axes, to the bone's own (the inverse of the bone's
transform, after Halo's axes are turned back to Saber3D's). NONE when there
is no room. */
static long bone_slot_at(
	struct saber_template *template_,
	struct template_walk const *walk,
	struct template_node const *node,
	long depth)
{
	struct template_node const *bone = node;
	char name[64];
	unsigned long start;
	unsigned long length;
	unsigned long index;
	long climb;
	long slot;
	long parent = NONE;
	real world[16];
	real inverse[16];

	for (climb = 0; bone && bone->bone_index == NONE && climb < MAXIMUM_NODE_DEPTH; climb++)
		bone = bone->parent_id == NONE ? NULL : node_of_id(walk, bone->parent_id);
	if (!bone || bone->bone_index == NONE)
		bone = node;
	start = bone->name_start;
	length = bone->name_length;
	if (length && walk->bytes[start] == '.')
	{
		start++;
		length--;
	}
	for (index = 0; index < length; index++)
	{
		if (walk->bytes[start + index] == '.')
		{
			length = index;
			break;
		}
	}
	length = MIN(length, sizeof(name) - 1);
	for (index = 0; index < length; index++)
	{
		char character = (char)walk->bytes[start + index];

		name[index] = character == '_' ? ' ' : (character >= 'A' && character <= 'Z') ? (char)(character - 'A' + 'a') :
			character;
	}
	name[length] = 0;
	for (slot = 0; slot < template_->bone_count; slot++)
	{
		if (!strcmp(template_->bones[slot], name))
			return slot;
	}
	if (bone->parent_id != NONE && depth < MAXIMUM_NODE_DEPTH)
	{
		struct template_node const *above = node_of_id(walk, bone->parent_id);

		if (above)
			parent = bone_slot_at(template_, walk, above, depth + 1);
	}
	node_world_transform(walk, bone, world);
	if (!matrix_invert(world, inverse))
	{
		for (index = 0; index < 16; index++)
			inverse[index] = (index % 5) ? 0.f : 1.f;
	}
	{
		char (*bones)[64] = realloc(template_->bones, sizeof(*template_->bones) * ((size_t)template_->bone_count + 1));
		long *parents;
		real (*binds)[16];

		if (!bones)
			return NONE;
		template_->bones = bones;
		parents = realloc(template_->bone_parents, sizeof(*parents) * ((size_t)template_->bone_count + 1));
		if (!parents)
			return NONE;
		template_->bone_parents = parents;
		binds = realloc(template_->bone_binds, sizeof(*binds) * ((size_t)template_->bone_count + 1));
		if (!binds)
			return NONE;
		template_->bone_binds = binds;
		{
			real (*origins)[3] = realloc(template_->bone_origins,
				sizeof(*origins) * ((size_t)template_->bone_count + 1));

			if (!origins)
				return NONE;
			template_->bone_origins = origins;
			origins[template_->bone_count][0] = world[12];
			origins[template_->bone_count][1] = -world[14];
			origins[template_->bone_count][2] = world[13];
		}
		strcpy(bones[template_->bone_count], name);
		parents[template_->bone_count] = parent;
		/* (Halo's axes to Saber3D's: Saber3D's (x, y, z) is Halo's (x, -z, y),
		so Halo's y is Saber3D's -z and its z Saber3D's y: rows 1 and 2,
		less row 2) */
		for (index = 0; index < 4; index++)
		{
			binds[template_->bone_count][index] = inverse[index];
			binds[template_->bone_count][4 + index] = -inverse[8 + index];
			binds[template_->bone_count][8 + index] = inverse[4 + index];
			binds[template_->bone_count][12 + index] = inverse[12 + index];
		}
	}

	return template_->bone_count++;
}

static long bone_slot(
	struct saber_template *template_,
	struct template_walk const *walk,
	struct template_node const *node)
{
	return bone_slot_at(template_, walk, node, 0);
}

/* the transform of normals of a row-vector transform: the inverse
transpose of its 3x3 (its cofactors: the determinant's scale is lost as
they are made unit vectors) */
static void normal_transform(
	real const *transform,
	real *normals)
{
	real a = transform[0], b = transform[1], c = transform[2];
	real d = transform[4], e = transform[5], f = transform[6];
	real g = transform[8], h = transform[9], i = transform[10];

	normals[0] = e * i - f * h;
	normals[1] = -(d * i - f * g);
	normals[2] = d * h - e * g;
	normals[3] = -(b * i - c * h);
	normals[4] = a * i - c * g;
	normals[5] = -(a * h - b * g);
	normals[6] = b * f - c * e;
	normals[7] = -(a * f - c * d);
	normals[8] = a * e - b * d;
}

/* Saber3D's packed normal: x and z of its magnitude's place in 181 and
181 * 181 steps, y of the rest, signed as it is */
static void normal_unpack(
	short bits,
	real_vector3d *normal)
{
	long magnitude = bits < 0 ? -(long)bits : (long)bits;
	real x = (-1.f + 2.f * (real)(magnitude % 181) / 181.f) * (181.f / 179.f);
	real z = (-1.f + 2.f * (real)(magnitude % (181 * 181)) / (181.f * 181.f)) * (181.f / 180.f);
	real rest = 1.f - x * x - z * z;

	if (rest < 0.f)
		rest = 0.f;
	if (rest > 1.f)
		rest = 1.f;
	normal->i = x;
	normal->j = bits > 0 ? square_root(rest) : bits < 0 ? -square_root(rest) : 0.f;
	normal->k = z;
}

static real unit_short(
	short value)
{
	real unit = (real)value / 32767.f;

	return unit < -1.f ? -1.f : unit;
}

/* where a vertex's texture coordinates are in its data, by the data's
size (Reclaimer's), or -1. `format` is the four bytes before the stride.
One level layout keeps them elsewhere than a template of that size: the
cliff's 44-byte vertices (0x2003dfde) at 16 rather than 28. Read from
28, the cliff is one texel. The beach's 20-byte vertices (0x200043c2)
stay at 16, the ordinary place for that size: the pair at 12 is a line,
its v one value, and the sand was that one row of its texture. A
template's own 20-byte vertices are 0x2000c646 and stay at 16 too. */
static long texcoord_offset(
	long size,
	unsigned long format)
{
	if (size == 44 && format == 0x2003dfdeUL)
		return 16;
	switch (size)
	{
	case 8:
	case 12:
		return 4;
	case 16:
		return 12;
	case 20:
	case 24:
	case 32:
		return 16;
	case 28:
		return 20;
	case 36:
		return 24;
	case 44:
		return 28;
	}

	return -1;
}

/* a vertex's tangent and binormal: its tangent along u, made at right
angles to its normal (or, with none, any), its binormal the two's cross */
static void vertex_basis(
	struct model_vertex_uncompressed *vertex,
	real_vector3d const *tangent)
{
	real_vector3d along = *tangent;
	real_vector3d normal_part = vertex->normal;

	scale_vector3d(&normal_part, dot_product3d(&vertex->normal, &along), &normal_part);
	along.i -= normal_part.i;
	along.j -= normal_part.j;
	along.k -= normal_part.k;
	if (normalize3d(&along) < 0.0001f)
	{
		perpendicular3d(&vertex->normal, &along);
		normalize3d(&along);
	}
	vertex->tangent = along;
	cross_product3d(&vertex->normal, &along, &vertex->binormal);
	normalize3d(&vertex->binormal);
}

/* qsort's order for the beach's texture-edge lengths */
static int compare_reals(
	void const *left,
	void const *right)
{
	real a = *(real const *)left;
	real b = *(real const *)right;

	if (a < b)
		return -1;
	if (a > b)
		return 1;

	return 0;
}

/* the mesh of a node made Halo's parts, each submesh's (of at most
MAXIMUM_PART_TRIANGLES triangles each); FALSE when it is not a mesh that
can be */
static boolean mesh_read(
	struct saber_template *template_,
	struct template_walk const *walk,
	struct template_node const *node,
	boolean xbox360,
	char const **reason)
{
	unsigned char const *bytes = walk->bytes;
	/* (the mesh whose buffers it is drawn of: its own, or its source's) */
	struct template_node const *source = node->source_id == NONE ? node : node_of_id(walk, node->source_id);
	unsigned long flags = source ? source->mesh_flags : 0;
	unsigned long base_vertex = node->source_id == NONE ? 0 : node->source_vertex;
	unsigned long first_triangle = node->source_id == NONE ? 0 : node->source_triangle;
	unsigned long vertex_data_start = 0;
	long source_vertex_count;
	long vertex_count;
	long triangle_count;
	long vertex_size = 0;
	long uv_offset = -1;
	unsigned long data;
	struct model_vertex_uncompressed *vertices;
	unsigned short *triangles;
	real_vector3d *tangents;
	long *remap;
	/* (a chunk's spare vertex: the origin, and no texture. A triangle on it
	is a wedge across the beach) */
	unsigned char *empty;
	real transform[16];
	real normals[9];
	real_point3d translation = { 0.f, 0.f, 0.f };
	real_point3d scale = { 1.f, 1.f, 1.f };
	boolean global_indices = FALSE;
	long agree = 0;
	boolean flip;
	long submesh_first[MAXIMUM_SUBMESHES];
	long submesh_count[MAXIMUM_SUBMESHES];
	short submesh_material[MAXIMUM_SUBMESHES];
	long submesh_total = 0;
	/* (a beach patch names no tile, so its coordinates cover the patch
	once and the sand is one smudge. This is the repeat that gives it the
	terrain's grain, 0 until it is measured. The median is that repeat's
	raw density, and an edge many times the median is a wedge) */
	real beach_repeat = 0.f;
	real beach_median = 0.f;
	long rigid_slot;
	unsigned char const *blend_indices = NULL;
	unsigned char const *blend_weights = NULL;
	long blend_count = 0;
	long blend_slots[256];
	struct template_node const *blend_nodes[256];
	/* (a skin compound's vertex is in the space of the bone it names: the
	bone whose space the transform is now, NONE the mesh's) */
	long transform_bone = NONE;
	boolean compound = FALSE;
	real anchor[3] = { 0.f, 0.f, 0.f };
	long index;

	*reason = "a mesh is not as a template's is";
	/* (a mesh the template marks hidden. The pelican's are its engine
	screens and a second body: drawn, the screens sit out past the pods) */
	if (node->hidden)
		return TRUE;
	/* (a template's meshes of another's buffers are left out, as they were
	before the level's were read: only a level's are drawn so) */
	if (source != node && !walk->level)
		return TRUE;
	if (!source || source->positions.end - source->positions.start < 4 ||
		source->triangles.end - source->triangles.start < 4)
	{
		return TRUE;
	}
	vertex_count = (long)read_u32(bytes + source->positions.start);
	triangle_count = (long)read_u32(bytes + source->triangles.start);
	source_vertex_count = vertex_count;
	if (vertex_count <= 0 || triangle_count <= 0 || node_damaged(walk, node))
		return TRUE;
	if (vertex_count > 0x100000 || triangle_count > 0x100000 ||
		(unsigned long)triangle_count * 6 > source->triangles.end - source->triangles.start - 4)
	{
		return FALSE;
	}
	/* (a level's mesh: its own of its source's vertices and triangles; a
	source drawn by its meshes, not itself) */
	if (source != node)
	{
		if (node->own_vertex_count <= 0 || node->own_triangle_count <= 0 ||
			base_vertex + (unsigned long)node->own_vertex_count > (unsigned long)vertex_count ||
			first_triangle + (unsigned long)node->own_triangle_count > (unsigned long)triangle_count)
		{
			return TRUE;
		}
		vertex_count = node->own_vertex_count;
		triangle_count = node->own_triangle_count;
	}
	else if (node->name_length >= 10 && memmem_name(bytes + node->name_start, node->name_length, "sharingObj"))
	{
		return TRUE;
	}
	if (walk->level && memmem_name(bytes + node->name_start, node->name_length, "collision"))
		return TRUE;
	data = source->positions.start + 4;
	if (TEST_FLAG(flags, _mesh_compressed_bit))
	{
		if (source->positions.end - data < 12)
			return FALSE;
		translation.x = (real)read_short(bytes + data);
		translation.y = (real)read_short(bytes + data + 2);
		translation.z = (real)read_short(bytes + data + 4);
		scale.x = (real)read_short(bytes + data + 6);
		scale.y = (real)read_short(bytes + data + 8);
		scale.z = (real)read_short(bytes + data + 10);
		data += 12;
		if ((unsigned long)source_vertex_count * 8 > source->positions.end - data)
			return FALSE;
		data += 8 * base_vertex;
	}
	else if ((unsigned long)source_vertex_count * 12 > source->positions.end - data)
	{
		return FALSE;
	}
	else
	{
		data += 12 * base_vertex;
	}
	if (source->vertex_data.end - source->vertex_data.start >= 13 &&
		(long)read_u32(bytes + source->vertex_data.start) == source_vertex_count)
	{
		vertex_size = bytes[source->vertex_data.start + 12];
		uv_offset = texcoord_offset(vertex_size, read_u32(bytes + source->vertex_data.start + 8));
		vertex_data_start = source->vertex_data.start + 13 + (unsigned long)vertex_size * base_vertex;
		if (uv_offset < 0 || (unsigned long)source_vertex_count * (unsigned long)vertex_size >
			source->vertex_data.end - source->vertex_data.start - 13)
		{
			uv_offset = -1;
		}
		/* (a 12-byte vertex of this format holds two pairs. A leaf card's
		first is a line, one coordinate a single value, so the card shows
		one texel row of its texture, the bark's edge or the leaf's black,
		across the whole quad. The card is the next pair. A pair that
		already covers its triangles, a jackal's shield, stays) */
		if (uv_offset == 4 && vertex_size == 12 && vertex_count > 0 &&
			read_u32(bytes + source->vertex_data.start + 8) == 0x20004242UL)
		{
			real min_u = 1.f;
			real max_u = -1.f;
			real min_v = 1.f;
			real max_v = -1.f;
			real min_u2 = 1.f;
			real max_u2 = -1.f;
			real min_v2 = 1.f;
			real max_v2 = -1.f;
			long scan;

			for (scan = 0; scan < vertex_count; scan++)
			{
				unsigned char const *uv = bytes + vertex_data_start + (unsigned long)vertex_size * (unsigned long)scan + 4;
				real u = unit_short(xbox360 ? read_short_big(uv) : read_short(uv));
				real v = unit_short(xbox360 ? read_short_big(uv + 2) : read_short(uv + 2));
				real u2 = unit_short(xbox360 ? read_short_big(uv + 4) : read_short(uv + 4));
				real v2 = unit_short(xbox360 ? read_short_big(uv + 6) : read_short(uv + 6));

				if (u < min_u)
					min_u = u;
				if (u > max_u)
					max_u = u;
				if (v < min_v)
					min_v = v;
				if (v > max_v)
					max_v = v;
				if (u2 < min_u2)
					min_u2 = u2;
				if (u2 > max_u2)
					max_u2 = u2;
				if (v2 < min_v2)
					min_v2 = v2;
				if (v2 > max_v2)
					max_v2 = v2;
			}
			if ((max_u - min_u < 0.02f || max_v - min_v < 0.02f) &&
				max_u2 - min_u2 > 0.02f && max_v2 - min_v2 > 0.02f)
			{
				uv_offset = 8;
			}
		}
		/* (a 20-byte vertex of this format keeps the texture twelve bytes
		in and a tangent at sixteen. The tangent's u runs below zero, so a
		crate and the plasma rifle were wrapped on it. A trunk's texture is
		the pair at sixteen and its u stays positive) */
		if (uv_offset == 16 && vertex_size == 20 && vertex_count > 0 &&
			read_u32(bytes + source->vertex_data.start + 8) == 0x2000C646UL)
		{
			real min_u = 1.f;
			real max_u = -1.f;
			real min_v = 1.f;
			real max_v = -1.f;
			real min_u2 = 1.f;
			long inside = 0;
			long inside_tangent = 0;
			long scan;

			for (scan = 0; scan < vertex_count; scan++)
			{
				unsigned char const *uv = bytes + vertex_data_start + (unsigned long)vertex_size * (unsigned long)scan;
				real u = unit_short(xbox360 ? read_short_big(uv + 12) : read_short(uv + 12));
				real v = unit_short(xbox360 ? read_short_big(uv + 14) : read_short(uv + 14));
				real u2 = unit_short(xbox360 ? read_short_big(uv + 16) : read_short(uv + 16));
				real v2 = unit_short(xbox360 ? read_short_big(uv + 18) : read_short(uv + 18));

				if (u < min_u)
					min_u = u;
				if (u > max_u)
					max_u = u;
				if (v < min_v)
					min_v = v;
				if (v > max_v)
					max_v = v;
				if (u2 < min_u2)
					min_u2 = u2;
				if (u >= -0.02f && u <= 1.02f && v >= -0.02f && v <= 1.02f)
					inside++;
				if (u2 >= -0.02f && u2 <= 1.02f && v2 >= -0.02f && v2 <= 1.02f)
					inside_tangent++;
			}
			if (min_u2 < -0.15f && min_v >= -0.05f && max_v <= 1.05f &&
				max_u - min_u > 0.02f && max_v - min_v > 0.02f && inside > inside_tangent)
			{
				uv_offset = 12;
			}
		}
	}

	/* the bones it moves with: its own node's, or each vertex's blend of the
	nodes it names (each a mesh's id from the first, its bone that mesh's) */
	*reason = "out of memory";
	rigid_slot = bone_slot(template_, walk, node);
	if (rigid_slot == NONE)
		return FALSE;
	if (node->blend.end > node->blend.start)
	{
		struct block indices;
		struct block weights;

		if (block_find(bytes, node->blend.start, node->blend.end, _block_blend_indices, &indices) &&
			indices.end - indices.start >= 4 + 4 * (unsigned long)vertex_count)
		{
			short first_id = read_short(bytes + indices.start);

			blend_count = MIN(MAX(read_short(bytes + indices.start + 2), 0), 256);
			blend_indices = bytes + indices.start + 4;
			for (index = 0; index < blend_count; index++)
			{
				struct template_node const *blended = node_of_id(walk, first_id + index);

				blend_nodes[index] = blended;
				blend_slots[index] = blended ? bone_slot(template_, walk, blended) : NONE;
			}
			if (block_find(bytes, node->blend.start, node->blend.end, _block_blend_weights, &weights) &&
				weights.end - weights.start >= 4 * (unsigned long)vertex_count)
			{
				blend_weights = bytes + weights.start;
			}
		}
	}

	/* the submeshes: each its triangles and its base layer's material */
	if (node->submeshes.end > node->submeshes.start)
	{
		struct block list;

		if (block_find(bytes, node->submeshes.start, node->submeshes.end, _block_submesh_list, &list) &&
			list.end - list.start >= 4)
		{
			unsigned long position = list.start + 4;
			struct block block;

			while (position < list.end && block_at(bytes, position, list.end, &block))
			{
				if (block.type == _block_triangle_range && block.end - block.start >= 8)
				{
					if (submesh_total >= MAXIMUM_SUBMESHES)
						return FALSE;
					submesh_first[submesh_total] = (long)read_u32(bytes + block.start);
					submesh_count[submesh_total] = (long)read_u32(bytes + block.start + 4);
					submesh_material[submesh_total] = 0;
					submesh_total++;
				}
				else if (block.type == _block_submesh_materials && submesh_total > 0 && block.end - block.start >= 4)
				{
					struct block material;

					if (block_find(bytes, block.start + 4, block.end, _block_material_index, &material) &&
						material.end - material.start >= 4)
					{
						submesh_material[submesh_total - 1] = (short)(long)read_u32(bytes + material.start);
					}
				}
				position = block.end;
			}
		}
	}
	if (!submesh_total)
	{
		submesh_first[0] = 0;
		submesh_count[0] = triangle_count;
		submesh_material[0] = 0;
		submesh_total = 1;
	}
	else if (source != node)
	{
		/* (a submesh's first triangle is of this mesh, not of the source) */
		for (index = 0; index < submesh_total; index++)
		{
			if (submesh_first[index] >= triangle_count && submesh_first[index] >= (long)first_triangle)
				submesh_first[index] -= (long)first_triangle;
		}
	}

	*reason = "out of memory";
	vertices = malloc(sizeof(*vertices) * (size_t)vertex_count);
	triangles = malloc(sizeof(*triangles) * 3 * (size_t)triangle_count);
	tangents = malloc(sizeof(*tangents) * (size_t)vertex_count);
	remap = malloc(sizeof(*remap) * (size_t)vertex_count);
	empty = malloc((size_t)vertex_count);
	if (!vertices || !triangles || !tangents || !remap || !empty)
	{
		if (vertices)
			free(vertices);
		if (triangles)
			free(triangles);
		if (tangents)
			free(tangents);
		if (remap)
			free(remap);
		if (empty)
			free(empty);
		return FALSE;
	}
	csmemset(empty, 0, (size_t)vertex_count);

	/* its vertices, in Halo's axes */
	node_world_transform(walk, node, transform);
	normal_transform(transform, normals);
	for (index = 0; index < vertex_count; index++)
	{
		struct model_vertex_uncompressed *vertex = &vertices[index];
		real_point3d point;
		real_vector3d normal = { 0.f, 0.f, 0.f };
		real_point3d world;
		real_vector3d world_normal;
		boolean blank_corner = FALSE;

		if (TEST_FLAG(flags, _mesh_compressed_bit))
		{
			unsigned char const *packed = bytes + data + 8 * index;

			blank_corner = read_short(packed) == 0 && read_short(packed + 2) == 0 &&
				read_short(packed + 4) == 0;
			point.x = unit_short(read_short(packed)) * scale.x + translation.x;
			point.y = unit_short(read_short(packed + 2)) * scale.y + translation.y;
			point.z = unit_short(read_short(packed + 4)) * scale.z + translation.z;
			if (TEST_FLAG(flags, _mesh_normals_bit))
				normal_unpack(read_short(packed + 6), &normal);
		}
		else
		{
			point.x = read_real(bytes + data + 12 * index);
			point.y = read_real(bytes + data + 12 * index + 4);
			point.z = read_real(bytes + data + 12 * index + 8);
		}
		/* (a level mesh's 0x3501: scale then translation, over the source's
		positions, before the node's transform) */
		if (walk->level && node->has_scene_bounds)
		{
			point.x = point.x * node->scene_scale[0] + node->scene_translation[0];
			point.y = point.y * node->scene_scale[1] + node->scene_translation[1];
			point.z = point.z * node->scene_scale[2] + node->scene_translation[2];
			normal.i *= node->scene_scale[0];
			normal.j *= node->scene_scale[1];
			normal.k *= node->scene_scale[2];
		}
		/* (a skin compound's, of no weights, is a copy of a mesh put in the
		space of the bone its first index names, as a warthog's four tires
		are one tire four times) */
		if (blend_indices && !blend_weights)
		{
			long bone = blend_indices[4 * index] < blend_count && blend_nodes[blend_indices[4 * index]] ?
				(long)blend_indices[4 * index] : NONE;

			if (bone != transform_bone)
			{
				if (bone == NONE)
					node_world_transform(walk, node, transform);
				else
					node_world_transform(walk, blend_nodes[bone], transform);
				normal_transform(transform, normals);
				transform_bone = bone;
			}
			if (!compound)
			{
				compound = TRUE;
				anchor[0] = transform[12];
				anchor[1] = -transform[14];
				anchor[2] = transform[13];
			}
		}
		world.x = point.x * transform[0] + point.y * transform[4] + point.z * transform[8] + transform[12];
		world.y = point.x * transform[1] + point.y * transform[5] + point.z * transform[9] + transform[13];
		world.z = point.x * transform[2] + point.y * transform[6] + point.z * transform[10] + transform[14];
		world_normal.i = normal.i * normals[0] + normal.j * normals[3] + normal.k * normals[6];
		world_normal.j = normal.i * normals[1] + normal.j * normals[4] + normal.k * normals[7];
		world_normal.k = normal.i * normals[2] + normal.j * normals[5] + normal.k * normals[8];
		csmemset(vertex, 0, sizeof(*vertex));
		vertex->position.x = world.x;
		vertex->position.y = -world.z;
		vertex->position.z = world.y;
		vertex->normal.i = world_normal.i;
		vertex->normal.j = -world_normal.k;
		vertex->normal.k = world_normal.j;
		normalize3d(&vertex->normal);
		if (!real_finite(world.x) || !real_finite(world.y) || !real_finite(world.z))
		{
			*reason = "a mesh's vertices are not numbers";
			free(vertices);
			free(triangles);
			free(tangents);
			free(remap);
			free(empty);
			return FALSE;
		}
		if (uv_offset >= 0)
		{
			unsigned char const *uv = bytes + vertex_data_start + vertex_size * index + uv_offset;

			vertex->texcoord.x = unit_short(xbox360 ? read_short_big(uv) : read_short(uv));
			vertex->texcoord.y = unit_short(xbox360 ? read_short_big(uv + 2) : read_short(uv + 2));
			/* (the beach's chunks end on a vertex at the origin with no
			texture. A triangle there stretches the sand into a wedge) */
			if (walk->level && blank_corner && vertex->texcoord.x == 0.f && vertex->texcoord.y == 0.f)
				empty[index] = 1;
			/* (a level mesh's repeat is the shader's map scale, applied as
			the texture is sampled. Folded in here, a beach's tile of a few
			hundred is clamped to -1..1 by the renderer and every triangle
			of it shows one texel. A template's tile is small enough to keep) */
			if (node->has_uv_decompress && !walk->level)
			{
				vertex->texcoord.x = vertex->texcoord.x * node->uv_tile[0] + node->uv_shift[0];
				vertex->texcoord.y = vertex->texcoord.y * node->uv_tile[1] + node->uv_shift[1];
			}
		}
		vertex->nodes[0] = (short)rigid_slot;
		vertex->nodes[1] = (short)rigid_slot;
		vertex->node_weights[0] = 1.f;
		vertex->node_weights[1] = 0.f;
		if (blend_indices)
		{
			unsigned char const *indices = blend_indices + 4 * index;
			unsigned char const *weights = blend_weights ? blend_weights + 4 * index : NULL;
			long best[2] = { NONE, NONE };
			long weight[2] = { -1, -1 };
			long influence;

			/* (its two most weighted; a skin compound's weights are none,
			its first index the whole) */
			for (influence = 0; influence < 4; influence++)
			{
				long value = weights ? (long)weights[influence] : (influence ? 0 : 255);

				if (indices[influence] >= blend_count)
					continue;
				if (value > weight[0])
				{
					best[1] = best[0];
					weight[1] = weight[0];
					best[0] = influence;
					weight[0] = value;
				}
				else if (value > weight[1])
				{
					best[1] = influence;
					weight[1] = value;
				}
			}
			if (best[0] != NONE && weight[0] <= 0)
				weight[0] = 255;
			if (best[0] != NONE && blend_slots[indices[best[0]]] != NONE)
			{
				vertex->nodes[0] = vertex->nodes[1] = (short)blend_slots[indices[best[0]]];
				if (best[1] != NONE && weight[1] > 0 && blend_slots[indices[best[1]]] != NONE)
				{
					vertex->nodes[1] = (short)blend_slots[indices[best[1]]];
					vertex->node_weights[0] = (real)weight[0] / (real)(weight[0] + weight[1]);
					vertex->node_weights[1] = 1.f - vertex->node_weights[0];
				}
			}
		}
		tangents[index].i = tangents[index].j = tangents[index].k = 0.f;
	}

	/* (an MCC mesh with no compression parameters whose coordinates sit in
	the half square: a warthog's hull, a marine's capped head, stored at
	half the range, so the face samples the hair at the corner of its
	texture. Twice the coordinate covers the texture. A few of the head's
	eyes reach just past a half, and a mesh that already spans the square,
	the gun and the tires and Johnson's head, is left) */
	if (uv_offset >= 0 && !node->has_uv_decompress && !walk->level && vertex_count > 0)
	{
		long inside = 0;
		real max_u = -1.f;
		real max_v = -1.f;

		for (index = 0; index < vertex_count; index++)
		{
			real u = vertices[index].texcoord.x;
			real v = vertices[index].texcoord.y;

			if (u > max_u)
				max_u = u;
			if (v > max_v)
				max_v = v;
			if (u >= -0.02f && u <= 0.51f && v >= -0.02f && v <= 0.51f)
				inside++;
		}
		if (inside * 10 >= vertex_count * 9 && max_u > 0.35f && max_v > 0.35f &&
			max_u <= 0.56f && max_v <= 0.56f)
		{
			for (index = 0; index < vertex_count; index++)
			{
				vertices[index].texcoord.x *= 2.f;
				vertices[index].texcoord.y *= 2.f;
			}
		}
	}
	/* (a far tree's leaves share the trunk's texture: the bark fills its
	left half and the foliage its right. Cards unwrapped on the left, a
	sapling's, show the bark. Half a texture across, they show the foliage.
	The trunk, which wraps the bark, is left) */
	if (uv_offset >= 0 && vertex_count > 0 &&
		memmem_name(bytes + node->name_start, node->name_length, "leave"))
	{
		boolean farplane = FALSE;
		long scan;

		for (scan = 0; scan < submesh_total; scan++)
		{
			short material = submesh_material[scan];

			if (material >= 0 && material < template_->material_count &&
				strstr(template_->materials[material], "farplane"))
			{
				farplane = TRUE;
				break;
			}
		}
		if (farplane)
		{
			for (scan = 0; scan < vertex_count; scan++)
			{
				if (vertices[scan].texcoord.x < 0.5f)
					vertices[scan].texcoord.x += 0.5f;
			}
		}
	}

	/* its triangles, turned as Halo's front faces are: clockwise about the
	normals the template gives */
	for (index = 0; index < 3 * triangle_count; index++)
	{
		unsigned short vertex = (unsigned short)read_short(bytes + source->triangles.start + 4 + 6 * first_triangle +
			2 * index);

		if (vertex >= vertex_count)
			global_indices = TRUE;
		triangles[index] = vertex;
	}
	if (global_indices)
	{
		if (source == node)
			goto skip_mesh;
		for (index = 0; index < 3 * triangle_count; index++)
		{
			unsigned long vertex = triangles[index];

			if (vertex < base_vertex || vertex >= base_vertex + (unsigned long)vertex_count)
				goto skip_mesh;
			triangles[index] = (unsigned short)(vertex - base_vertex);
		}
	}
	for (index = 0; index < triangle_count; index++)
	{
		struct model_vertex_uncompressed const *a = &vertices[triangles[3 * index]];
		struct model_vertex_uncompressed const *b = &vertices[triangles[3 * index + 1]];
		struct model_vertex_uncompressed const *c = &vertices[triangles[3 * index + 2]];
		real_vector3d ab;
		real_vector3d ac;
		real_vector3d face;
		real_vector3d sum;

		vector_from_points3d(&a->position, &b->position, &ab);
		vector_from_points3d(&a->position, &c->position, &ac);
		cross_product3d(&ab, &ac, &face);
		sum.i = a->normal.i + b->normal.i + c->normal.i;
		sum.j = a->normal.j + b->normal.j + c->normal.j;
		sum.k = a->normal.k + b->normal.k + c->normal.k;
		if (dot_product3d(&face, &sum) > 0.f)
			agree++;
	}
	/* (Halo's front faces turn clockwise about their normals, in its axes:
	(b - a) x (c - a) points in, away from the normal) */
	flip = TEST_FLAG(flags, _mesh_normals_bit) ? agree * 2 > triangle_count : FALSE;
	for (index = 0; flip && index < triangle_count; index++)
	{
		unsigned short swap = triangles[3 * index + 1];

		triangles[3 * index + 1] = triangles[3 * index + 2];
		triangles[3 * index + 2] = swap;
	}
	/* (the rocket launcher's barrel is two rings the length of the weapon
	apart, and its texture coordinate runs from one ring to the other
	straight across the atlas, through the gaps between the pictures.
	Each gap is drawn as a dark ring around the barrel. The barrel's own
	picture is the strip down the right of its texture. A triangle that
	long, whose coordinate sweeps one axis and holds the other, is put on
	that strip: around the barrel across the strip, along the barrel down
	it. The flat parts of the weapon, whose triangles are short, are left) */
	if (!walk->level && uv_offset >= 0 && vertex_count > 0 && triangle_count > 0)
	{
		boolean rocket = FALSE;
		long scan;

		for (scan = 0; scan < submesh_total; scan++)
		{
			short material = submesh_material[scan];

			if (material >= 0 && material < template_->material_count &&
				strstr(template_->materials[material], "rocket_launcher_04"))
			{
				rocket = TRUE;
				break;
			}
		}
		if (rocket)
		{
			unsigned char *wall = malloc((size_t)vertex_count);

			if (wall)
			{
				real u_along_min = 1.f;
				real u_along_max = 0.f;
				real u_around_min = 1.f;
				real u_around_max = 0.f;
				real v_along_min = 1.f;
				real v_along_max = 0.f;
				real v_around_min = 1.f;
				real v_around_max = 0.f;

				csmemset(wall, 0, (size_t)vertex_count);
				for (index = 0; index < triangle_count; index++)
				{
					long corner[3];
					long edge;
					long far0 = 0;
					long far1 = 1;
					real longest = 0.f;
					real shortest = 1e30f;
					real du;
					real dv;

					corner[0] = triangles[3 * index];
					corner[1] = triangles[3 * index + 1];
					corner[2] = triangles[3 * index + 2];
					for (edge = 0; edge < 3; edge++)
					{
						long from = corner[edge];
						long to = corner[(edge + 1) % 3];
						real dx = vertices[to].position.x - vertices[from].position.x;
						real dy = vertices[to].position.y - vertices[from].position.y;
						real dz = vertices[to].position.z - vertices[from].position.z;
						real len = dx * dx + dy * dy + dz * dz;

						if (len > longest)
						{
							longest = len;
							far0 = from;
							far1 = to;
						}
						if (len < shortest)
							shortest = len;
					}
					du = vertices[far1].texcoord.x - vertices[far0].texcoord.x;
					dv = vertices[far1].texcoord.y - vertices[far0].texcoord.y;
					if (du < 0.f)
						du = -du;
					if (dv < 0.f)
						dv = -dv;
					if (shortest > 0.f && longest > shortest * 40.f &&
						((du > 0.04f && dv < 0.02f) || (dv > 0.04f && du < 0.02f)))
					{
						unsigned char kind = (unsigned char)(du >= dv ? 1 : 2);

						for (edge = 0; edge < 3; edge++)
						{
							if (wall[corner[edge]] == 0)
								wall[corner[edge]] = kind;
						}
					}
				}
				for (index = 0; index < vertex_count; index++)
				{
					real u = vertices[index].texcoord.x;
					real v = vertices[index].texcoord.y;

					if (wall[index] == 1)
					{
						if (u < u_along_min)
							u_along_min = u;
						if (u > u_along_max)
							u_along_max = u;
						if (v < u_around_min)
							u_around_min = v;
						if (v > u_around_max)
							u_around_max = v;
					}
					else if (wall[index] == 2)
					{
						if (v < v_along_min)
							v_along_min = v;
						if (v > v_along_max)
							v_along_max = v;
						if (u < v_around_min)
							v_around_min = u;
						if (u > v_around_max)
							v_around_max = u;
					}
				}
				for (index = 0; index < vertex_count; index++)
				{
					real along;
					real around;
					real along_min;
					real along_span;
					real around_min;
					real around_span;

					if (wall[index] == 1)
					{
						along = vertices[index].texcoord.x;
						around = vertices[index].texcoord.y;
						along_min = u_along_min;
						along_span = u_along_max - u_along_min;
						around_min = u_around_min;
						around_span = u_around_max - u_around_min;
					}
					else if (wall[index] == 2)
					{
						along = vertices[index].texcoord.y;
						around = vertices[index].texcoord.x;
						along_min = v_along_min;
						along_span = v_along_max - v_along_min;
						around_min = v_around_min;
						around_span = v_around_max - v_around_min;
					}
					else
						continue;
					vertices[index].texcoord.y = along_span > 0.02f ?
						0.22f + (along - along_min) / along_span * 0.70f : 0.55f;
					vertices[index].texcoord.x = around_span > 0.01f ?
						0.920f + (around - around_min) / around_span * 0.045f : 0.942f;
				}
				free(wall);
			}
		}
	}

	/* (with no normals of its own, its faces') */
	if (!TEST_FLAG(flags, _mesh_normals_bit))
	{
		for (index = 0; index < triangle_count; index++)
		{
			struct model_vertex_uncompressed *a = &vertices[triangles[3 * index]];
			struct model_vertex_uncompressed *b = &vertices[triangles[3 * index + 1]];
			struct model_vertex_uncompressed *c = &vertices[triangles[3 * index + 2]];
			real_vector3d ab;
			real_vector3d ac;
			real_vector3d face;

			vector_from_points3d(&a->position, &b->position, &ab);
			vector_from_points3d(&a->position, &c->position, &ac);
			cross_product3d(&ac, &ab, &face);
			add_vectors3d(&a->normal, &face, &a->normal);
			add_vectors3d(&b->normal, &face, &b->normal);
			add_vectors3d(&c->normal, &face, &c->normal);
		}
		for (index = 0; index < vertex_count; index++)
		{
			if (normalize3d(&vertices[index].normal) < 0.0001f)
			{
				vertices[index].normal.i = vertices[index].normal.j = 0.f;
				vertices[index].normal.k = 1.f;
			}
		}
	}

	/* tangents along u */
	for (index = 0; index < triangle_count; index++)
	{
		long a = triangles[3 * index];
		long b = triangles[3 * index + 1];
		long c = triangles[3 * index + 2];
		real_vector3d ab;
		real_vector3d ac;
		real du1 = vertices[b].texcoord.x - vertices[a].texcoord.x;
		real dv1 = vertices[b].texcoord.y - vertices[a].texcoord.y;
		real du2 = vertices[c].texcoord.x - vertices[a].texcoord.x;
		real dv2 = vertices[c].texcoord.y - vertices[a].texcoord.y;
		real determinant = du1 * dv2 - du2 * dv1;
		real_vector3d tangent;

		if (determinant > -1e-12f && determinant < 1e-12f)
			continue;
		vector_from_points3d(&vertices[a].position, &vertices[b].position, &ab);
		vector_from_points3d(&vertices[a].position, &vertices[c].position, &ac);
		tangent.i = (ab.i * dv2 - ac.i * dv1) / determinant;
		tangent.j = (ab.j * dv2 - ac.j * dv1) / determinant;
		tangent.k = (ab.k * dv2 - ac.k * dv1) / determinant;
		if (!real_finite(tangent.i) || !real_finite(tangent.j) || !real_finite(tangent.k))
			continue;
		add_vectors3d(&tangents[a], &tangent, &tangents[a]);
		add_vectors3d(&tangents[b], &tangent, &tangents[b]);
		add_vectors3d(&tangents[c], &tangent, &tangents[c]);
	}
	for (index = 0; index < vertex_count; index++)
		vertex_basis(&vertices[index], &tangents[index]);

	/* (a beach patch names no tile, unlike the rest of the terrain. Its
	coordinates cover the patch once, so one copy of the sand is smeared
	across it, and each patch by a different amount, which is the torn
	edge between them. The repeat is whatever gives the patch the terrain's
	grain, about three world units a copy. A patch that already tiles is
	left to its tile) */
	if (walk->level && !node->has_uv_decompress && uv_offset >= 0 && triangle_count > 8 &&
		memmem_name(bytes + node->name_start, node->name_length, "terrain"))
	{
		real *ratios = malloc(sizeof(*ratios) * 3 * (size_t)triangle_count);
		long ratio_count = 0;

		if (ratios)
		{
			for (index = 0; index < triangle_count; index++)
			{
				long edge;

				for (edge = 0; edge < 3; edge++)
				{
					struct model_vertex_uncompressed const *from = &vertices[triangles[3 * index + edge]];
					struct model_vertex_uncompressed const *to =
						&vertices[triangles[3 * index + (edge + 1) % 3]];
					real dx = to->position.x - from->position.x;
					real dy = to->position.y - from->position.y;
					real dz = to->position.z - from->position.z;
					real du = to->texcoord.x - from->texcoord.x;
					real dv = to->texcoord.y - from->texcoord.y;
					real world = square_root(dx * dx + dy * dy + dz * dz);
					real length = square_root(du * du + dv * dv);

					if (world > 0.5f && length > 0.f)
						ratios[ratio_count++] = length / world;
				}
			}
			if (ratio_count > 6)
			{
				qsort(ratios, (size_t)ratio_count, sizeof(*ratios), compare_reals);
				beach_median = ratios[ratio_count / 2];
				if (beach_median > 0.0002f && beach_median < 0.2f)
				{
					beach_repeat = 0.3f / beach_median;
					if (beach_repeat > 96.f)
						beach_repeat = 96.f;
				}
			}
			free(ratios);
		}
	}

	/* each submesh's parts, of their vertices alone */
	*reason = "out of memory";
	for (index = 0; index < submesh_total; index++)
	{
		long first = submesh_first[index];
		long count = submesh_count[index];

		if (first < 0 || count <= 0 || first > triangle_count || count > triangle_count - first)
			continue;
		while (count > 0)
		{
			long part_triangles = MIN(count, MAXIMUM_PART_TRIANGLES);
			struct saber_mesh_part *parts = realloc(template_->parts,
				sizeof(*template_->parts) * ((size_t)template_->part_count + 1));
			struct saber_mesh_part *part;
			long triangle;

			if (!parts)
				goto failed;
			template_->parts = parts;
			part = &parts[template_->part_count];
			csmemset(part, 0, sizeof(*part));
			part->material_index = submesh_material[index];
			part->lod = node->lod;
			part->rank = node_rank(walk, node);
			node_variant(walk, node, part->variant, (long)sizeof(part->variant));
			part->compound = (short)compound;
			memcpy(part->anchor, anchor, sizeof(anchor));
			part->uv_scale[0] = 1.f;
			part->uv_scale[1] = 1.f;
			if (walk->level && node->has_uv_decompress)
			{
				if (node->uv_tile[0] != 0.f)
					part->uv_scale[0] = node->uv_tile[0];
				if (node->uv_tile[1] != 0.f)
					part->uv_scale[1] = node->uv_tile[1];
				part->uv_offset[0] = node->uv_shift[0];
				part->uv_offset[1] = node->uv_shift[1];
			}
			else if (beach_repeat != 0.f)
			{
				part->uv_scale[0] = beach_repeat;
				part->uv_scale[1] = beach_repeat;
			}
			part->vertices = malloc(sizeof(*part->vertices) * 3 * (size_t)part_triangles);
			part->triangles = malloc(sizeof(*part->triangles) * 3 * (size_t)part_triangles);
			template_->part_count++;
			if (!part->vertices || !part->triangles)
				goto failed;
			for (triangle = 0; triangle < vertex_count; triangle++)
				remap[triangle] = NONE;
			part->triangle_count = 0;
			for (triangle = 0; triangle < part_triangles; triangle++)
			{
				long corner;
				long vertex[3];
				boolean drop = FALSE;

				for (corner = 0; corner < 3; corner++)
				{
					vertex[corner] = triangles[3 * first + 3 * triangle + corner];
					if (empty[vertex[corner]])
						drop = TRUE;
				}
				/* (an edge many times the patch's usual texture density runs
				off its chart and draws the sand as a striped wedge) */
				if (!drop && beach_median > 0.f)
				{
					long edge;

					for (edge = 0; edge < 3; edge++)
					{
						struct model_vertex_uncompressed const *from = &vertices[vertex[edge]];
						struct model_vertex_uncompressed const *to = &vertices[vertex[(edge + 1) % 3]];
						real dx = to->position.x - from->position.x;
						real dy = to->position.y - from->position.y;
						real dz = to->position.z - from->position.z;
						real du = to->texcoord.x - from->texcoord.x;
						real dv = to->texcoord.y - from->texcoord.y;
						real world = square_root(dx * dx + dy * dy + dz * dz);
						real length = square_root(du * du + dv * dv);

						if (world > 0.5f && length > world * beach_median * 8.f)
							drop = TRUE;
					}
				}
				if (drop)
					continue;
				for (corner = 0; corner < 3; corner++)
				{
					if (remap[vertex[corner]] == NONE)
					{
						remap[vertex[corner]] = part->vertex_count;
						part->vertices[part->vertex_count++] = vertices[vertex[corner]];
					}
					part->triangles[3 * part->triangle_count + corner] = (unsigned short)remap[vertex[corner]];
				}
				part->triangle_count++;
			}
			if (part->triangle_count == 0)
			{
				free(part->vertices);
				free(part->triangles);
				template_->part_count--;
				first += part_triangles;
				count -= part_triangles;
				continue;
			}
			first += part_triangles;
			count -= part_triangles;
		}
	}
	free(vertices);
	free(triangles);
	free(tangents);
	free(remap);
	free(empty);

	return TRUE;

skip_mesh:
	free(vertices);
	free(triangles);
	free(tangents);
	free(remap);
	free(empty);

	return TRUE;

failed:
	free(vertices);
	free(triangles);
	free(tangents);
	free(remap);
	free(empty);

	return FALSE;
}

/* ---------- public code */

struct saber_pak *saber_pak_open(
	char const *path,
	char const **reason)
{
	struct saber_pak *pak = malloc(sizeof(*pak));
	unsigned char words[16];

	*reason = "out of memory";
	if (!pak)
		return NULL;
	csmemset(pak, 0, sizeof(*pak));
	*reason = "there is no such file";
	pak->file = config_host_file_open(path, &pak->file_size);
	if (!pak->file)
	{
		free(pak);
		return NULL;
	}
	*reason = "it cannot be read";
	if (pak->file_size < 16)
		goto refused;
	if (!file_read(pak, 0, words, 16))
		goto refused;
	/* (the Collection's zlib chunks, else the 360's LZX blocks: a
	compressed package's fourth word is a block's offset, or 0) */
	if (mcc_package(pak, read_u32(words), read_u32(words + 4)))
	{
		pak->compressed = TRUE;
		pak->zlib_chunks = TRUE;
		pak->block_count = (long)read_u32(words);
		if (!zlib_blocks_open(pak, reason))
			goto refused;
	}
	else if ((long)read_u32(words + 12) <= 0 || read_u32(words + 12) > MAXIMUM_NAME_LENGTH)
	{
		pak->compressed = TRUE;
		pak->xbox360 = TRUE;
		pak->block_count = (long)read_u32(words);
		if (!blocks_open(pak, reason))
			goto refused;
	}
	else
	{
		pak->stream_size = pak->file_size;
	}
	if (pak->zlib_chunks)
	{
		if (ipak_stream(pak))
		{
			if (!ipak_entries(pak, reason))
				goto refused;
		}
		else if (!directory_read(pak, reason))
		{
			goto refused;
		}
	}
	else if (!directory_read(pak, reason))
	{
		goto refused;
	}

	return pak;

refused:
	saber_pak_close(pak);

	return NULL;
}

void saber_pak_close(
	struct saber_pak *pak)
{
	if (!pak)
		return;
	if (pak->file)
		config_host_file_close(pak->file);
	if (pak->block_offsets)
		free(pak->block_offsets);
	if (pak->block_starts)
		free(pak->block_starts);
	if (pak->lzx)
		free(pak->lzx);
	if (pak->window)
		free(pak->window);
	if (pak->block)
		free(pak->block);
	if (pak->compressed_block)
		free(pak->compressed_block);
	if (pak->entries)
		free(pak->entries);
	if (pak->names)
		free(pak->names);
	free(pak);
}

int saber_pak_xbox360(
	struct saber_pak const *pak)
{
	return pak->xbox360;
}

long saber_pak_entry_count(
	struct saber_pak const *pak)
{
	return pak->entry_count;
}

char const *saber_pak_entry_name(
	struct saber_pak const *pak,
	long entry)
{
	return entry >= 0 && entry < pak->entry_count ? pak->entries[entry].name : "";
}

long saber_pak_entry_type(
	struct saber_pak const *pak,
	long entry)
{
	return entry >= 0 && entry < pak->entry_count ? pak->entries[entry].type : NONE;
}

long saber_pak_find(
	struct saber_pak const *pak,
	char const *name,
	long type)
{
	long index;

	for (index = 0; index < pak->entry_count; index++)
	{
		if (pak->entries[index].type == type && !strcmp(pak->entries[index].name, name))
			return index;
	}

	return NONE;
}

unsigned char *saber_pak_read(
	struct saber_pak *pak,
	long entry,
	unsigned long *size)
{
	unsigned char *bytes;

	if (entry < 0 || entry >= pak->entry_count || pak->entries[entry].size > MAXIMUM_ENTRY_SIZE)
		return NULL;
	bytes = malloc((size_t)pak->entries[entry].size + 1);
	if (!bytes)
		return NULL;
	if (!stream_read(pak, pak->entries[entry].offset, bytes, pak->entries[entry].size))
	{
		free(bytes);
		return NULL;
	}
	*size = pak->entries[entry].size;

	return bytes;
}

static int template_blocks_read(struct saber_template *template_, unsigned char const *bytes, unsigned long size,
	struct block const *root, int xbox360, boolean level, char const **reason);

/* whether a level's placed object is its sky (the dome, the clouds, the
horizon), not a cinematic's or the water's */
static boolean level_object_sky(
	char const *name)
{
	return strstr(name, "sky") != NULL || strstr(name, "cloud") != NULL || strstr(name, "horizont") != NULL;
}

/* the placed object's name, from its 0xE502 ("LPTA" and the name), or
FALSE when it has none */
static boolean object_template_name(
	unsigned char const *bytes,
	struct block const *object,
	char *name,
	unsigned long size)
{
	struct block block;
	unsigned long start;
	unsigned long length;
	unsigned long index;

	name[0] = 0;
	if (!size || !block_find(bytes, object->start, object->end, _block_object_name, &block))
		return FALSE;
	start = block.start;
	if (block.end - start >= 4 && bytes[start] == 'L' && bytes[start + 1] == 'P' && bytes[start + 2] == 'T' &&
		bytes[start + 3] == 'A')
	{
		start += 4;
	}
	length = 0;
	while (start + length < block.end && bytes[start + length] && length + 1 < size)
		length++;
	for (index = 0; index < length; index++)
		name[index] = (char)bytes[start + index];
	name[length] = 0;

	return length > 0;
}

/* one placed object of a level: its nodes read on their own (their ids are
not the level's) and its meshes added to the level. A sky object's parts
are marked so. A mesh that cannot be read leaves the object there */
static void level_object_read(
	struct saber_template *template_,
	unsigned char const *bytes,
	unsigned long size,
	struct block const *object,
	int xbox360)
{
	char name[64];
	struct template_walk walk;
	long first;
	long index;
	char const *reason = NULL;

	if (!object_template_name(bytes, object, name, sizeof(name)) || !level_object_sky(name))
		return;
	csmemset(&walk, 0, sizeof(walk));
	walk.bytes = bytes;
	walk.size = size;
	walk.level = TRUE;
	walk.nodes = malloc(sizeof(*walk.nodes) * MAXIMUM_NODES);
	if (!walk.nodes)
		return;
	nodes_read(&walk, object->start, object->end, 0);
	if (walk.failed)
	{
		free(walk.nodes);
		return;
	}
	first = template_->part_count;
	for (index = 0; index < walk.node_count; index++)
	{
		if (!mesh_read(template_, &walk, &walk.nodes[index], xbox360 ? TRUE : FALSE, &reason))
			break;
	}
	free(walk.nodes);
	for (index = first; index < template_->part_count; index++)
		template_->parts[index].sky = 1;
}

/* a level's placed objects (0xEA01): the skydome and the cloud layers live
here, not among the level's own nodes */
static void level_objects_read(
	struct saber_template *template_,
	unsigned char const *bytes,
	unsigned long size,
	struct block const *root,
	int xbox360)
{
	unsigned long position = root->start;
	struct block block;

	while (position < root->end && block_at(bytes, position, root->end, &block))
	{
		if (block.type == _block_objects && block.end - block.start >= 4)
		{
			long count = (long)read_u32(bytes + block.start);
			unsigned long at = block.start + 4;
			struct block object;

			if (count > 0 && count <= 64)
			{
				while (count > 0 && at < block.end && block_at(bytes, at, block.end, &object))
				{
					if (object.type == _block_template)
						level_object_read(template_, bytes, size, &object, xbox360);
					at = object.end;
					count--;
				}
			}
		}
		position = block.end;
	}
}

int saber_template_read(
	struct saber_template *template_,
	unsigned char const *bytes,
	unsigned long size,
	int xbox360,
	char const **reason)
{
	struct block root;

	csmemset(template_, 0, sizeof(*template_));
	*reason = "it is not a template";
	if (!block_at(bytes, 0, size, &root) || root.type != _block_template)
		return FALSE;

	return template_blocks_read(template_, bytes, size, &root, xbox360, FALSE, reason);
}

int saber_level_geometry_read(
	struct saber_template *template_,
	unsigned char const *bytes,
	unsigned long size,
	int xbox360,
	char const **reason)
{
	struct block root;

	csmemset(template_, 0, sizeof(*template_));
	root.type = _block_template;
	root.start = 0;
	root.end = size;

	return template_blocks_read(template_, bytes, size, &root, xbox360, TRUE, reason);
}

/* a skin compound's vertices and the box they sit in (its compressed
positions' translation and scale, or the plain positions' own) */
static boolean compound_bounds(
	struct template_walk const *walk,
	struct template_node const *node,
	long *vertices,
	real *low,
	real *high)
{
	unsigned char const *bytes = walk->bytes;
	long count;
	unsigned long data;
	long axis;

	if (node->positions.end - node->positions.start < 4)
		return FALSE;
	count = (long)read_u32(bytes + node->positions.start);
	if (count <= 0 || count > 0x100000)
		return FALSE;
	data = node->positions.start + 4;
	if (node->has_mesh_flags && TEST_FLAG(node->mesh_flags, _mesh_compressed_bit))
	{
		short packed[6];

		if (node->positions.end - data < 12)
			return FALSE;
		for (axis = 0; axis < 6; axis++)
			packed[axis] = read_short(bytes + data + 2 * (unsigned long)axis);
		for (axis = 0; axis < 3; axis++)
		{
			real scale = (real)packed[axis + 3];

			if (scale < 0.f)
				scale = -scale;
			low[axis] = (real)packed[axis] - scale;
			high[axis] = (real)packed[axis] + scale;
		}
	}
	else
	{
		long index;

		if ((unsigned long)count * 12 > node->positions.end - data)
			return FALSE;
		for (axis = 0; axis < 3; axis++)
		{
			low[axis] = read_real(bytes + data + 4 * axis);
			high[axis] = low[axis];
		}
		for (index = 1; index < count; index++)
		{
			for (axis = 0; axis < 3; axis++)
			{
				real value = read_real(bytes + data + 12 * (unsigned long)index + 4 * (unsigned long)axis);

				if (value < low[axis])
					low[axis] = value;
				if (value > high[axis])
					high[axis] = value;
			}
		}
	}
	*vertices = count;

	return TRUE;
}

/* two compounds of one mesh: each side within a quarter of the longer,
and the middles that close. A weapon's lower detail is the same gun; a
pelican's hull and its glass are not */
static boolean box_same_shape(
	real const *low_a,
	real const *high_a,
	real const *low_b,
	real const *high_b)
{
	long axis;

	for (axis = 0; axis < 3; axis++)
	{
		real sa = high_a[axis] - low_a[axis];
		real sb = high_b[axis] - low_b[axis];
		real longer = sa > sb ? sa : sb;
		real side = sa > sb ? sa - sb : sb - sa;
		real middle = (low_a[axis] + high_a[axis]) - (low_b[axis] + high_b[axis]);

		if (middle < 0.f)
			middle = -middle;
		middle /= 2.f;
		if (longer < 1.f)
			longer = 1.f;
		if (side > longer / 4.f || middle > longer / 4.f)
			return FALSE;
	}

	return TRUE;
}

/* a weapon's SkinCompound_2 is SkinCompound_1 with fewer triangles, the
same gun, and drawn together the coarser one covers the other. A pelican's
compounds are the pieces of the ship, and drawing one of them leaves it
the glass. The fullest compound is the model when it is at least half of
them; a copy of its box with seven tenths of the vertices or fewer is the
next level, and the rest of the pieces stay with the fullest */
static void skin_compound_lods(
	struct template_walk *walk)
{
	struct template_node *nodes[64];
	long vertices[64];
	real low[64][3];
	real high[64][3];
	long count = 0;
	long total = 0;
	long richest = 0;
	long index;
	long level;
	long previous;

	for (index = 0; index < walk->node_count && count < (long)NUMBEROF(nodes); index++)
	{
		struct template_node *node = &walk->nodes[index];

		if (!node->name_length || node->lod != 0 ||
			!memmem_name(walk->bytes + node->name_start, node->name_length, "SkinCompound_"))
		{
			continue;
		}
		if (!compound_bounds(walk, node, &vertices[count], low[count], high[count]))
			continue;
		nodes[count] = node;
		total += vertices[count];
		if (vertices[count] > vertices[richest])
			richest = count;
		count++;
	}
	if (count < 2 || vertices[richest] * 2 < total)
		return;
	previous = vertices[richest];
	level = 0;
	for (;;)
	{
		long next = NONE;
		long next_vertices = 0;

		for (index = 0; index < count; index++)
		{
			if (nodes[index]->lod != 0 || index == richest)
				continue;
			if (!box_same_shape(low[richest], high[richest], low[index], high[index]))
				continue;
			if (vertices[index] * 10 > previous * 7)
				continue;
			if (next == NONE || vertices[index] > next_vertices)
			{
				next = index;
				next_vertices = vertices[index];
			}
		}
		if (next == NONE || level >= 4)
			break;
		level++;
		nodes[next]->lod = (short)level;
		previous = next_vertices;
	}
}

/* a template of the blocks in `root` (a template's, or a level's whole
geometry, of the same blocks at its top) */
static int template_blocks_read(
	struct saber_template *template_,
	unsigned char const *bytes,
	unsigned long size,
	struct block const *root_block,
	int xbox360,
	boolean level,
	char const **reason)
{
	struct template_walk walk;
	struct block root = *root_block;
	struct block materials;
	long index;

	/* its materials */
	if (block_find(bytes, root.start, root.end, _block_materials, &materials) && materials.end - materials.start >= 4)
	{
		unsigned long position = materials.start + 4;
		struct block block;

		while (position < materials.end && block_at(bytes, position, materials.end, &block))
		{
			if (block.type == _block_material)
			{
				char (*names)[64] = realloc(template_->materials,
					sizeof(*template_->materials) * ((size_t)template_->material_count + 1));
				unsigned long length = MIN(block.end - block.start, sizeof(names[0]) - 1);

				*reason = "out of memory";
				if (!names)
					return FALSE;
				template_->materials = names;
				memcpy(names[template_->material_count], bytes + block.start, length);
				names[template_->material_count][length] = 0;
				template_->material_count++;
			}
			position = block.end;
		}
	}

	/* its nodes, and their meshes */
	*reason = "out of memory";
	csmemset(&walk, 0, sizeof(walk));
	walk.bytes = bytes;
	walk.size = size;
	walk.level = level;
	walk.nodes = malloc(sizeof(*walk.nodes) * MAXIMUM_NODES);
	if (!walk.nodes)
		return FALSE;
	nodes_read(&walk, root.start, root.end, 0);
	*reason = "its nodes are not a template's";
	if (walk.failed)
	{
		free(walk.nodes);
		return FALSE;
	}
	skin_compound_lods(&walk);
	for (index = 0; index < walk.node_count; index++)
	{
		if (!mesh_read(template_, &walk, &walk.nodes[index], xbox360 ? TRUE : FALSE, reason))
		{
			free(walk.nodes);
			return FALSE;
		}
	}
	free(walk.nodes);
	/* (a level's skydome and cloud layers are placed objects, after its
	own nodes) */
	if (level)
		level_objects_read(template_, bytes, size, &root, xbox360);
	*reason = "it has no mesh";

	return template_->part_count > 0;
}

void saber_template_dispose(
	struct saber_template *template_)
{
	long index;

	for (index = 0; index < template_->part_count; index++)
	{
		if (template_->parts[index].vertices)
			free(template_->parts[index].vertices);
		if (template_->parts[index].triangles)
			free(template_->parts[index].triangles);
	}
	if (template_->parts)
		free(template_->parts);
	if (template_->materials)
		free(template_->materials);
	if (template_->bones)
		free(template_->bones);
	if (template_->bone_parents)
		free(template_->bone_parents);
	if (template_->bone_binds)
		free(template_->bone_binds);
	if (template_->bone_origins)
		free(template_->bone_origins);
	csmemset(template_, 0, sizeof(*template_));
}

int saber_texture_read(
	struct saber_texture *texture,
	unsigned char const *bytes,
	unsigned long size)
{
	boolean big_endian;
	long width;
	long height;
	long format;
	long levels = 1;
	unsigned long data;
	long block_size;
	long total = 0;
	long level;

	csmemset(texture, 0, sizeof(*texture));
	if (size >= 58 && !memcmp(bytes + 6, "TCIP", 4))
	{
		big_endian = FALSE;
		width = (long)read_u32(bytes + 16);
		height = (long)read_u32(bytes + 20);
		format = (long)read_u32(bytes + 38);
		/* (its mipmaps' block, 0xF9, before its pixels') */
		if (bytes[42] == 0xF9 && bytes[43] == 0x00)
			levels = (long)read_u32(bytes + 48);
		data = 58;
	}
	else if (size >= 4096 && !memcmp(bytes + 8, "PICT", 4))
	{
		big_endian = TRUE;
		width = (long)read_u32_big(bytes + 12);
		height = (long)read_u32_big(bytes + 16);
		format = (long)read_u32_big(bytes + 32);
		data = 4096;
	}
	else
	{
		return FALSE;
	}
	switch (format)
	{
	case 12:
	case 13:
	case 70:
		texture->format = _bitmap_format_dxt1;
		block_size = 8;
		break;
	case 15:
	case 73:
		texture->format = _bitmap_format_dxt3;
		block_size = 16;
		break;
	case 17:
	case 76:
		texture->format = _bitmap_format_dxt5;
		block_size = 16;
		break;
	default:
		return FALSE;
	}
	if (width < 4 || height < 4 || width > 4096 || height > 4096 || (width & (width - 1)) || (height & (height - 1)))
		return FALSE;
	/* (as many of its mipmaps as are there, down to 4x4) */
	if (levels < 1)
		levels = 1;
	for (level = 0; level < levels; level++)
	{
		long level_width = width >> level;
		long level_height = height >> level;
		long level_size;

		if (level_width < 4 || level_height < 4)
			break;
		level_size = (level_width / 4) * (level_height / 4) * block_size;
		if ((unsigned long)(total + level_size) > size - data)
			break;
		total += level_size;
	}
	if (!level)
		return FALSE;
	texture->pixels = malloc((size_t)total);
	if (!texture->pixels)
		return FALSE;
	memcpy(texture->pixels, bytes + data, (size_t)total);
	if (big_endian)
	{
		long index;

		for (index = 0; index + 1 < total; index += 2)
		{
			unsigned char swap = texture->pixels[index];

			texture->pixels[index] = texture->pixels[index + 1];
			texture->pixels[index + 1] = swap;
		}
	}
	texture->width = (short)width;
	texture->height = (short)height;
	texture->mipmap_count = (short)(level - 1);
	texture->pixels_size = total;

	return TRUE;
}

/* how many texels are clear (alpha below 64) and solid (foliage: above
128, otherwise above 192). FALSE when the texture is not one this counts */
static int texture_alpha_counts(
	struct saber_texture const *texture,
	int foliage,
	long *clear_count,
	long *solid_count,
	long *texel_count)
{
	long block_size = texture->format == _bitmap_format_dxt1 ? 8 : 16;
	long blocks = (long)(texture->width / 4) * (long)(texture->height / 4);
	long clear = 0;
	long solid = 0;
	long block;

	if (!texture->pixels || blocks <= 0 || blocks * block_size > texture->pixels_size)
		return FALSE;
	for (block = 0; block < blocks; block++)
	{
		unsigned char const *bytes = texture->pixels + block * block_size;
		long texel;

		if (texture->format == _bitmap_format_dxt1)
		{
			unsigned long color0 = (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8);
			unsigned long color1 = (unsigned long)bytes[2] | ((unsigned long)bytes[3] << 8);

			for (texel = 0; texel < 16; texel++)
			{
				long index = (bytes[4 + texel / 4] >> (2 * (texel % 4))) & 3;

				if (color0 <= color1 && index == 3)
					clear++;
				else
					solid++;
			}
		}
		else if (texture->format == _bitmap_format_dxt3)
		{
			for (texel = 0; texel < 16; texel++)
			{
				long alpha = ((bytes[texel / 2] >> (4 * (texel % 2))) & 15) * 17;

				clear += alpha < 64;
				solid += alpha > (foliage ? 128 : 192);
			}
		}
		else
		{
			long alpha0 = bytes[0];
			long alpha1 = bytes[1];
			unsigned long low = (unsigned long)bytes[2] | ((unsigned long)bytes[3] << 8) | ((unsigned long)bytes[4] << 16);
			unsigned long high = (unsigned long)bytes[5] | ((unsigned long)bytes[6] << 8) | ((unsigned long)bytes[7] << 16);

			for (texel = 0; texel < 16; texel++)
			{
				long index = (long)(((texel < 8 ? low : high) >> (3 * (texel % 8))) & 7);
				long alpha;

				if (index == 0)
					alpha = alpha0;
				else if (index == 1)
					alpha = alpha1;
				else if (alpha0 > alpha1)
					alpha = ((8 - index) * alpha0 + (index - 1) * alpha1) / 7;
				else if (index < 6)
					alpha = ((6 - index) * alpha0 + (index - 1) * alpha1) / 5;
				else
					alpha = index == 6 ? 0 : 255;
				clear += alpha < 64;
				solid += alpha > (foliage ? 128 : 192);
			}
		}
	}

	*clear_count = clear;
	*solid_count = solid;
	*texel_count = blocks * 16;

	return TRUE;
}

int saber_texture_cutout(
	struct saber_texture const *texture,
	int foliage)
{
	long clear;
	long solid;
	long texels;

	/* (a twentieth clear and a fifth solid, at least; foliage's a tenth
	more than half solid, its leaves' edges soft) */
	if (!texture_alpha_counts(texture, foliage, &clear, &solid, &texels))
		return FALSE;

	return clear * 20 >= texels && solid * (foliage ? 10 : 5) >= texels;
}

int saber_texture_shaded_alpha(
	struct saber_texture const *texture)
{
	long clear;
	long solid;
	long texels;
	long middle;

	/* (a silhouette's edge is the thin band between clear and solid. A
	shade laid across the picture, the pelican bay's, has at least as
	many texels in that band as it has solid, and cutting it out drops
	the faces that land there) */
	if (!texture_alpha_counts(texture, FALSE, &clear, &solid, &texels))
		return FALSE;
	middle = texels - clear - solid;

	return middle >= solid;
}

/* a 565 colour's blue of a value 0 to 255 */
static unsigned long blue565(
	long value)
{
	return (unsigned long)((value * 31 + 127) / 255);
}

/* the colour of a texture's texel (its most detailed mipmap), each channel
of 255 */
static void texture_texel(
	struct saber_texture const *texture,
	long x,
	long y,
	long *red,
	long *green,
	long *blue)
{
	long block_size = texture->format == _bitmap_format_dxt1 ? 8 : 16;
	long blocks_wide = MAX(texture->width / 4, 1);
	long offset = ((y / 4) * blocks_wide + x / 4) * block_size + (block_size - 8);
	unsigned char const *block;
	unsigned long colors[2];
	long channels[4][3];
	long index;
	long which;

	*red = *green = *blue = 0;
	if (!texture->pixels || offset < 0 || offset + 8 > texture->pixels_size)
		return;
	block = texture->pixels + offset;
	colors[0] = (unsigned long)block[0] | ((unsigned long)block[1] << 8);
	colors[1] = (unsigned long)block[2] | ((unsigned long)block[3] << 8);
	for (which = 0; which < 2; which++)
	{
		channels[which][0] = (long)((colors[which] >> 11) & 31) * 255 / 31;
		channels[which][1] = (long)((colors[which] >> 5) & 63) * 255 / 63;
		channels[which][2] = (long)(colors[which] & 31) * 255 / 31;
	}
	for (which = 0; which < 3; which++)
	{
		if (colors[0] > colors[1] || texture->format != _bitmap_format_dxt1)
		{
			channels[2][which] = (2 * channels[0][which] + channels[1][which]) / 3;
			channels[3][which] = (channels[0][which] + 2 * channels[1][which]) / 3;
		}
		else
		{
			channels[2][which] = (channels[0][which] + channels[1][which]) / 2;
			channels[3][which] = 0;
		}
	}
	index = (long)((block[4 + (y % 4)] >> (2 * (x % 4))) & 3);
	*red = channels[index][0];
	*green = channels[index][1];
	*blue = channels[index][2];
}

int saber_texture_read_mask(
	struct saber_texture *texture,
	unsigned char const *bytes,
	unsigned long size,
	int banded,
	struct saber_texture const *base)
{
	boolean big_endian;
	long width;
	long height;
	long format;
	unsigned long data;
	unsigned char *mask;
	long blocks_wide;
	long blocks_high;
	long x;
	long y;

	csmemset(texture, 0, sizeof(*texture));
	if (size >= 58 && !memcmp(bytes + 6, "TCIP", 4))
	{
		big_endian = FALSE;
		width = (long)read_u32(bytes + 16);
		height = (long)read_u32(bytes + 20);
		format = (long)read_u32(bytes + 38);
		data = 58;
	}
	else if (size >= 4096 && !memcmp(bytes + 8, "PICT", 4))
	{
		big_endian = TRUE;
		width = (long)read_u32_big(bytes + 12);
		height = (long)read_u32_big(bytes + 16);
		format = (long)read_u32_big(bytes + 32);
		data = 4096;
	}
	else
	{
		return FALSE;
	}
	if (width < 4 || height < 4 || width > 4096 || height > 4096 || (width & (width - 1)) || (height & (height - 1)))
		return FALSE;
	mask = malloc((size_t)width * (size_t)height);
	if (!mask)
		return FALSE;
	blocks_wide = width / 4;
	blocks_high = height / 4;
	if (format == 37 || format == 79)
	{
		/* (one channel, DXT5's alpha: a block of 8 bytes; the Xbox 360's
		16-bit words byte-swapped) */
		if ((unsigned long)(blocks_wide * blocks_high * 8) > size - data)
		{
			free(mask);
			return FALSE;
		}
		for (y = 0; y < blocks_high; y++)
		{
			for (x = 0; x < blocks_wide; x++)
			{
				unsigned char block[8];
				long texel;
				unsigned long low;
				unsigned long high;

				memcpy(block, bytes + data + 8 * (y * blocks_wide + x), 8);
				if (big_endian)
				{
					long index;

					for (index = 0; index < 8; index += 2)
					{
						unsigned char swap = block[index];

						block[index] = block[index + 1];
						block[index + 1] = swap;
					}
				}
				low = (unsigned long)block[2] | ((unsigned long)block[3] << 8) | ((unsigned long)block[4] << 16);
				high = (unsigned long)block[5] | ((unsigned long)block[6] << 8) | ((unsigned long)block[7] << 16);
				for (texel = 0; texel < 16; texel++)
				{
					long index = (long)(((texel < 8 ? low : high) >> (3 * (texel % 8))) & 7);
					long alpha0 = block[0];
					long alpha1 = block[1];
					long value;

					if (index == 0)
						value = alpha0;
					else if (index == 1)
						value = alpha1;
					else if (alpha0 > alpha1)
						value = ((8 - index) * alpha0 + (index - 1) * alpha1) / 7;
					else if (index < 6)
						value = ((6 - index) * alpha0 + (index - 1) * alpha1) / 5;
					else
						value = index == 6 ? 0 : 255;
					mask[(4 * y + texel / 4) * width + 4 * x + texel % 4] = (unsigned char)value;
				}
			}
		}
	}
	else if (format == 0 || format == 22 || format == 90)
	{
		/* (four bytes a texel, its first channel: B of the PC's BGRA, of
		the Xbox 360's ARGB once its 32-bit words are turned round) */
		long texel;

		if ((unsigned long)(width * height * 4) > size - data)
		{
			free(mask);
			return FALSE;
		}
		for (texel = 0; texel < width * height; texel++)
			mask[texel] = bytes[data + 4 * texel + (big_endian ? 3 : 0)];
	}
	else
	{
		free(mask);
		return FALSE;
	}

	/* (a banded mask: its background, its most common value, none; else
	whole) */
	if (banded)
	{
		long counts[256];
		long background = 0;
		long texel;

		csmemset(counts, 0, sizeof(counts));
		for (texel = 0; texel < width * height; texel++)
			counts[mask[texel]]++;
		for (texel = 1; texel < 256; texel++)
		{
			if (counts[texel] > counts[background])
				background = texel;
		}
		for (texel = 0; texel < width * height; texel++)
		{
			long apart = (long)mask[texel] - background;

			mask[texel] = (unsigned char)(apart > 8 || apart < -8 ? 255 : 0);
			/* (but not where the texture is coloured already: an Elite's
			armour baked blue, which colour change would darken; its grey
			plates take it) */
			if (mask[texel] && base && base->width > 0 && base->height > 0)
			{
				long red;
				long green;
				long blue;
				long most;
				long least;

				texture_texel(base, (texel % width) * base->width / width, (texel / width) * base->height / height,
					&red, &green, &blue);
				most = MAX(red, MAX(green, blue));
				least = MIN(red, MIN(green, blue));
				if (most > 24 && (most - least) * 5 > most)
					mask[texel] = 0;
			}
		}
	}

	/* the mask as DXT1, in blue */
	texture->pixels = malloc((size_t)blocks_wide * (size_t)blocks_high * 8);
	if (!texture->pixels)
	{
		free(mask);
		return FALSE;
	}
	for (y = 0; y < blocks_high; y++)
	{
		for (x = 0; x < blocks_wide; x++)
		{
			unsigned char *block = texture->pixels + 8 * (y * blocks_wide + x);
			long low = 255;
			long high = 0;
			unsigned long color0;
			unsigned long color1;
			unsigned long indices = 0;
			long texel;

			for (texel = 0; texel < 16; texel++)
			{
				long value = mask[(4 * y + texel / 4) * width + 4 * x + texel % 4];

				low = MIN(low, value);
				high = MAX(high, value);
			}
			color0 = blue565(high);
			color1 = blue565(low);
			if (color0 > color1)
			{
				for (texel = 0; texel < 16; texel++)
				{
					long value = mask[(4 * y + texel / 4) * width + 4 * x + texel % 4];
					/* (the palette: color0, color1, and two between, a third and
					two thirds of the way from color0) */
					long step = (high - value) * 3 / MAX(high - low, 1);
					long index = step <= 0 ? 0 : step >= 3 ? 1 : step == 1 ? 2 : 3;

					indices |= (unsigned long)index << (2 * texel);
				}
			}
			else
			{
				color1 = color0;
			}
			block[0] = (unsigned char)color0;
			block[1] = (unsigned char)(color0 >> 8);
			block[2] = (unsigned char)color1;
			block[3] = (unsigned char)(color1 >> 8);
			block[4] = (unsigned char)indices;
			block[5] = (unsigned char)(indices >> 8);
			block[6] = (unsigned char)(indices >> 16);
			block[7] = (unsigned char)(indices >> 24);
		}
	}
	free(mask);
	texture->width = (short)width;
	texture->height = (short)height;
	texture->mipmap_count = 0;
	texture->format = _bitmap_format_dxt1;
	texture->pixels_size = blocks_wide * blocks_high * 8;

	return TRUE;
}

int saber_texture_blank(
	struct saber_texture *texture)
{
	csmemset(texture, 0, sizeof(*texture));
	texture->pixels = malloc(8);
	if (!texture->pixels)
		return FALSE;
	memset(texture->pixels, 0, 8);
	texture->width = 4;
	texture->height = 4;
	texture->format = _bitmap_format_dxt1;
	texture->pixels_size = 8;

	return TRUE;
}

/* a 565 colour from channels of 255 */
static unsigned long pack565(
	long red,
	long green,
	long blue)
{
	return ((unsigned long)((red * 31 + 127) / 255) << 11) |
		((unsigned long)((green * 63 + 127) / 255) << 5) |
		(unsigned long)((blue * 31 + 127) / 255);
}

/* one channel, of 255, of a 565 colour */
static long unpack565(
	unsigned long color,
	long channel)
{
	if (channel == 0)
		return (long)((color >> 11) & 31) * 255 / 31;
	if (channel == 1)
		return (long)((color >> 5) & 63) * 255 / 63;

	return (long)(color & 31) * 255 / 31;
}

/* a DXT block's texel alpha, of 255 */
static long block_texel_alpha(
	unsigned char const *block,
	long format,
	long texel)
{
	if (format == _bitmap_format_dxt3)
		return ((block[texel / 2] >> (4 * (texel % 2))) & 15) * 17;

	{
		long alpha0 = block[0];
		long alpha1 = block[1];
		unsigned long bits = texel < 8 ?
			(unsigned long)block[2] | ((unsigned long)block[3] << 8) | ((unsigned long)block[4] << 16) :
			(unsigned long)block[5] | ((unsigned long)block[6] << 8) | ((unsigned long)block[7] << 16);
		long index = (long)((bits >> (3 * (texel % 8))) & 7);

		if (index == 0)
			return alpha0;
		if (index == 1)
			return alpha1;
		if (alpha0 > alpha1)
			return ((8 - index) * alpha0 + (index - 1) * alpha1) / 7;
		if (index < 6)
			return ((6 - index) * alpha0 + (index - 1) * alpha1) / 5;

		return index == 6 ? 0 : 255;
	}
}

/* a star field's black punched out (DXT1: colour 0 no greater than colour
1, and index 3 clear). The black is the sky behind the stars, and it is
stored with no alpha. A block blends a star into that black, so the grey
around a star is the same sky and is punched with it */
static void texture_punch_black(
	struct saber_texture *texture)
{
	long offset;

	if (!texture->pixels || texture->format != _bitmap_format_dxt1)
		return;
	for (offset = 0; offset + 8 <= texture->pixels_size; offset += 8)
	{
		unsigned char *block = texture->pixels + offset;
		unsigned long color0 = (unsigned long)block[0] | ((unsigned long)block[1] << 8);
		unsigned long color1 = (unsigned long)block[2] | ((unsigned long)block[3] << 8);
		long red[4];
		long green[4];
		long blue[4];
		unsigned char texel_red[16];
		unsigned char texel_green[16];
		unsigned char texel_blue[16];
		unsigned char dark[16];
		long dark_count = 0;
		long darkest = 0;
		long brightest = 0;
		long dark_luma = 255;
		long bright_luma = -1;
		long channel;
		long texel;

		for (channel = 0; channel < 2; channel++)
		{
			unsigned long color = channel == 0 ? color0 : color1;

			red[channel] = unpack565(color, 0);
			green[channel] = unpack565(color, 1);
			blue[channel] = unpack565(color, 2);
		}
		if (color0 > color1)
		{
			red[2] = (2 * red[0] + red[1]) / 3;
			green[2] = (2 * green[0] + green[1]) / 3;
			blue[2] = (2 * blue[0] + blue[1]) / 3;
			red[3] = (red[0] + 2 * red[1]) / 3;
			green[3] = (green[0] + 2 * green[1]) / 3;
			blue[3] = (blue[0] + 2 * blue[1]) / 3;
		}
		else
		{
			red[2] = (red[0] + red[1]) / 2;
			green[2] = (green[0] + green[1]) / 2;
			blue[2] = (blue[0] + blue[1]) / 2;
			red[3] = green[3] = blue[3] = 0;
		}
		for (texel = 0; texel < 16; texel++)
		{
			long index = (block[4 + texel / 4] >> (2 * (texel % 4))) & 3;
			long luma;

			/* (index 3 of a block already punched is clear) */
			if (color0 <= color1 && index == 3)
			{
				dark[texel] = 1;
				dark_count++;
				continue;
			}
			texel_red[texel] = (unsigned char)red[index];
			texel_green[texel] = (unsigned char)green[index];
			texel_blue[texel] = (unsigned char)blue[index];
			luma = red[index];
			if (green[index] > luma)
				luma = green[index];
			if (blue[index] > luma)
				luma = blue[index];
			if (luma < 96)
			{
				dark[texel] = 1;
				dark_count++;
				continue;
			}
			dark[texel] = 0;
			if (luma < dark_luma)
			{
				dark_luma = luma;
				darkest = texel;
			}
			if (luma > bright_luma)
			{
				bright_luma = luma;
				brightest = texel;
			}
		}
		if (!dark_count)
			continue;
		if (dark_count == 16)
		{
			memset(block, 0, 4);
			memset(block + 4, 0xFF, 4);
			continue;
		}
		{
			unsigned long packed0 = pack565(texel_red[darkest], texel_green[darkest], texel_blue[darkest]);
			unsigned long packed1 = pack565(texel_red[brightest], texel_green[brightest], texel_blue[brightest]);
			long palette[3][3];
			unsigned char rows[4];

			if (packed0 > packed1)
			{
				unsigned long swap = packed0;
				long swap_texel = darkest;

				packed0 = packed1;
				packed1 = swap;
				darkest = brightest;
				brightest = swap_texel;
			}
			for (channel = 0; channel < 3; channel++)
			{
				palette[0][channel] = unpack565(packed0, channel);
				palette[1][channel] = unpack565(packed1, channel);
				palette[2][channel] = (palette[0][channel] + palette[1][channel]) / 2;
			}
			memset(rows, 0, sizeof(rows));
			for (texel = 0; texel < 16; texel++)
			{
				long index = 3;
				long best = 255 * 255 * 3;
				long choice;

				if (!dark[texel])
				{
					for (choice = 0; choice < 3; choice++)
					{
						long difference = texel_red[texel] - palette[choice][0];
						long distance = difference * difference;

						difference = texel_green[texel] - palette[choice][1];
						distance += difference * difference;
						difference = texel_blue[texel] - palette[choice][2];
						distance += difference * difference;
						if (distance < best)
						{
							best = distance;
							index = choice;
						}
					}
				}
				rows[texel / 4] |= (unsigned char)(index << (2 * (texel % 4)));
			}
			block[0] = (unsigned char)packed0;
			block[1] = (unsigned char)(packed0 >> 8);
			block[2] = (unsigned char)packed1;
			block[3] = (unsigned char)(packed1 >> 8);
			block[4] = rows[0];
			block[5] = rows[1];
			block[6] = rows[2];
			block[7] = rows[3];
		}
	}
}

/* a card's alpha lifted for blending. The picture sits near 140 and the
card around it at 0. Cutting that on a line slices the planet's limb off;
clear stays clear, and the rest is lifted so the body is solid and the
limb fades */
static long card_alpha_lift(
	long alpha)
{
	if (alpha < 12)
		return 0;
	if (alpha >= 96)
		return 255;

	return (alpha - 12) * 255 / 84;
}

/* the DXT5 index of a lifted alpha. Alpha 0 is 255 and alpha 1 is 0, so
the six steps between them run down from solid to clear */
static long card_alpha_index(
	long lifted)
{
	static const long level[8] = { 255, 0, 218, 182, 145, 109, 72, 36 };
	long best = 1;
	long distance = 255;
	long index;

	for (index = 0; index < 8; index++)
	{
		long delta = lifted - level[index];

		if (delta < 0)
			delta = -delta;
		if (delta < distance)
		{
			distance = delta;
			best = index;
		}
	}

	return best;
}

static void texture_card_alpha(
	struct saber_texture *texture)
{
	long offset;

	if (!texture->pixels || texture->format == _bitmap_format_dxt1)
		return;
	for (offset = 0; offset + 16 <= texture->pixels_size; offset += 16)
	{
		unsigned char *block = texture->pixels + offset;
		long texel;

		if (texture->format == _bitmap_format_dxt3)
		{
			unsigned char alpha[8];

			memset(alpha, 0, sizeof(alpha));
			for (texel = 0; texel < 16; texel++)
			{
				long lifted = card_alpha_lift(block_texel_alpha(block, texture->format, texel));

				alpha[texel / 2] |= (unsigned char)(((lifted * 15 + 127) / 255) << (4 * (texel % 2)));
			}
			memcpy(block, alpha, 8);
		}
		else
		{
			unsigned long low = 0;
			unsigned long high = 0;

			for (texel = 0; texel < 16; texel++)
			{
				long index = card_alpha_index(card_alpha_lift(
					block_texel_alpha(block, texture->format, texel)));

				if (texel < 8)
					low |= (unsigned long)index << (3 * texel);
				else
					high |= (unsigned long)index << (3 * (texel - 8));
			}
			block[0] = 255;
			block[1] = 0;
			block[2] = (unsigned char)low;
			block[3] = (unsigned char)(low >> 8);
			block[4] = (unsigned char)(low >> 16);
			block[5] = (unsigned char)high;
			block[6] = (unsigned char)(high >> 8);
			block[7] = (unsigned char)(high >> 16);
		}
	}
}

/* the halo ring's colour sits a few shades off one blue. Spread those
shades (about the picture's middle colour) so the band's surface shows
instead of one fill. Four times is enough that the light on the band
separates from the blue, and not so much that the rim clips to white */
static long ring_spread(
	long color,
	long middle)
{
	long spread = middle + (color - middle) * 4;

	if (spread < 0)
		return 0;
	if (spread > 255)
		return 255;

	return spread;
}

void saber_texture_ring_contrast(
	struct saber_texture *texture)
{
	long block_size;
	long color_at;
	long block_count;
	long histogram[3][256];
	long middle[3];
	long channel;
	long offset;
	long count;

	if (!texture->pixels || texture->width < 4 || texture->height < 4)
		return;
	block_size = texture->format == _bitmap_format_dxt1 ? 8 : 16;
	color_at = block_size == 8 ? 0 : 8;
	block_count = ((long)texture->width / 4) * ((long)texture->height / 4);
	if (block_count <= 0 || block_count * block_size > texture->pixels_size)
		return;
	csmemset(histogram, 0, sizeof(histogram));
	for (offset = 0; offset < block_count * block_size; offset += block_size)
	{
		unsigned long color = (unsigned long)texture->pixels[offset + color_at] |
			((unsigned long)texture->pixels[offset + color_at + 1] << 8);

		histogram[0][unpack565(color, 0)]++;
		histogram[1][unpack565(color, 1)]++;
		histogram[2][unpack565(color, 2)]++;
	}
	for (channel = 0; channel < 3; channel++)
	{
		count = 0;
		middle[channel] = 0;
		for (offset = 0; offset < 256; offset++)
		{
			count += histogram[channel][offset];
			if (count * 2 >= block_count)
			{
				middle[channel] = offset;
				break;
			}
		}
	}
	for (offset = 0; offset + block_size <= texture->pixels_size; offset += block_size)
	{
		unsigned char *block = texture->pixels + offset + color_at;
		unsigned long color0 = (unsigned long)block[0] | ((unsigned long)block[1] << 8);
		unsigned long color1 = (unsigned long)block[2] | ((unsigned long)block[3] << 8);
		unsigned long packed0;
		unsigned long packed1;
		int order;

		packed0 = pack565(
			ring_spread(unpack565(color0, 0), middle[0]),
			ring_spread(unpack565(color0, 1), middle[1]),
			ring_spread(unpack565(color0, 2), middle[2]));
		packed1 = pack565(
			ring_spread(unpack565(color1, 0), middle[0]),
			ring_spread(unpack565(color1, 1), middle[1]),
			ring_spread(unpack565(color1, 2), middle[2]));
		order = color0 > color1;
		if ((packed0 > packed1) != order)
		{
			unsigned long swap = packed0;
			long index;

			packed0 = packed1;
			packed1 = swap;
			for (index = 4; index < 8; index++)
			{
				unsigned char bits = block[index];
				unsigned char remapped = 0;
				long texel;

				for (texel = 0; texel < 4; texel++)
				{
					long which = (bits >> (2 * texel)) & 3;

					which ^= 1;
					remapped |= (unsigned char)(which << (2 * texel));
				}
				block[index] = remapped;
			}
		}
		block[0] = (unsigned char)packed0;
		block[1] = (unsigned char)(packed0 >> 8);
		block[2] = (unsigned char)packed1;
		block[3] = (unsigned char)(packed1 >> 8);
	}
}

/* a texture's colour multiplied by numer/denom, its alpha left as it is */
void saber_texture_darken(
	struct saber_texture *texture,
	long numer,
	long denom)
{
	long block_size;
	long color_at;
	long offset;

	if (!texture->pixels || numer < 0 || denom <= 0)
		return;
	block_size = texture->format == _bitmap_format_dxt1 ? 8 : 16;
	color_at = block_size == 8 ? 0 : 8;
	for (offset = 0; offset + block_size <= texture->pixels_size; offset += block_size)
	{
		unsigned char *block = texture->pixels + offset + color_at;
		unsigned long color0 = (unsigned long)block[0] | ((unsigned long)block[1] << 8);
		unsigned long color1 = (unsigned long)block[2] | ((unsigned long)block[3] << 8);
		unsigned long packed0;
		unsigned long packed1;
		long channel0[3];
		long channel1[3];
		long channel;
		int order;

		for (channel = 0; channel < 3; channel++)
		{
			long scaled;

			channel0[channel] = unpack565(color0, channel);
			channel1[channel] = unpack565(color1, channel);
			scaled = channel0[channel] * numer / denom;
			channel0[channel] = scaled < 0 ? 0 : (scaled > 255 ? 255 : scaled);
			scaled = channel1[channel] * numer / denom;
			channel1[channel] = scaled < 0 ? 0 : (scaled > 255 ? 255 : scaled);
		}
		packed0 = pack565(channel0[0], channel0[1], channel0[2]);
		packed1 = pack565(channel1[0], channel1[1], channel1[2]);
		order = color0 > color1;
		if ((packed0 > packed1) != order)
		{
			unsigned long swap = packed0;
			long index;

			packed0 = packed1;
			packed1 = swap;
			for (index = 4; index < 8; index++)
			{
				unsigned char bits = block[index];
				unsigned char remapped = 0;
				long texel;

				for (texel = 0; texel < 4; texel++)
				{
					long which = (bits >> (2 * texel)) & 3;

					which ^= 1;
					remapped |= (unsigned char)(which << (2 * texel));
				}
				block[index] = remapped;
			}
		}
		block[0] = (unsigned char)packed0;
		block[1] = (unsigned char)(packed0 >> 8);
		block[2] = (unsigned char)packed1;
		block[3] = (unsigned char)(packed1 >> 8);
	}
}

/* the marine eyepiece: a dark mask. Luminance becomes green, the field
included, so the lens is green and the marks are brighter */
void saber_texture_monicle(
	struct saber_texture *texture)
{
	long block_size;
	long color_at;
	long offset;

	if (!texture->pixels)
		return;
	block_size = texture->format == _bitmap_format_dxt1 ? 8 : 16;
	color_at = block_size == 8 ? 0 : 8;
	for (offset = 0; offset + block_size <= texture->pixels_size; offset += block_size)
	{
		unsigned char *block = texture->pixels + offset + color_at;
		unsigned long color0 = (unsigned long)block[0] | ((unsigned long)block[1] << 8);
		unsigned long color1 = (unsigned long)block[2] | ((unsigned long)block[3] << 8);
		unsigned long packed0;
		unsigned long packed1;
		long channel0[3];
		long channel1[3];
		long which;
		int order;

		for (which = 0; which < 2; which++)
		{
			long *channel = which ? channel1 : channel0;
			unsigned long color = which ? color1 : color0;
			long lum;
			long glow;

			channel[0] = unpack565(color, 0);
			channel[1] = unpack565(color, 1);
			channel[2] = unpack565(color, 2);
			lum = (channel[0] * 30 + channel[1] * 59 + channel[2] * 11) / 100;
			if (lum < 0)
				lum = 0;
			if (lum > 255)
				lum = 255;
			glow = 90 + lum * 165 / 255;
			channel[0] = glow / 4;
			channel[1] = glow;
			channel[2] = glow / 5;
		}
		packed0 = pack565(channel0[0], channel0[1], channel0[2]);
		packed1 = pack565(channel1[0], channel1[1], channel1[2]);
		order = color0 > color1;
		if ((packed0 > packed1) != order)
		{
			unsigned long swap = packed0;
			long index;

			packed0 = packed1;
			packed1 = swap;
			for (index = 4; index < 8; index++)
			{
				unsigned char bits = block[index];
				unsigned char remapped = 0;
				long texel;

				for (texel = 0; texel < 4; texel++)
				{
					long bit = (bits >> (2 * texel)) & 3;

					bit ^= 1;
					remapped |= (unsigned char)(bit << (2 * texel));
				}
				block[index] = remapped;
			}
		}
		block[0] = (unsigned char)packed0;
		block[1] = (unsigned char)(packed0 >> 8);
		block[2] = (unsigned char)packed1;
		block[3] = (unsigned char)(packed1 >> 8);
	}
}

void saber_texture_punch_black(
	struct saber_texture *texture)
{
	texture_punch_black(texture);
}

void saber_texture_sky_alpha(
	struct saber_texture *texture,
	int stars)
{
	if (stars || texture->format == _bitmap_format_dxt1)
		texture_punch_black(texture);
	else
		texture_card_alpha(texture);
}

/* the rear meter's white plate. Luminance becomes blue; black stays black */
void saber_texture_holo(
	struct saber_texture *texture)
{
	long block_size;
	long color_at;
	long offset;

	if (!texture->pixels)
		return;
	block_size = texture->format == _bitmap_format_dxt1 ? 8 : 16;
	color_at = block_size == 8 ? 0 : 8;
	for (offset = 0; offset + block_size <= texture->pixels_size; offset += block_size)
	{
		unsigned char *block = texture->pixels + offset + color_at;
		unsigned long color0 = (unsigned long)block[0] | ((unsigned long)block[1] << 8);
		unsigned long color1 = (unsigned long)block[2] | ((unsigned long)block[3] << 8);
		unsigned long packed0;
		unsigned long packed1;
		long channel0[3];
		long channel1[3];
		long which;
		int order;

		for (which = 0; which < 2; which++)
		{
			long *channel = which ? channel1 : channel0;
			unsigned long color = which ? color1 : color0;
			long lum;

			channel[0] = unpack565(color, 0);
			channel[1] = unpack565(color, 1);
			channel[2] = unpack565(color, 2);
			lum = (channel[0] * 30 + channel[1] * 59 + channel[2] * 11) / 100;
			if (lum < 0)
				lum = 0;
			if (lum > 255)
				lum = 255;
			channel[0] = lum * 128 / 255;
			channel[1] = lum * 200 / 255;
			channel[2] = lum;
		}
		packed0 = pack565(channel0[0], channel0[1], channel0[2]);
		packed1 = pack565(channel1[0], channel1[1], channel1[2]);
		order = color0 > color1;
		if ((packed0 > packed1) != order)
		{
			unsigned long swap = packed0;
			long index;

			packed0 = packed1;
			packed1 = swap;
			for (index = 4; index < 8; index++)
			{
				unsigned char bits = block[index];
				unsigned char remapped = 0;
				long texel;

				for (texel = 0; texel < 4; texel++)
				{
					long bit = (bits >> (2 * texel)) & 3;

					bit ^= 1;
					remapped |= (unsigned char)(bit << (2 * texel));
				}
				block[index] = remapped;
			}
		}
		block[0] = (unsigned char)packed0;
		block[1] = (unsigned char)(packed0 >> 8);
		block[2] = (unsigned char)packed1;
		block[3] = (unsigned char)(packed1 >> 8);
	}
}

void saber_texture_make_solid(
	struct saber_texture *texture)
{
	long offset;

	if (!texture->pixels || texture->format == _bitmap_format_dxt1)
		return;
	/* (every block of every mipmap: each's alpha its first 8 bytes) */
	for (offset = 0; offset + 16 <= texture->pixels_size; offset += 16)
	{
		if (texture->format == _bitmap_format_dxt3)
		{
			memset(texture->pixels + offset, 0xFF, 8);
		}
		else
		{
			texture->pixels[offset] = 0xFF;
			texture->pixels[offset + 1] = 0xFF;
			memset(texture->pixels + offset + 2, 0, 6);
		}
	}
}

/* A window's alpha is how much of it covers what is behind it. The
Pelican's cockpit glass is plotted dark and near opaque; its alpha keeps a
fifth of itself, so the bay shows through it as the map's own glass does.
(The alpha is per texel: a DXT3 stores it whole, a DXT5's is its first two
bytes and the indices after them) */
void saber_texture_glass(
	struct saber_texture *texture)
{
	long offset;

	if (!texture->pixels)
		return;
	if (texture->format == _bitmap_format_dxt3)
	{
		for (offset = 0; offset + 16 <= texture->pixels_size; offset += 16)
		{
			long index;

			for (index = 0; index < 8; index++)
			{
				texture->pixels[offset + index] = (unsigned char)(texture->pixels[offset + index] / 5);
			}
		}
	}
	else if (texture->format == _bitmap_format_dxt5)
	{
		for (offset = 0; offset + 16 <= texture->pixels_size; offset += 16)
		{
			unsigned char *block = texture->pixels + offset;
			long alpha0 = block[0];
			long alpha1 = block[1];

			/* (each alpha its fifth: the two ends, and the interpolated ones
			are the same interpolations of them) */
			block[0] = (unsigned char)(alpha0 / 5);
			block[1] = (unsigned char)(alpha1 / 5);
		}
	}
}

void saber_texture_dispose(
	struct saber_texture *texture)
{
	if (texture->pixels)
		free(texture->pixels);
	csmemset(texture, 0, sizeof(*texture));
}
