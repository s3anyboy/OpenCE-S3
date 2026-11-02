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
XMemCompress stream (saber_lzx.c). The PC's (Halo: The Master Chief
Collection's) are the stream itself. As Reclaimer tells them apart: a
package whose fourth u32 is no name length (0 or less, or over 1024) is
compressed. Blocks are read as their entries are, and the last one read is
kept.

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
	A mesh named .lod1, .lod2 ... is the template's at a lower level of
	detail.

Saber3D's axes are forward x, right -z and up y, Halo's forward x, left y
and up z: a point of the template is Halo's with its y and z exchanged,
which turns its triangles over; they are turned to face as Halo's do
(clockwise about the normals the template gives). A template's own units vary (Halo's are not
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

/* a template's blocks */
enum
{
	_block_template = 0xE402,
	_block_materials = 0x5501,
	_block_material = 0x5601,
	_block_node = 0xF000,
	_block_mesh = 0xB903,
	_block_transform = 0xF900,
	_block_parent = 0x2B01,
	_block_mesh_flags = 0x2E01,
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

#define MAXIMUM_NODES 4096
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
};

struct template_walk
{
	unsigned char const *bytes;
	unsigned long size;
	struct template_node *nodes;
	long node_count;
	boolean failed;
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
	unsigned long compressed_size = pak->block_offsets[block_index + 1] - pak->block_offsets[block_index];
	unsigned long size = pak->block_starts[block_index + 1] - pak->block_starts[block_index];
	long written;

	if (pak->cached_block == block_index)
		return pak->block;
	pak->cached_block = NONE;
	if (compressed_size > MAXIMUM_COMPRESSED_BLOCK_SIZE ||
		!file_read(pak, pak->block_offsets[block_index], pak->compressed_block, compressed_size))
	{
		return NULL;
	}
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

/* whether a node, or a node it is under, is a rank's variant that is not
the least's (an Elite's or a Grunt's "_major", "officer" ...: Halo draws its
ranks as one model of their colours, so the minor's mesh alone is drawn) */
static boolean node_rank_variant(
	struct template_walk const *walk,
	struct template_node const *node)
{
	static char const *const ranks[] = { "major", "officer", "special", "spec_ops", "specops", "zealot", "stealth",
		"ultra", "commander", "honor" };
	long depth;

	for (depth = 0; node && depth < MAXIMUM_NODE_DEPTH; depth++)
	{
		char name[64];
		unsigned long length = MIN(node->name_length, sizeof(name) - 1);
		long rank;

		memcpy(name, walk->bytes + node->name_start, length);
		name[length] = 0;
		for (rank = 0; rank < (long)NUMBEROF(ranks); rank++)
		{
			if (strstr(name, ranks[rank]))
				return TRUE;
		}
		node = node->parent_id == NONE ? NULL : node_of_id(walk, node->parent_id);
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
			origins[template_->bone_count][1] = world[14];
			origins[template_->bone_count][2] = world[13];
		}
		strcpy(bones[template_->bone_count], name);
		parents[template_->bone_count] = parent;
		/* (Halo's axes to Saber3D's, y and z exchanged: its rows 1 and 2) */
		for (index = 0; index < 4; index++)
		{
			binds[template_->bone_count][index] = inverse[index];
			binds[template_->bone_count][4 + index] = inverse[8 + index];
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
size (Reclaimer's), or -1 */
static long texcoord_offset(
	long size)
{
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
	unsigned long flags = node->mesh_flags;
	long vertex_count;
	long triangle_count;
	long vertex_size = 0;
	long uv_offset = -1;
	unsigned long data;
	struct model_vertex_uncompressed *vertices;
	unsigned short *triangles;
	real_vector3d *tangents;
	long *remap;
	real transform[16];
	real normals[9];
	real_point3d translation = { 0.f, 0.f, 0.f };
	real_point3d scale = { 1.f, 1.f, 1.f };
	long agree = 0;
	boolean flip;
	long submesh_first[MAXIMUM_SUBMESHES];
	long submesh_count[MAXIMUM_SUBMESHES];
	short submesh_material[MAXIMUM_SUBMESHES];
	long submesh_total = 0;
	long rigid_slot;
	unsigned char const *blend_indices = NULL;
	unsigned char const *blend_weights = NULL;
	long blend_count = 0;
	long blend_slots[256];
	long index;

	*reason = "a mesh is not as a template's is";
	if (node->positions.end - node->positions.start < 4 || node->triangles.end - node->triangles.start < 4)
		return TRUE;
	vertex_count = (long)read_u32(bytes + node->positions.start);
	triangle_count = (long)read_u32(bytes + node->triangles.start);
	if (vertex_count <= 0 || triangle_count <= 0 || node_rank_variant(walk, node))
		return TRUE;
	if (vertex_count > 0x100000 || triangle_count > 0x100000 ||
		(unsigned long)triangle_count * 6 > node->triangles.end - node->triangles.start - 4)
	{
		return FALSE;
	}
	data = node->positions.start + 4;
	if (TEST_FLAG(flags, _mesh_compressed_bit))
	{
		if (node->positions.end - data < 12)
			return FALSE;
		translation.x = (real)read_short(bytes + data);
		translation.y = (real)read_short(bytes + data + 2);
		translation.z = (real)read_short(bytes + data + 4);
		scale.x = (real)read_short(bytes + data + 6);
		scale.y = (real)read_short(bytes + data + 8);
		scale.z = (real)read_short(bytes + data + 10);
		data += 12;
		if ((unsigned long)vertex_count * 8 > node->positions.end - data)
			return FALSE;
	}
	else if ((unsigned long)vertex_count * 12 > node->positions.end - data)
	{
		return FALSE;
	}
	if (node->vertex_data.end - node->vertex_data.start >= 13 &&
		(long)read_u32(bytes + node->vertex_data.start) == vertex_count)
	{
		vertex_size = bytes[node->vertex_data.start + 12];
		uv_offset = texcoord_offset(vertex_size);
		if (uv_offset < 0 ||
			(unsigned long)vertex_count * (unsigned long)vertex_size > node->vertex_data.end - node->vertex_data.start - 13)
		{
			uv_offset = -1;
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

	*reason = "out of memory";
	vertices = malloc(sizeof(*vertices) * (size_t)vertex_count);
	triangles = malloc(sizeof(*triangles) * 3 * (size_t)triangle_count);
	tangents = malloc(sizeof(*tangents) * (size_t)vertex_count);
	remap = malloc(sizeof(*remap) * (size_t)vertex_count);
	if (!vertices || !triangles || !tangents || !remap)
	{
		if (vertices)
			free(vertices);
		if (triangles)
			free(triangles);
		if (tangents)
			free(tangents);
		if (remap)
			free(remap);
		return FALSE;
	}

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

		if (TEST_FLAG(flags, _mesh_compressed_bit))
		{
			unsigned char const *packed = bytes + data + 8 * index;

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
		world.x = point.x * transform[0] + point.y * transform[4] + point.z * transform[8] + transform[12];
		world.y = point.x * transform[1] + point.y * transform[5] + point.z * transform[9] + transform[13];
		world.z = point.x * transform[2] + point.y * transform[6] + point.z * transform[10] + transform[14];
		world_normal.i = normal.i * normals[0] + normal.j * normals[3] + normal.k * normals[6];
		world_normal.j = normal.i * normals[1] + normal.j * normals[4] + normal.k * normals[7];
		world_normal.k = normal.i * normals[2] + normal.j * normals[5] + normal.k * normals[8];
		csmemset(vertex, 0, sizeof(*vertex));
		vertex->position.x = world.x;
		vertex->position.y = world.z;
		vertex->position.z = world.y;
		vertex->normal.i = world_normal.i;
		vertex->normal.j = world_normal.k;
		vertex->normal.k = world_normal.j;
		normalize3d(&vertex->normal);
		if (!real_finite(world.x) || !real_finite(world.y) || !real_finite(world.z))
		{
			*reason = "a mesh's vertices are not numbers";
			free(vertices);
			free(triangles);
			free(tangents);
			free(remap);
			return FALSE;
		}
		if (uv_offset >= 0)
		{
			unsigned char const *uv = bytes + node->vertex_data.start + 13 + vertex_size * index + uv_offset;

			vertex->texcoord.x = unit_short(xbox360 ? read_short_big(uv) : read_short(uv));
			vertex->texcoord.y = unit_short(xbox360 ? read_short_big(uv + 2) : read_short(uv + 2));
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

	/* its triangles, turned as Halo's front faces are: clockwise about the
	normals the template gives */
	*reason = "a mesh's triangles name vertices it has not got";
	for (index = 0; index < 3 * triangle_count; index++)
	{
		triangles[index] = (unsigned short)read_short(bytes + node->triangles.start + 4 + 2 * index);
		if (triangles[index] >= vertex_count)
		{
			free(vertices);
			free(triangles);
			free(tangents);
			free(remap);
			return FALSE;
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
			part->vertices = malloc(sizeof(*part->vertices) * 3 * (size_t)part_triangles);
			part->triangles = malloc(sizeof(*part->triangles) * 3 * (size_t)part_triangles);
			template_->part_count++;
			if (!part->vertices || !part->triangles)
				goto failed;
			for (triangle = 0; triangle < vertex_count; triangle++)
				remap[triangle] = NONE;
			for (triangle = 0; triangle < 3 * part_triangles; triangle++)
			{
				long vertex = triangles[3 * first + triangle];

				if (remap[vertex] == NONE)
				{
					remap[vertex] = part->vertex_count;
					part->vertices[part->vertex_count++] = vertices[vertex];
				}
				part->triangles[triangle] = (unsigned short)remap[vertex];
			}
			part->triangle_count = part_triangles;
			first += part_triangles;
			count -= part_triangles;
		}
	}
	free(vertices);
	free(triangles);
	free(tangents);
	free(remap);

	return TRUE;

failed:
	free(vertices);
	free(triangles);
	free(tangents);
	free(remap);

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
	/* (a compressed package's fourth word is a block's offset, or 0) */
	pak->compressed = (long)read_u32(words + 12) <= 0 || read_u32(words + 12) > MAXIMUM_NAME_LENGTH;
	if (pak->compressed)
	{
		pak->block_count = (long)read_u32(words);
		if (!blocks_open(pak, reason))
			goto refused;
	}
	else
	{
		pak->stream_size = pak->file_size;
	}
	if (!directory_read(pak, reason))
		goto refused;

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
	return pak->compressed;
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

int saber_template_read(
	struct saber_template *template_,
	unsigned char const *bytes,
	unsigned long size,
	int xbox360,
	char const **reason)
{
	struct template_walk walk;
	struct block root;
	struct block materials;
	long index;

	csmemset(template_, 0, sizeof(*template_));
	*reason = "it is not a template";
	if (!block_at(bytes, 0, size, &root) || root.type != _block_template)
		return FALSE;

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
	for (index = 0; index < walk.node_count; index++)
	{
		if (!mesh_read(template_, &walk, &walk.nodes[index], xbox360 ? TRUE : FALSE, reason))
		{
			free(walk.nodes);
			return FALSE;
		}
	}
	free(walk.nodes);
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

void saber_texture_dispose(
	struct saber_texture *texture)
{
	if (texture->pixels)
		free(texture->pixels);
	csmemset(texture, 0, sizeof(*texture));
}
