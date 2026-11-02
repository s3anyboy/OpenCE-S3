/*
SABER_MODELS.C

Halo: Combat Evolved Anniversary's remastered models drawn over the map's
(display.anniversary_models): as a map loads, its Saber3D package
(<display.anniversary_paks>\<map>.s3dpak, by default paks\ next to the
game's executable: the Xbox 360's paks360 or Halo: The Master Chief
Collection's) is read for a template of each of the map's models (its
objects': the structure is no model, and is left as it is), and each that
has one is drawn as it, textured with the package's textures.

A model's template is found by the model's name (template_find): its last
folder, "weapons\pistol\pistol" pistol, "weapons\pistol\fp\fp" pistol_fp,
"weapons\needler\projectile\projectile" needler_projectile, as it is or
with "__h", the map's name ("light_fixture_floor1_b30__h") or another
"__" kind ("tree_leafy__tree"), the first that has a mesh. Its effects'
parts (muzzle flashes, lights' cones, overshields, counters' digits) are
left out, and of a character's ranks the least's meshes alone are drawn
(saber_paks.c). Its lower levels of detail (its .lod1, .lod2 meshes) are
the model's geometries of Halo's lower ones.

A template is drawn as a Custom Edition map's model is (mod_models.c does
the same for the mods' models): its meshes made the vertices of the
map's model's parts (saber_paks.c reads them, in Halo's axes), compressed
by the game's own rasterizer_geometry_compress_vertices into the Xbox's
contiguous memory and given buffers by rasterizer_vertex_buffer_new and
rasterizer_triangle_buffer_new. Each vertex moves with the map's model's
nodes of the names of the template's bones it is blended of (its two most
weighted; a bone with no such node, the model's first), so the map's
animations move it. A model of as many nodes as the renderer skins a part
with or more is left as it is.

A template's units are not Halo's, nor all the same: the scenery's are
metres, the weapons' and characters' tenths of a foot. Each is scaled to
the map's model: the ratio of the two's sizes, across, taken to the
nearest of the units templates are seen in.

What is drawn changes, nothing else: the map's model keeps its nodes,
markers and regions (what the machines of a network game agree on). Its
first region's permutations draw the template, its others nothing. Each of
the template's materials is drawn with one of the map's model's shaders
that can be (model shaders with a base map: the first material the first,
the next the next ...), whose base map becomes the package's texture of
the material (its full texture, from the level's package or
pak_stream.s3dpak beside it, else its 64-pixel TexturesMips64), drawn
without alpha testing. The shader loses its multipurpose and reflection
maps, which are the classic model's. A shader or bitmap another model has
too is drawn so for it as well. The map's model, shader and bitmap tags
take the made definitions in place, and their own come back as the map
goes.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_model_types.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "shaders/shader_definitions.h"
#include "shaders/shader_texture_animation.h"
#include "tag_files/tag_groups.h"
#include "cache_file_formats.h"
#include "saber.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtl.h>

/* the platform layer's (port_config.c, xbox_files.c, xbox_textures.c) */
int config_boolean(const char *name);
const char *config_string(const char *name);
void config_executable_folder(char *path, size_t size);
void platform_log(char const *format, ...);
void halo_custom_edition_texels_channels(void const *texels, unsigned char channel_order);

/* the game's (port) */
char *tag_get_name(long tag_index);

/* ---------- constants */

/* the package's file, in the folder of display.anniversary_paks (by
default paks, next to the executable) */
#define DEFAULT_PAKS_FOLDER "paks"
#define PAK_EXTENSION ".s3dpak"
/* the package of the full textures, beside the levels' */
#define STREAM_PAK_NAME "pak_stream.s3dpak"

/* the most a texture's pixels may take (mod_bitmaps.c's: a quarter of the
texture cache, which draws it among others) */
#define MAXIMUM_TEXTURE_SIZE ((long)(HALO_PORT_TEXTURE_CACHE_SIZE / 4))

/* the most textures one bitmap of the map holds (as permutations), and the
most materials a template draws */
#define MAXIMUM_SABER_BITMAP_TEXTURES 32
#define MAXIMUM_SABER_MATERIALS 64

/* models.c's shader types and model flags */
#define SHADER_TYPE_MODEL 4
#define MODEL_PARTS_HAVE_LOCAL_NODES_BIT 1
/* rasterizer_xbox_models.c's model shader flags */
#define SHADER_MODEL_TWO_SIDED_BIT 1
#define SHADER_MODEL_NOT_ALPHA_TESTED_BIT 2
#define SHADER_MODEL_ALPHA_BLENDED_DECAL_BIT 3

/* bitmaps.c's types and flags */
enum
{
	_bitmap_type_2d = 0,
};

enum
{
	_bitmap_has_power_of_two_dimensions_bit,
	_bitmap_compressed_bit,
	_bitmap_palettized_bit,
	_bitmap_swizzled_bit,
	_bitmap_linear_bit,
	_bitmap_v16u16_bit,
	_bitmap_allocated_bit,
	_bitmap_cached_bit,
};

/* the units templates are seen in, in Halo's (world units, of 10 feet):
metres, tenths of a foot, feet, Halo's own */
static real const saber_units[] = { 1.f / 3.048f, 0.01f, 0.1f, 1.f };

/* ---------- structures */

/* this build's model geometry, part and shader reference (models.c's; no
header declares them) */
struct model_geometry
{
	byte reserved[0x24];
	struct tag_block parts;
};

struct model_geometry_part
{
	unsigned long flags;
	short shader_index;
	char previous_part_index;
	char next_part_index;
	short centroid_primary_node_index;
	short centroid_secondary_node_index;
	real centroid_primary_node_weight;
	real centroid_secondary_node_weight;
	real_point3d centroid;
	struct tag_block uncompressed_vertices;
	struct tag_block compressed_vertices;
	struct tag_block triangles;
	struct triangle_buffer triangle_buffer;
	struct vertex_buffer vertex_buffer;
};

struct model_shader_reference
{
	struct tag_reference shader;
	short permutation_index;
	word pad;
	long unused[3];
};

/* a model shader (rasterizer_xbox_models.c's) */
struct shader_model_properties
{
	word flags;
	short type;
	byte reserved04[0xC];
	real translucency;
	byte reserved14[0x10];
	short change_color_source;
	byte reserved26[0x1E];
	word self_illumination_flags;
	short pad46;
	short self_illumination_color_source;
	short self_illumination_animation_function;
	real self_illumination_animation_period;
	real_rgb_color self_illumination_animation_color_lower_bound;
	real_rgb_color self_illumination_animation_color_upper_bound;
	byte reserved68[0xC];
	real map_u_scale;
	real map_v_scale;
	struct tag_reference base_map;
	byte reserved8C[8];
	struct tag_reference multipurpose_map;
	byte reservedA4[8];
	short detail_function;
	short detail_mask;
	real detail_map_scale;
	struct tag_reference detail_map;
	real detail_map_v_scale;
	byte reservedC8[0xC];
	struct shader_texture_animation texture_animation;
	byte reserved10C[8];
	real reflection_falloff_distance;
	real reflection_cutoff_distance;
	real perpendicular_brightness;
	real_rgb_color perpendicular_tint_color;
	real parallel_brightness;
	real_rgb_color parallel_tint_color;
	struct tag_reference reflection_cube_map;
};

struct shader_model_definition
{
	struct shader shader;
	struct shader_model_properties model;
};

typedef char verify_model_geometry_size[sizeof(struct model_geometry) == 0x30 ? 1 : -1];
typedef char verify_model_geometry_part_size[sizeof(struct model_geometry_part) == 0x68 ? 1 : -1];
typedef char verify_model_shader_reference_size[sizeof(struct model_shader_reference) == 0x20 ? 1 : -1];
typedef char verify_shader_model_properties_size[sizeof(struct shader_model_properties) == 0x14C ? 1 : -1];

/* a model made of a template, played over the map's model tag */
struct saber_model
{
	/* its shaders: the map's model's, then one for each of the template's
	materials (a shader of the map's model, the permutation its texture) */
	struct model_shader_reference *shaders;
	struct saber_model *next;
	long tag_index;
	struct model map_model;
	struct model model;
	struct model_region *regions;
	struct model_region_permutation *permutations;
	/* a geometry of each of the template's levels of detail */
	struct model_geometry geometries[5];
	long geometry_count;
	struct model_geometry_part *parts;
	long part_count;
	byte *vertices;
	unsigned long vertices_size;
	boolean vertices_contiguous;
};

/* a shader of the map drawn with a texture of the package: its own
definition, given back as the map goes */
struct saber_shader
{
	struct saber_shader *next;
	long tag_index;
	struct shader_model_definition map_shader;
};

/* a bitmap of the map that is textures of the package: each a bitmap of
it, which a shader reference's permutation chooses (the renderer draws a
model shader's base map's bitmap of the permutation), so that one shader
draws each material of a template that has more than its model's shaders */
struct saber_bitmap
{
	struct saber_bitmap *next;
	long tag_index;
	struct bitmap_group map_group;
	long count;
	struct bitmap_data bitmaps[MAXIMUM_SABER_BITMAP_TEXTURES];
	struct saber_texture textures[MAXIMUM_SABER_BITMAP_TEXTURES];
	char names[MAXIMUM_SABER_BITMAP_TEXTURES][64];
	/* whether each is cut out by its alpha (saber_texture_cutout) */
	boolean cutouts[MAXIMUM_SABER_BITMAP_TEXTURES];
};

/* ---------- globals */

static struct
{
	struct saber_model *models;
	struct saber_shader *shaders;
	struct saber_bitmap *bitmaps;
	/* the folder of the level's package (with its separator), and the
	stream's package there, opened as a texture is first looked for in it */
	char folder[1024];
	struct saber_pak *stream;
	boolean stream_tried;
} saber_models_globals;

/* ---------- private code */

/* the package of the map `map_name` in the folder `folder` (an absolute
path, or one in the executable's folder) */
static void pak_path(
	char const *folder,
	char const *map_name,
	char *path,
	size_t size)
{
	char const *name = strrchr(map_name, '\\');
	char executable_folder[512];

	name = name ? name + 1 : map_name;
	if (!folder || !*folder)
		folder = DEFAULT_PAKS_FOLDER;
	if (folder[0] == '/' || folder[0] == '\\' || (folder[0] && folder[1] == ':'))
	{
		snprintf(path, size, "%s/%s%s", folder, name, PAK_EXTENSION);
	}
	else
	{
		config_executable_folder(executable_folder, sizeof(executable_folder));
		snprintf(path, size, "%s%s/%s%s", executable_folder, folder, name, PAK_EXTENSION);
	}
}

/* The span across (x and y) of the map's model's vertices, as the game
draws them (as object_mesh.c reads them); FALSE when it has none to read. */
static boolean map_model_span(
	struct model const *model,
	real *span)
{
	real_point3d low = { 0.f, 0.f, 0.f };
	real_point3d high = { 0.f, 0.f, 0.f };
	boolean found = FALSE;
	long geometry_index;

	for (geometry_index = 0; geometry_index < model->geometries.count; geometry_index++)
	{
		struct model_geometry const *geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index,
			struct model_geometry);
		long part_index;

		for (part_index = 0; part_index < geometry->parts.count; part_index++)
		{
			struct model_geometry_part const *part = TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index,
				struct model_geometry_part);
			struct vertex_buffer const *vertex_buffer = &part->vertex_buffer;
			byte *vertices = NULL;
			long stride;
			long index;

			if (!vertex_buffer->hardware_format || vertex_buffer->count <= 0)
				continue;
			if (vertex_buffer->type == _rasterizer_vertex_type_model_compressed)
				stride = sizeof(struct model_vertex_compressed);
			else if (vertex_buffer->type == _rasterizer_vertex_type_model_uncompressed)
				stride = sizeof(struct model_vertex_uncompressed);
			else
				continue;
			IDirect3DVertexBuffer8_Lock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format, 0, 0, &vertices,
				D3DLOCK_READONLY);
			if (!vertices)
				continue;
			for (index = 0; index < vertex_buffer->count; index++)
			{
				/* (both kinds of vertex start with their position) */
				real_point3d const *position = (real_point3d const *)(vertices + index * stride);

				if (!found)
				{
					low = high = *position;
					found = TRUE;
				}
				low.x = MIN(low.x, position->x);
				low.y = MIN(low.y, position->y);
				high.x = MAX(high.x, position->x);
				high.y = MAX(high.y, position->y);
			}
			IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format);
		}
	}
	*span = (high.x - low.x) + (high.y - low.y);

	/* (a model of a size no model has is not one read) */
	return found && *span > 0.001f && *span < 10000.f;
}

/* the scale of the template's units to Halo's: of saber_units, the nearest
to the ratio of the map's model's size to the template's (with no size of
the map's to go by, metres for a scenery template, "__h", else tenths of a
foot) */
static real template_scale(
	struct saber_template const *template_,
	char const *template_name,
	struct model const *map_model)
{
	real_point3d low = { 0.f, 0.f, 0.f };
	real_point3d high = { 0.f, 0.f, 0.f };
	boolean found = FALSE;
	real map_span;
	real ratio;
	real best;
	long part_index;
	long index;

	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part const *part = &template_->parts[part_index];

		for (index = 0; index < part->vertex_count; index++)
		{
			real_point3d const *position = &part->vertices[index].position;

			if (!found)
			{
				low = high = *position;
				found = TRUE;
			}
			low.x = MIN(low.x, position->x);
			low.y = MIN(low.y, position->y);
			high.x = MAX(high.x, position->x);
			high.y = MAX(high.y, position->y);
		}
	}
	/* (a template of a "__" kind, the scenery's, the vehicles', the
	powerups', is in metres; the others, the characters' and weapons', in
	tenths of a foot. Its size is no guide: a template may carry what is not
	the model's, as c_uplink__h carries crates 69 metres off) */
	best = strstr(template_name, "__") ? saber_units[0] : saber_units[1];
	if (found && (high.x - low.x) + (high.y - low.y) > 0.f && map_model_span(map_model, &map_span))
	{
		ratio = map_span / ((high.x - low.x) + (high.y - low.y));
		platform_log("anniversary: %s's units are %f world units (its size is %f of the map's model's)",
			template_name, best, 1.f / ratio);
	}

	return best;
}

/* the strip of a part's triangles: each after the last, joined by
triangles of no area, starting on an even index so it keeps its turn */
static long part_strip(
	struct saber_mesh_part const *part,
	word *strip)
{
	long count = 0;
	long index;

	for (index = 0; index < part->triangle_count; index++)
	{
		word a = part->triangles[3 * index];

		if (count)
		{
			strip[count] = strip[count - 1];
			count++;
			strip[count++] = a;
			if (count & 1)
				strip[count++] = a;
		}
		strip[count++] = a;
		strip[count++] = part->triangles[3 * index + 1];
		strip[count++] = part->triangles[3 * index + 2];
	}

	return count;
}

/* the memory the renderer draws a model's vertices and strips from: the
Xbox's contiguous memory, as a map's (else the heap) */
static void *geometry_allocate(
	unsigned long size,
	boolean *contiguous)
{
	void *geometry = XPhysicalAlloc(size, (unsigned long)-1, 0, PAGE_READWRITE);

	*contiguous = geometry != NULL;
	if (!geometry)
		geometry = malloc(size);

	return geometry;
}

static void saber_model_free(
	struct saber_model *made)
{
	long index;

	for (index = 0; made->parts && index < made->part_count; index++)
	{
		rasterizer_triangle_buffer_delete(&made->parts[index].triangle_buffer);
		rasterizer_vertex_buffer_delete(&made->parts[index].vertex_buffer);
	}
	if (made->vertices)
	{
		if (made->vertices_contiguous)
			XPhysicalFree(made->vertices);
		else
			free(made->vertices);
	}
	if (made->shaders)
		free(made->shaders);
	if (made->regions)
		free(made->regions);
	if (made->permutations)
		free(made->permutations);
	if (made->parts)
		free(made->parts);
	free(made);
}

/* a part's centroid's node: the node most of its vertices' weight is on */
static void part_centroid_node(
	struct model_geometry_part *part,
	struct saber_mesh_part const *source)
{
	real weights[MAXIMUM_NODES_PER_MODEL];
	long index;
	long best = 0;

	csmemset(weights, 0, sizeof(weights));
	for (index = 0; index < source->vertex_count; index++)
	{
		struct model_vertex_uncompressed const *vertex = &source->vertices[index];

		if (vertex->nodes[0] >= 0 && vertex->nodes[0] < MAXIMUM_NODES_PER_MODEL)
			weights[vertex->nodes[0]] += vertex->node_weights[0];
		if (vertex->nodes[1] >= 0 && vertex->nodes[1] < MAXIMUM_NODES_PER_MODEL)
			weights[vertex->nodes[1]] += vertex->node_weights[1];
	}
	for (index = 1; index < MAXIMUM_NODES_PER_MODEL; index++)
	{
		if (weights[index] > weights[best])
			best = index;
	}
	part->centroid_primary_node_index = (short)best;
	part->centroid_secondary_node_index = (short)best;
}

/* The model the template makes of the map's model: the map's, its every
permutation drawing the template's geometry (`shader_indices`, the shader
each part is drawn with). NULL, with *reason saying why, when it cannot be
made. */
static struct saber_model *saber_model_new(
	long tag_index,
	struct model const *map_model,
	struct saber_template const *template_,
	short const *shader_indices,
	real scale,
	char const **reason)
{
	long compressed_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_model_compressed);
	long uncompressed_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_model_uncompressed);
	struct saber_model *made = malloc(sizeof(*made));
	unsigned long size = 0;
	long permutation_count = 0;
	long region_index;
	long part_index;
	long next = 0;
	struct model_vertex_compressed *vertices;
	word *strips;
	/* the template's levels of detail there are parts of, and its parts in
	the order of their levels (each level's parts a geometry's) */
	short levels[5];
	long level_count = 0;
	long order[MAXIMUM_PARTS_PER_MODEL_GEOMETRY];
	long first[5];
	long level;
	long made_index;

	*reason = "out of memory";
	if (!made)
		return NULL;
	csmemset(made, 0, sizeof(*made));
	for (level = 0; level < 5; level++)
	{
		for (part_index = 0; part_index < template_->part_count; part_index++)
		{
			if (MIN(template_->parts[part_index].lod, 4) == level)
				break;
		}
		if (part_index < template_->part_count)
			levels[level_count++] = (short)level;
	}
	for (level = 0, made_index = 0; level < level_count; level++)
	{
		first[level] = made_index;
		for (part_index = 0; part_index < template_->part_count; part_index++)
		{
			if (MIN(template_->parts[part_index].lod, 4) == levels[level])
				order[made_index++] = part_index;
		}
	}
	made->tag_index = tag_index;
	made->map_model = *map_model;
	made->model = *map_model;
	made->model.flags &= ~FLAG(MODEL_PARTS_HAVE_LOCAL_NODES_BIT);
	made->model.base_map_scale.i = 1.f;
	made->model.base_map_scale.j = 1.f;

	/* the map's regions and permutations, every one of them the template */
	for (region_index = 0; region_index < map_model->regions.count; region_index++)
	{
		permutation_count += TAG_BLOCK_GET_ELEMENT(&map_model->regions, region_index, struct model_region)->permutations.count;
	}
	made->regions = malloc(sizeof(*made->regions) * ((size_t)map_model->regions.count + 1));
	made->permutations = malloc(sizeof(*made->permutations) * ((size_t)permutation_count + 1));
	made->parts = malloc(sizeof(*made->parts) * ((size_t)template_->part_count + 1));
	if (!made->regions || !made->permutations || !made->parts)
		goto refused;
	csmemset(made->parts, 0, sizeof(*made->parts) * ((size_t)template_->part_count + 1));
	for (region_index = 0; region_index < map_model->regions.count; region_index++)
	{
		struct model_region const *map_region = TAG_BLOCK_GET_ELEMENT(&map_model->regions, region_index,
			struct model_region);
		long permutation_index;

		made->regions[region_index] = *map_region;
		made->regions[region_index].permutations.address = made->permutations + next;
		made->regions[region_index].permutations.definition = NULL;
		for (permutation_index = 0; permutation_index < map_region->permutations.count; permutation_index++)
		{
			struct model_region_permutation *permutation = &made->permutations[next++];
			long detail;

			*permutation = *TAG_BLOCK_GET_ELEMENT(&map_region->permutations, permutation_index,
				struct model_region_permutation);
			/* (super high the template's most detailed, each lower Halo level
			its next, and the rest its least; the template whole, drawn by
			the first region alone) */
			for (detail = 0; detail < 5; detail++)
			{
				permutation->geometry_indices[detail] = region_index ? NONE :
					(short)MIN(level_count - 1, 4 - detail);
			}
		}
	}
	made->model.regions.address = made->regions;
	made->model.regions.definition = NULL;

	/* its geometry: a part of each of the template's */
	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part const *part = &template_->parts[part_index];

		size += (unsigned long)part->vertex_count * (unsigned long)compressed_size +
			(unsigned long)part->triangle_count * 6 * sizeof(word);
	}
	made->vertices_size = size + 1;
	made->vertices = geometry_allocate(made->vertices_size, &made->vertices_contiguous);
	if (!made->vertices)
		goto refused;
	vertices = (struct model_vertex_compressed *)made->vertices;
	for (part_index = 0; part_index < template_->part_count; part_index++)
		vertices += template_->parts[part_index].vertex_count;
	strips = (word *)vertices;
	vertices = (struct model_vertex_compressed *)made->vertices;
	for (made_index = 0; made_index < template_->part_count; made_index++)
	{
		struct saber_mesh_part const *source = &template_->parts[order[made_index]];
		struct model_geometry_part *part = &made->parts[made_index];
		struct model_vertex_uncompressed *scaled = malloc(sizeof(*scaled) * ((size_t)source->vertex_count + 1));
		long strip_count;
		long index;

		*reason = "out of memory";
		if (!scaled)
			goto refused;
		part->shader_index = shader_indices[order[made_index]];
		part->previous_part_index = NONE;
		part->next_part_index = NONE;
		part->centroid_primary_node_weight = 1.f;
		part_centroid_node(part, source);
		for (index = 0; index < source->vertex_count; index++)
		{
			scaled[index] = source->vertices[index];
			scaled[index].position.x *= scale;
			scaled[index].position.y *= scale;
			scaled[index].position.z *= scale;
			part->centroid.x += scaled[index].position.x / (real)source->vertex_count;
			part->centroid.y += scaled[index].position.y / (real)source->vertex_count;
			part->centroid.z += scaled[index].position.z / (real)source->vertex_count;
		}
		rasterizer_geometry_compress_vertices(
			_rasterizer_vertex_type_model_uncompressed,
			source->vertex_count,
			vertices,
			source->vertex_count * compressed_size,
			scaled,
			source->vertex_count * uncompressed_size);
		free(scaled);
		strip_count = part_strip(source, strips);
		made->part_count = made_index + 1;
		*reason = "the renderer cannot make its buffers";
		if (!rasterizer_vertex_buffer_new(&part->vertex_buffer, _rasterizer_vertex_type_model_compressed,
				source->vertex_count, vertices, source->vertex_count * compressed_size) ||
			!rasterizer_triangle_buffer_new(&part->triangle_buffer, _triangle_buffer_type_precompiled_strip,
				strip_count - 2, strips))
		{
			goto refused;
		}
		vertices += source->vertex_count;
		strips += strip_count;
	}
	for (level = 0; level < level_count; level++)
	{
		long end = level + 1 < level_count ? first[level + 1] : made->part_count;

		made->geometries[level].parts.count = end - first[level];
		made->geometries[level].parts.address = made->parts + first[level];
		made->geometries[level].parts.definition = NULL;
	}
	made->geometry_count = level_count;
	made->model.geometries.count = level_count;
	made->model.geometries.address = made->geometries;
	made->model.geometries.definition = NULL;

	return made;

refused:
	saber_model_free(made);

	return NULL;
}

/* the model shader of the map's model's shader `shader_index`, or NONE */
static long model_shader_tag(
	struct model const *model,
	short shader_index)
{
	struct model_shader_reference const *reference;
	struct shader *shader;

	if (shader_index < 0 || shader_index >= model->shaders.count)
		return NONE;
	reference = TAG_BLOCK_GET_ELEMENT(&model->shaders, shader_index, struct model_shader_reference);
	if (reference->shader.index == NONE)
		return NONE;
	shader = shader_definition_get(reference->shader.index);

	return shader && shader->base.type == SHADER_TYPE_MODEL ? reference->shader.index : NONE;
}

/* the stream's package, pak_stream.s3dpak, beside the level's, or NULL */
static struct saber_pak *saber_stream_pak(
	void)
{
	if (!saber_models_globals.stream_tried)
	{
		char path[1100];
		char const *reason;

		saber_models_globals.stream_tried = TRUE;
		snprintf(path, sizeof(path), "%s%s", saber_models_globals.folder, STREAM_PAK_NAME);
		saber_models_globals.stream = saber_pak_open(path, &reason);
		if (saber_models_globals.stream)
			platform_log("anniversary: textures from %s", path);
		else
			platform_log("anniversary: no textures from %s: %s", path, reason);
	}

	return saber_models_globals.stream;
}

/* The texture `texture_name`, of the most detail there is that the texture
cache can draw: the level's package's full texture, else the stream's
(pak_stream.s3dpak, beside the level's: most full textures are there), else
the level's 64-pixel one. FALSE, with *reason saying why, when there is none
this build draws. */
static boolean texture_find(
	struct saber_pak *pak,
	char const *texture_name,
	struct saber_texture *texture,
	char const **source,
	char const **reason)
{
	struct saber_pak *paks[3];
	long types[3];
	char const *sources[3];
	long index;

	paks[0] = pak;
	types[0] = _saber_entry_textures;
	sources[0] = "the level's package";
	paks[1] = saber_stream_pak();
	types[1] = _saber_entry_textures;
	sources[1] = "pak_stream";
	paks[2] = pak;
	types[2] = _saber_entry_textures_mips64;
	sources[2] = "the level's 64-pixel mipmaps";
	*reason = "the packages have not got its texture";
	for (index = 0; index < 3; index++)
	{
		long entry = paks[index] ? saber_pak_find(paks[index], texture_name, types[index]) : NONE;
		unsigned char *bytes;
		unsigned long size = 0;

		if (entry == NONE)
			continue;
		bytes = saber_pak_read(paks[index], entry, &size);
		if (!bytes)
		{
			*reason = "its texture cannot be read";
			continue;
		}
		if (!saber_texture_read(texture, bytes, size))
		{
			*reason = "its texture is not one this build draws (DXT)";
			free(bytes);
			continue;
		}
		free(bytes);
		/* (what the texture cache takes for it, with its mipmaps' padding) */
		if (texture->pixels_size > MAXIMUM_TEXTURE_SIZE)
		{
			*reason = "its texture is larger than the texture cache draws among others";
			saber_texture_dispose(texture);
			continue;
		}
		*source = sources[index];
		return TRUE;
	}

	return FALSE;
}

/* Reads the package's texture `texture_name` as a bitmap of the map's
bitmap tag `bitmap_tag`, as the texture cache draws a Custom Edition map's
(not yet played); FALSE with *reason saying why not. */
static boolean saber_texture_bitmap(
	struct saber_pak *pak,
	long bitmap_tag,
	char const *texture_name,
	struct saber_texture *texture,
	struct bitmap_data *bitmap,
	char const **reason)
{
	char const *source = NULL;

	csmemset(texture, 0, sizeof(*texture));
	csmemset(bitmap, 0, sizeof(*bitmap));
	if (!texture_find(pak, texture_name, texture, &source, reason))
		return FALSE;
	platform_log("anniversary: the texture %s from %s, %dx%d, %d mipmaps after the first", texture_name, source,
		texture->width, texture->height, texture->mipmap_count);
	bitmap->signature = BITMAP_GROUP_TAG;
	bitmap->width = texture->width;
	bitmap->height = texture->height;
	bitmap->depth = 1;
	bitmap->type = _bitmap_type_2d;
	bitmap->format = texture->format;
	bitmap->flags = FLAG(_bitmap_has_power_of_two_dimensions_bit) | FLAG(_bitmap_compressed_bit) |
		FLAG(_bitmap_cached_bit);
	bitmap->mipmap_count = texture->mipmap_count;
	bitmap->pixels_offset = 0;
	bitmap->pixels_size = bitmap_get_pixel_data_size(bitmap);
	bitmap->tag_index = bitmap_tag;
	bitmap->cache_block_index = NONE;
	*reason = "its texture's pixels are not all there";
	if (bitmap->pixels_size <= 0 || bitmap->pixels_size > texture->pixels_size)
	{
		saber_texture_dispose(texture);
		return FALSE;
	}

	return TRUE;
}

/* the textures played over the map's bitmap tag, or NULL */
static struct saber_bitmap *bitmap_played_get(
	long tag_index)
{
	struct saber_bitmap *played;

	for (played = saber_models_globals.bitmaps; played; played = played->next)
	{
		if (played->tag_index == tag_index)
			return played;
	}

	return NULL;
}

/* whether the shader or bitmap tag is already played over */
static boolean shader_played(
	long tag_index)
{
	struct saber_shader const *played;

	for (played = saber_models_globals.shaders; played; played = played->next)
	{
		if (played->tag_index == tag_index)
			return TRUE;
	}

	return FALSE;
}

/* whether a model shader lights itself (its self-illumination's colours,
which its multipurpose map masks) */
static boolean shader_self_illuminated(
	struct shader_model_definition const *shader)
{
	real_rgb_color const *lower = &shader->model.self_illumination_animation_color_lower_bound;
	real_rgb_color const *upper = &shader->model.self_illumination_animation_color_upper_bound;

	return lower->red > 0.f || lower->green > 0.f || lower->blue > 0.f ||
		upper->red > 0.f || upper->green > 0.f || upper->blue > 0.f;
}

/* Has the map's model shader drawn as its base map alone (the classic
model's other maps gone, its base map's scale none), FALSE when there is no
memory for it. */
static boolean shader_plug(
	long shader_tag)
{
	struct shader_model_definition *shader = (struct shader_model_definition *)shader_definition_get(shader_tag);
	struct saber_shader *played = malloc(sizeof(*played));

	if (!played)
		return FALSE;
	played->tag_index = shader_tag;
	played->map_shader = *shader;
	played->next = saber_models_globals.shaders;
	saber_models_globals.shaders = played;
	shader->model.map_u_scale = 1.f;
	shader->model.map_v_scale = 1.f;
	shader->model.multipurpose_map.index = NONE;
	/* (and its detail map, whose pattern (a cyborg's mottling) the
	template's texture coordinates do not lay as the classic model's did) */
	shader->model.detail_map.index = NONE;
	/* (with no multipurpose map to mask it, an object's change colour would
	tint it whole: the package's textures have their colours) */
	shader->model.change_color_source = 0;
	/* (nor drawn as a decal blended over the rest, nor lit by itself,
	which the multipurpose map masked) */
	SET_FLAG(shader->model.flags, SHADER_MODEL_ALPHA_BLENDED_DECAL_BIT, FALSE);
	shader->model.self_illumination_color_source = 0;
	csmemset(&shader->model.self_illumination_animation_color_lower_bound, 0,
		sizeof(shader->model.self_illumination_animation_color_lower_bound));
	csmemset(&shader->model.self_illumination_animation_color_upper_bound, 0,
		sizeof(shader->model.self_illumination_animation_color_upper_bound));
	/* (the package's textures' alpha is no alpha test's: drawn whole) */
	SET_FLAG(shader->model.flags, SHADER_MODEL_NOT_ALPHA_TESTED_BIT, TRUE);
	shader->model.detail_mask = 0;
	shader->model.reflection_cube_map.index = NONE;

	return TRUE;
}

/* whether a part's material is an effect's (a muzzle flash, a light's
cone, an overshield, a weapon's counter's digits), which a model shader
would draw solid: such parts are left out */
static boolean material_effect(
	char const *material)
{
	return !strncmp(material, "sfx_", 4) || !strncmp(material, "lig_", 4) || !strncmp(material, "fx_", 3) ||
		strstr(material, "digits") != NULL;
}

/* the template's parts less those of effects and of no material, and past
the most a model's geometry has at a level of detail (let go of) */
static void template_effects_remove(
	struct saber_template *template_)
{
	long kept = 0;
	long index;

	for (index = 0; index < template_->part_count; index++)
	{
		struct saber_mesh_part *part = &template_->parts[index];

		/* (and a part of no material: a collision's or a marker's) */
		if (part->material_index < 0 || part->material_index >= template_->material_count ||
			material_effect(template_->materials[part->material_index]))
		{
			free(part->vertices);
			free(part->triangles);
			continue;
		}
		template_->parts[kept++] = *part;
	}
	template_->part_count = kept;
	{
		long level_parts[5] = { 0, 0, 0, 0, 0 };

		kept = 0;
		for (index = 0; index < template_->part_count; index++)
		{
			struct saber_mesh_part *part = &template_->parts[index];
			long level = MIN(part->lod, 4);

			if (level_parts[level] >= MAXIMUM_PARTS_PER_MODEL_GEOMETRY)
			{
				free(part->vertices);
				free(part->triangles);
				continue;
			}
			level_parts[level]++;
			template_->parts[kept++] = *part;
		}
		template_->part_count = kept;
	}
}

/* the template's parts less those far from the rest (let go of): a
part's middle more than four times the size of the template's largest part
from that part's (c_uplink__h carries crates of a scene 69 metres off) */
static void template_strays_remove(
	struct saber_template *template_)
{
	real_point3d middles[MAXIMUM_PARTS_PER_MODEL_GEOMETRY * 5];
	real radii[MAXIMUM_PARTS_PER_MODEL_GEOMETRY * 5];
	long largest = NONE;
	long kept = 0;
	long index;

	if (template_->part_count > MAXIMUM_PARTS_PER_MODEL_GEOMETRY * 5)
		return;
	for (index = 0; index < template_->part_count; index++)
	{
		struct saber_mesh_part const *part = &template_->parts[index];
		real_point3d low = part->vertices[0].position;
		real_point3d high = part->vertices[0].position;
		real_vector3d across;
		long vertex;

		for (vertex = 1; vertex < part->vertex_count; vertex++)
		{
			real_point3d const *position = &part->vertices[vertex].position;

			low.x = MIN(low.x, position->x);
			low.y = MIN(low.y, position->y);
			low.z = MIN(low.z, position->z);
			high.x = MAX(high.x, position->x);
			high.y = MAX(high.y, position->y);
			high.z = MAX(high.z, position->z);
		}
		middles[index].x = (low.x + high.x) / 2.f;
		middles[index].y = (low.y + high.y) / 2.f;
		middles[index].z = (low.z + high.z) / 2.f;
		vector_from_points3d(&low, &high, &across);
		radii[index] = magnitude3d(&across) / 2.f;
		if (largest == NONE || part->vertex_count > template_->parts[largest].vertex_count)
			largest = index;
	}
	if (largest == NONE || radii[largest] <= 0.f)
		return;
	for (index = 0; index < template_->part_count; index++)
	{
		struct saber_mesh_part *part = &template_->parts[index];
		real_vector3d apart;

		vector_from_points3d(&middles[largest], &middles[index], &apart);
		if (magnitude3d(&apart) > 4.f * radii[largest])
		{
			free(part->vertices);
			free(part->triangles);
			continue;
		}
		template_->parts[kept++] = *part;
	}
	template_->part_count = kept;
}

/* a Halo node's name as the templates' bones are compared: lower case,
spaces for underscores */
static void node_name_compared(
	char const *name,
	char *compared,
	size_t size)
{
	size_t index;

	for (index = 0; name[index] && index + 1 < size; index++)
	{
		char character = name[index];

		compared[index] = character == '_' ? ' ' : (character >= 'A' && character <= 'Z') ? (char)(character - 'A' + 'a') :
			character;
	}
	compared[index] = 0;
}

/* a point or vector (w 1 or 0) times a 4x4 row-major matrix, as a row
vector */
static void bind_transform(
	real const *matrix,
	real x,
	real y,
	real z,
	real w,
	real *result)
{
	result[0] = x * matrix[0] + y * matrix[4] + z * matrix[8] + w * matrix[12];
	result[1] = x * matrix[1] + y * matrix[5] + z * matrix[9] + w * matrix[13];
	result[2] = x * matrix[2] + y * matrix[6] + z * matrix[10] + w * matrix[14];
}

/* whether a node's default inverse matrix is one (the map's runtime field,
made as its tags load) */
static boolean node_default_valid(
	struct model_node const *node)
{
	real_matrix4x3 const *matrix = &node->runtime_default_inverse_matrix;

	return matrix->scale > 0.0001f && matrix->scale < 10000.f &&
		matrix->position.x == matrix->position.x && matrix->position.y == matrix->position.y &&
		matrix->position.z == matrix->position.z;
}

/* the map's model's node of a template's bone's name, or NONE */
static short model_node_named(
	struct model const *model,
	char const *bone)
{
	long node_index;

	for (node_index = 0; node_index < model->nodes.count; node_index++)
	{
		char compared[64];

		node_name_compared(TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node)->name, compared,
			sizeof(compared));
		if (!strcmp(compared, bone))
			return (short)node_index;
	}

	return NONE;
}

/* The scale of the template's units to Halo's by its skeleton: the
lengths of its bones whose own and parent's names are the map's model's
nodes', to those nodes' apart in the model's default pose (however either
stands, a bone is as long). FALSE when too few bones are the model's to
tell. */
static boolean skeleton_scale(
	struct saber_template const *template_,
	struct model const *model,
	real *scale)
{
	real template_length = 0.f;
	real model_length = 0.f;
	long pairs = 0;
	long bone;

	for (bone = 0; bone < template_->bone_count; bone++)
	{
		long parent = template_->bone_parents[bone];
		short node;
		short parent_node;
		real_point3d origin;
		real_point3d parent_origin;
		real_vector3d apart;
		real dx;
		real dy;
		real dz;

		if (parent == NONE)
			continue;
		node = model_node_named(model, template_->bones[bone]);
		parent_node = model_node_named(model, template_->bones[parent]);
		if (node == NONE || parent_node == NONE || node == parent_node ||
			!node_default_valid(TAG_BLOCK_GET_ELEMENT(&model->nodes, node, struct model_node)) ||
			!node_default_valid(TAG_BLOCK_GET_ELEMENT(&model->nodes, parent_node, struct model_node)))
		{
			continue;
		}
		matrix4x3_inverse_transform_point(&TAG_BLOCK_GET_ELEMENT(&model->nodes, node, struct model_node)->
			runtime_default_inverse_matrix, global_origin3d, &origin);
		matrix4x3_inverse_transform_point(&TAG_BLOCK_GET_ELEMENT(&model->nodes, parent_node, struct model_node)->
			runtime_default_inverse_matrix, global_origin3d, &parent_origin);
		vector_from_points3d(&parent_origin, &origin, &apart);
		dx = template_->bone_origins[bone][0] - template_->bone_origins[parent][0];
		dy = template_->bone_origins[bone][1] - template_->bone_origins[parent][1];
		dz = template_->bone_origins[bone][2] - template_->bone_origins[parent][2];
		model_length += magnitude3d(&apart);
		template_length += square_root(dx * dx + dy * dy + dz * dz);
		pairs++;
	}
	if (pairs < 3 || template_length <= 0.f || model_length <= 0.f)
		return FALSE;
	*scale = model_length / template_length;

	return *scale > 0.f && *scale < 1000.f;
}

/* Has the template's vertices move with the map's model's nodes, in the
map's model's units: each bone the node of its name, else its nearest
parent's that has one (a vertex of a bone with none, as a vehicle's
parts, is the model's first node's, as it is). A vertex is put where its
bones put it in the map's model's default pose: in each of its bones' own
space (the template's bind), scaled to Halo's units, then where that node
is by default; so the template is the map's model's shape as the map's
animations pose it, though the two's skeletons stand otherwise. */
static void template_nodes_map(
	struct saber_template *template_,
	struct model const *model,
	real scale,
	long *matched_bones,
	long *posed_vertices,
	long *unposed_vertices)
{
	short *nodes = malloc(sizeof(*nodes) * ((size_t)template_->bone_count + 1));
	long *binds = malloc(sizeof(*binds) * ((size_t)template_->bone_count + 1));
	boolean defaults = TRUE;
	long bone;
	long part_index;
	long node_index;

	if (!nodes || !binds)
	{
		if (nodes)
			free(nodes);
		if (binds)
			free(binds);
		return;
	}
	for (node_index = 0; node_index < model->nodes.count; node_index++)
	{
		if (!node_default_valid(TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node)))
			defaults = FALSE;
	}
	/* (each bone's node and the bone whose bind is used: its own, or its
	nearest parent's that has a node) */
	for (bone = 0; bone < template_->bone_count; bone++)
	{
		long at = bone;
		long depth;

		nodes[bone] = NONE;
		binds[bone] = NONE;
		for (depth = 0; at != NONE && depth < 64 && nodes[bone] == NONE; depth++)
		{
			for (node_index = 0; node_index < model->nodes.count; node_index++)
			{
				char compared[64];

				node_name_compared(TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node)->name, compared,
					sizeof(compared));
				if (!strcmp(compared, template_->bones[at]))
				{
					nodes[bone] = (short)node_index;
					binds[bone] = at;
					break;
				}
			}
			if (nodes[bone] == NONE)
				at = template_->bone_parents[at];
		}
	}
	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part *part = &template_->parts[part_index];
		long index;

		for (index = 0; index < part->vertex_count; index++)
		{
			struct model_vertex_uncompressed *vertex = &part->vertices[index];
			real_point3d position = { 0.f, 0.f, 0.f };
			real_vector3d normal = { 0.f, 0.f, 0.f };
			real_vector3d tangent = { 0.f, 0.f, 0.f };
			boolean posed = defaults && template_->bone_count > 0;
			long influence;

			long slots[2];

			for (influence = 0; influence < 2; influence++)
			{
				long slot = vertex->nodes[influence];
				short node = slot >= 0 && slot < template_->bone_count ? nodes[slot] : NONE;

				if (node == NONE && vertex->node_weights[influence] > 0.f)
					posed = FALSE;
				slots[influence] = node == NONE ? NONE : binds[slot];
				vertex->nodes[influence] = node == NONE ? 0 : node;
			}
			for (influence = 0; posed && influence < 2; influence++)
			{
				long slot = slots[influence];
				struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, vertex->nodes[influence],
					struct model_node);
				real weight = vertex->node_weights[influence];
				real local[3];
				real_point3d point;
				real_point3d placed;
				real_vector3d direction;
				real_vector3d turned;

				if (weight <= 0.f || slot == NONE)
					continue;
				bind_transform(template_->bone_binds[slot], vertex->position.x, vertex->position.y, vertex->position.z,
					1.f, local);
				point.x = local[0] * scale;
				point.y = local[1] * scale;
				point.z = local[2] * scale;
				matrix4x3_inverse_transform_point(&node->runtime_default_inverse_matrix, &point, &placed);
				position.x += placed.x * weight;
				position.y += placed.y * weight;
				position.z += placed.z * weight;
				bind_transform(template_->bone_binds[slot], vertex->normal.i, vertex->normal.j, vertex->normal.k, 0.f,
					local);
				direction.i = local[0];
				direction.j = local[1];
				direction.k = local[2];
				matrix4x3_inverse_transform_vector(&node->runtime_default_inverse_matrix, &direction, &turned);
				normalize3d(&turned);
				normal.i += turned.i * weight;
				normal.j += turned.j * weight;
				normal.k += turned.k * weight;
				bind_transform(template_->bone_binds[slot], vertex->tangent.i, vertex->tangent.j, vertex->tangent.k, 0.f,
					local);
				direction.i = local[0];
				direction.j = local[1];
				direction.k = local[2];
				matrix4x3_inverse_transform_vector(&node->runtime_default_inverse_matrix, &direction, &turned);
				tangent.i += turned.i * weight;
				tangent.j += turned.j * weight;
				tangent.k += turned.k * weight;
			}
			if (posed && normalize3d(&normal) > 0.0001f)
			{
				real along = dot_product3d(&tangent, &normal);

				(*posed_vertices)++;
				vertex->position = position;
				vertex->normal = normal;
				tangent.i -= normal.i * along;
				tangent.j -= normal.j * along;
				tangent.k -= normal.k * along;
				if (normalize3d(&tangent) < 0.0001f)
				{
					perpendicular3d(&normal, &tangent);
					normalize3d(&tangent);
				}
				vertex->tangent = tangent;
				cross_product3d(&normal, &tangent, &vertex->binormal);
				normalize3d(&vertex->binormal);
			}
			else
			{
				/* (as it is, in the model's space, scaled) */
				(*unposed_vertices)++;
				vertex->position.x *= scale;
				vertex->position.y *= scale;
				vertex->position.z *= scale;
			}
		}
	}
	for (bone = 0; bone < template_->bone_count; bone++)
	{
		if (binds[bone] == bone)
			(*matched_bones)++;
	}
	free(nodes);
	free(binds);
}

/* Turns each part's triangles to face as Halo's do, clockwise about their
vertices' normals, if most do not: a template posed by a skeleton
(template_nodes_map) may be turned over by it, its bones' own spaces
Saber3D's, of the other hand to Halo's axes. */
static void parts_face_halo(
	struct saber_template *template_)
{
	long part_index;

	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part *part = &template_->parts[part_index];
		long agree = 0;
		long index;

		for (index = 0; index < part->triangle_count; index++)
		{
			struct model_vertex_uncompressed const *a = &part->vertices[part->triangles[3 * index]];
			struct model_vertex_uncompressed const *b = &part->vertices[part->triangles[3 * index + 1]];
			struct model_vertex_uncompressed const *c = &part->vertices[part->triangles[3 * index + 2]];
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
		if (agree * 2 <= part->triangle_count)
			continue;
		for (index = 0; index < part->triangle_count; index++)
		{
			unsigned short swap = part->triangles[3 * index + 1];

			part->triangles[3 * index + 1] = part->triangles[3 * index + 2];
			part->triangles[3 * index + 2] = swap;
		}
	}
}

/* the package's template of a model of the map, by the model's name: its
last folder (or, for a first-person model, its folder's, "_fp"), lower
case with underscores for spaces, as it is or followed by "__h" or another
"__" kind, or by the map's name and those ("light_fixture_floor1_b30__h").
The first that has a mesh, read (*template_name its name); FALSE when none
has */
static boolean template_find(
	struct saber_pak *pak,
	char const *model_name,
	char const *map_name,
	struct saber_template *template_,
	char *template_name,
	size_t template_name_size)
{
	char base[128];
	char const *end = model_name + strlen(model_name);
	char const *start;
	size_t length;
	size_t index;
	long pass;
	long count = saber_pak_entry_count(pak);

	/* (its last folder: "weapons\\pistol\\pistol" pistol, "...\\pistol\\fp\\fp"
	pistol_fp) */
	start = end;
	while (start > model_name && start[-1] != '\\')
		start--;
	if (!_stricmp(start, "fp") && start > model_name)
	{
		char const *folder_end = start - 1;
		char const *folder = folder_end;

		while (folder > model_name && folder[-1] != '\\')
			folder--;
		while (folder > model_name && folder_end - folder == 2 && !_strnicmp(folder, "fp", 2))
		{
			folder_end = folder - 1;
			folder = folder_end;
			while (folder > model_name && folder[-1] != '\\')
				folder--;
		}
		length = MIN((size_t)(folder_end - folder), sizeof(base) - 4);
		memcpy(base, folder, length);
		strcpy(base + length, "_fp");
	}
	else if (!_stricmp(start, "projectile") && start > model_name)
	{
		/* (a projectile's is its weapon's: "weapons\\needler\\projectile\\
		projectile" needler_projectile) */
		char const *folder_end = start - 1;
		char const *folder;

		while (folder_end > model_name && folder_end[-1] != '\\')
			folder_end--;
		folder_end = folder_end > model_name ? folder_end - 1 : folder_end;
		folder = folder_end;
		while (folder > model_name && folder[-1] != '\\')
			folder--;
		length = MIN((size_t)(folder_end - folder), sizeof(base) - 12);
		memcpy(base, folder, length);
		strcpy(base + length, "_projectile");
	}
	else
	{
		length = MIN((size_t)(end - start), sizeof(base) - 1);
		memcpy(base, start, length);
		base[length] = 0;
	}
	for (index = 0; base[index]; index++)
	{
		if (base[index] == ' ')
			base[index] = '_';
		else if (base[index] >= 'A' && base[index] <= 'Z')
			base[index] = (char)(base[index] - 'A' + 'a');
	}

	/* (as it is; "__h"; with the map's name, "__h"; any other "__" kind,
	without and with the map's name) */
	for (pass = 0; pass < 6; pass++)
	{
		long entry;

		for (entry = 0; entry < count; entry++)
		{
			char const *name = saber_pak_entry_name(pak, entry);
			char wanted[200];
			boolean match;

			if (saber_pak_entry_type(pak, entry) != _saber_entry_templates)
				continue;
			switch (pass)
			{
			case 0:
				match = !strcmp(name, base);
				break;
			case 1:
				snprintf(wanted, sizeof(wanted), "%s__h", base);
				match = !strcmp(name, wanted);
				break;
			case 2:
				snprintf(wanted, sizeof(wanted), "%s_%s__h", base, map_name);
				match = !strcmp(name, wanted);
				break;
			case 3:
				snprintf(wanted, sizeof(wanted), "%s_%s", base, map_name);
				match = !strcmp(name, wanted);
				break;
			default:
				if (pass == 4)
					snprintf(wanted, sizeof(wanted), "%s__", base);
				else
					snprintf(wanted, sizeof(wanted), "%s_%s__", base, map_name);
				/* (a far plane's stand-in is not the model) */
				match = !strncmp(name, wanted, strlen(wanted)) && !strstr(name, "farplane") && !strstr(name, "_far_");
				break;
			}
			if (match)
			{
				unsigned long size = 0;
				unsigned char *bytes = saber_pak_read(pak, entry, &size);
				char const *reason;
				boolean read = bytes && saber_template_read(template_, bytes, size, saber_pak_xbox360(pak), &reason);

				if (bytes)
					free(bytes);
				if (read)
				{
					template_effects_remove(template_);
					template_strays_remove(template_);
					if (template_->part_count > 0)
					{
						snprintf(template_name, template_name_size, "%s", name);
						return TRUE;
					}
				}
				saber_template_dispose(template_);
			}
		}
	}

	return FALSE;
}

/* Draws the map's model `tag_index` as the package's template of it, if it
has one: its textures read and its model made before any of the map's tags
are played over, so that one that cannot be drawn is the map's as it was.
TRUE if it is drawn. */
static boolean model_play(
	struct saber_pak *pak,
	long tag_index,
	char const *map_name)
{
	struct model *definition = model_definition_get(tag_index);
	struct saber_template template_;
	char template_name[128];
	struct saber_model *made = NULL;
	short *shader_indices = NULL;
	long shader_tags[MAXIMUM_SHADERS_PER_MODEL];
	short usable[MAXIMUM_SHADERS_PER_MODEL];
	/* each of the template's materials drawn (in the order they are first
	drawn): its place among them, its shader of the map's model, the bitmap
	tag of that shader's base map and its texture's place among that
	bitmap's; the textures read for it (not yet played: NULL, a texture
	already there) */
	short material_places[256];
	short material_shaders[MAXIMUM_SABER_MATERIALS];
	long material_bitmaps[MAXIMUM_SABER_MATERIALS];
	short material_permutations[MAXIMUM_SABER_MATERIALS];
	boolean material_cutouts[MAXIMUM_SABER_MATERIALS];
	struct saber_texture *material_textures[MAXIMUM_SABER_MATERIALS];
	struct bitmap_data *material_bitmap_data[MAXIMUM_SABER_MATERIALS];
	long usable_count = 0;
	long material_count = 0;
	long shader_count = 0;
	long level_parts[5] = { 0, 0, 0, 0, 0 };
	real scale;
	long matched_bones = 0;
	long posed_vertices = 0;
	long unposed_vertices = 0;
	char const *reason = "out of memory";
	long index;

	for (made = saber_models_globals.models; made; made = made->next)
	{
		if (made->tag_index == tag_index)
			return FALSE;
	}
	csmemset(&template_, 0, sizeof(template_));
	if (!template_find(pak, tag_get_name(tag_index), map_name, &template_, template_name, sizeof(template_name)))
		return FALSE;
	shader_indices = malloc(sizeof(*shader_indices) * ((size_t)template_.part_count + 1));
	if (!shader_indices)
		goto refused;
	for (index = 0; index < template_.part_count; index++)
		level_parts[MIN(template_.parts[index].lod, 4)]++;
	reason = "a level of detail of its template has more parts than a model's geometry can";
	for (index = 0; index < 5; index++)
	{
		if (level_parts[index] > MAXIMUM_PARTS_PER_MODEL_GEOMETRY)
			goto refused;
	}
	/* (the renderer skins a part of a map's model by the model's nodes, at
	most so many) */
	reason = "the map's model has more nodes than its parts are drawn with";
	if (definition->nodes.count <= 0 || definition->nodes.count >= RASTERIZER_MAXIMUM_NODES_PER_MODEL)
		goto refused;
	/* (a skinned template's by its skeleton, else by its size) */
	if (skeleton_scale(&template_, definition, &scale))
	{
		platform_log("anniversary: %s's units are %f world units, by its skeleton", template_name, scale);
	}
	else
	{
		scale = template_scale(&template_, template_name, definition);
	}
	template_nodes_map(&template_, definition, scale, &matched_bones, &posed_vertices, &unposed_vertices);
	parts_face_halo(&template_);
	if (posed_vertices)
	{
		platform_log("anniversary: %s's bones: %ld of %ld are the model's nodes; %ld vertices posed by them, %ld not",
			template_name, matched_bones, template_.bone_count, posed_vertices, unposed_vertices);
	}

	/* the map's model's shaders that can be drawn with a texture (model
	shaders with a base map), each tag once, and each material's: the
	first's the first, the next's the next ... Those drawn solid and unlit
	by themselves come first, then the self-illuminated (a grunt's "inset
	lights"), then the decals blended over the rest, which would draw a
	material all but unseen */
	{
		long pass;

		for (pass = 0; pass < 3; pass++)
		{
			for (index = 0; index < definition->shaders.count && usable_count < MAXIMUM_SHADERS_PER_MODEL; index++)
			{
				long shader_tag = model_shader_tag(definition, (short)index);
				struct shader_model_definition const *shader;
				boolean decal;
				boolean lit;
				long other;

				if (shader_tag == NONE)
					continue;
				shader = (struct shader_model_definition const *)shader_definition_get(shader_tag);
				if (shader->model.base_map.index == NONE)
					continue;
				decal = TEST_FLAG(shader->model.flags, SHADER_MODEL_ALPHA_BLENDED_DECAL_BIT);
				lit = shader_self_illuminated(shader);
				if (pass != (decal ? 2 : lit ? 1 : 0))
					continue;
				for (other = 0; other < usable_count &&
					model_shader_tag(definition, usable[other]) != shader_tag; other++)
				{
				}
				if (other == usable_count)
					usable[usable_count++] = (short)index;
			}
		}
	}
	reason = "the map's model has no model shader with a base map";
	if (!usable_count)
		goto refused;
	csmemset(material_textures, 0, sizeof(material_textures));
	csmemset(material_bitmap_data, 0, sizeof(material_bitmap_data));
	for (index = 0; index < 256; index++)
		material_places[index] = NONE;
	/* each material its own of the usable shaders, in turn, and its texture
	a bitmap of that shader's base map of its own (its permutation) */
	for (index = 0; index < template_.part_count; index++)
	{
		short material = template_.parts[index].material_index;
		long place;
		long shader_tag;
		long bitmap_tag;
		struct saber_bitmap *played;
		long existing = 0;
		long other;

		reason = "a part's material is not one of its template's";
		if (material < 0 || material >= template_.material_count || material >= 256)
			goto refused;
		if (material_places[material] != NONE)
		{
			shader_indices[index] = (short)(definition->shaders.count + material_places[material]);
			continue;
		}
		reason = "its template has more materials than are drawn";
		if (material_count >= MAXIMUM_SABER_MATERIALS)
			goto refused;
		place = material_count++;
		material_places[material] = (short)place;
		shader_indices[index] = (short)(definition->shaders.count + place);
		material_shaders[place] = usable[place % usable_count];
		shader_tag = model_shader_tag(definition, material_shaders[place]);
		bitmap_tag = ((struct shader_model_definition *)shader_definition_get(shader_tag))->model.base_map.index;
		material_bitmaps[place] = bitmap_tag;
		for (other = 0; other < shader_count && shader_tags[other] != shader_tag; other++)
		{
		}
		if (other == shader_count && !shader_played(shader_tag))
			shader_tags[shader_count++] = shader_tag;

		/* (a texture this bitmap has, played before or read for this model,
		is its place; another is read, its place the bitmap's next) */
		played = bitmap_played_get(bitmap_tag);
		for (other = 0; played && other < played->count; other++)
		{
			if (!strcmp(played->names[other], template_.materials[material]))
				break;
		}
		material_cutouts[place] = FALSE;
		if (played && other < played->count)
		{
			material_permutations[place] = (short)other;
			material_cutouts[place] = played->cutouts[other];
			continue;
		}
		existing = played ? played->count : 0;
		for (other = 0; other < place; other++)
		{
			if (material_bitmaps[other] == bitmap_tag && material_textures[other])
				existing++;
		}
		reason = "a bitmap of the map's holds too many of its textures";
		if (existing >= MAXIMUM_SABER_BITMAP_TEXTURES)
			goto refused;
		material_permutations[place] = (short)existing;
		reason = "out of memory";
		material_textures[place] = malloc(sizeof(struct saber_texture));
		material_bitmap_data[place] = malloc(sizeof(struct bitmap_data));
		if (!material_textures[place] || !material_bitmap_data[place])
			goto refused;
		if (!saber_texture_bitmap(pak, bitmap_tag, template_.materials[material], material_textures[place],
				material_bitmap_data[place], &reason))
		{
			free(material_textures[place]);
			material_textures[place] = NULL;
			goto refused;
		}
		material_cutouts[place] = saber_texture_cutout(material_textures[place]) ? TRUE : FALSE;
		/* (one that is not cut out drawn whole, though its shader be alpha
		tested for another's) */
		if (!material_cutouts[place])
			saber_texture_make_solid(material_textures[place]);
	}
	made = saber_model_new(tag_index, definition, &template_, shader_indices, 1.f, &reason);
	if (!made)
		goto refused;

	/* its shaders: the map's model's, then each material's */
	reason = "out of memory";
	made->shaders = malloc(sizeof(*made->shaders) * ((size_t)definition->shaders.count + (size_t)material_count + 1));
	if (!made->shaders)
	{
		saber_model_free(made);
		made = NULL;
		goto refused;
	}
	for (index = 0; index < definition->shaders.count; index++)
		made->shaders[index] = *TAG_BLOCK_GET_ELEMENT(&definition->shaders, index, struct model_shader_reference);
	for (index = 0; index < material_count; index++)
	{
		made->shaders[definition->shaders.count + index] = made->shaders[material_shaders[index]];
		made->shaders[definition->shaders.count + index].permutation_index = material_permutations[index];
	}
	made->model.shaders.count = definition->shaders.count + material_count;
	made->model.shaders.address = made->shaders;
	made->model.shaders.definition = NULL;

	/* (played: the textures read into their bitmaps, the shaders, the model) */
	for (index = 0; index < material_count; index++)
	{
		struct saber_bitmap *played;
		long slot;

		if (!material_textures[index])
			continue;
		played = bitmap_played_get(material_bitmaps[index]);
		if (!played)
		{
			struct bitmap_group *group = bitmap_group_get(material_bitmaps[index]);

			played = malloc(sizeof(*played));
			if (!played)
				continue;
			csmemset(played, 0, sizeof(*played));
			played->tag_index = material_bitmaps[index];
			played->map_group = *group;
			group->sequences.count = 0;
			group->bitmaps.address = played->bitmaps;
			group->bitmaps.definition = NULL;
			played->next = saber_models_globals.bitmaps;
			saber_models_globals.bitmaps = played;
		}
		slot = material_permutations[index];
		if (slot >= MAXIMUM_SABER_BITMAP_TEXTURES)
			continue;
		played->bitmaps[slot] = *material_bitmap_data[index];
		played->textures[slot] = *material_textures[index];
		{
			short material;

			for (material = 0; material < 256 && material_places[material] != index; material++)
			{
			}
			csstrncpy(played->names[slot], material < template_.material_count ? template_.materials[material] : "",
				sizeof(played->names[slot]) - 1);
		}
		played->cutouts[slot] = material_cutouts[index];
		played->count = MAX(played->count, slot + 1);
		bitmap_group_get(played->tag_index)->bitmaps.count = played->count;
		free(material_textures[index]);
		free(material_bitmap_data[index]);
		material_textures[index] = NULL;
		material_bitmap_data[index] = NULL;
	}
	for (index = 0; index < shader_count; index++)
		shader_plug(shader_tags[index]);
	/* (a shader of a texture cut out by its alpha, leaves', is alpha
	tested again, and drawn from both sides as Saber3D draws leaves) */
	for (index = 0; index < material_count; index++)
	{
		struct shader_model_definition *shader;

		if (!material_cutouts[index])
			continue;
		shader = (struct shader_model_definition *)shader_definition_get(
			model_shader_tag(definition, material_shaders[index]));
		SET_FLAG(shader->model.flags, SHADER_MODEL_NOT_ALPHA_TESTED_BIT, FALSE);
		SET_FLAG(shader->model.flags, SHADER_MODEL_TWO_SIDED_BIT, TRUE);
	}
	*definition = made->model;
	made->next = saber_models_globals.models;
	saber_models_globals.models = made;
	platform_log("anniversary: %s drawn as the template %s (%ld parts, %ld levels of detail)",
		tag_get_name(tag_index), template_name, made->part_count, made->geometry_count);
	saber_template_dispose(&template_);
	free(shader_indices);

	return TRUE;

refused:
	platform_log("anniversary: %s is drawn as the map's: the template %s: %s", tag_get_name(tag_index), template_name,
		reason);
	for (index = 0; index < material_count; index++)
	{
		if (material_textures[index])
		{
			saber_texture_dispose(material_textures[index]);
			free(material_textures[index]);
		}
		if (material_bitmap_data[index])
			free(material_bitmap_data[index]);
	}
	saber_template_dispose(&template_);
	if (shader_indices)
		free(shader_indices);

	return FALSE;
}

/* ---------- public code */

void saber_models_tags_loaded(
	char const *map_name)
{
	char path[1024];
	struct saber_pak *pak;
	char const *reason;

	/* (what was made for a map whose tags went without its being let go of
	is not written over this one's) */
	saber_models_tags_unloaded(FALSE);
	if (!map_name || !*map_name || !config_boolean("display.anniversary_models"))
		return;
	/* (in display.anniversary_paks's folder, else in paks next to the
	executable: a config.toml written before paks was the default keeps
	the data folder's anniversary) */
	pak_path(config_string("display.anniversary_paks"), map_name, path, sizeof(path));
	pak = saber_pak_open(path, &reason);
	if (!pak)
	{
		char other[1024];
		char const *other_reason;

		pak_path(DEFAULT_PAKS_FOLDER, map_name, other, sizeof(other));
		if (strcmp(other, path) && (pak = saber_pak_open(other, &other_reason)) != NULL)
			strcpy(path, other);
	}
	if (!pak)
	{
		platform_log("anniversary: no models for %s: %s: %s", map_name, path, reason);
		return;
	}
	platform_log("anniversary: %s's models from %s", map_name, path);
	{
		char *separator = MAX(strrchr(path, '/'), strrchr(path, '\\'));
		size_t length = separator ? (size_t)(separator - path) + 1 : 0;

		memcpy(saber_models_globals.folder, path, length);
		saber_models_globals.folder[length] = 0;
	}
	/* (every model of the map's: its objects', not its structure's, which
	is no model) */
	{
		struct tag_iterator iterator;
		char const *name = strrchr(map_name, '\\');
		long tag_index;
		long drawn = 0;
		long models = 0;

		name = name ? name + 1 : map_name;
		tag_iterator_new(&iterator, MODELS_GROUP_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			models++;
			if (model_play(pak, tag_index, name))
				drawn++;
		}
		platform_log("anniversary: %ld of the map's %ld models drawn as templates", drawn, models);
	}
	saber_pak_close(pak);
	if (saber_models_globals.stream)
		saber_pak_close(saber_models_globals.stream);
	saber_models_globals.stream = NULL;
	saber_models_globals.stream_tried = FALSE;
}

void saber_models_tags_unloaded(
	int give_back)
{
	while (saber_models_globals.models)
	{
		struct saber_model *made = saber_models_globals.models;

		saber_models_globals.models = made->next;
		if (give_back)
			*model_definition_get(made->tag_index) = made->map_model;
		saber_model_free(made);
	}
	while (saber_models_globals.shaders)
	{
		struct saber_shader *shader = saber_models_globals.shaders;

		saber_models_globals.shaders = shader->next;
		if (give_back)
			*(struct shader_model_definition *)shader_definition_get(shader->tag_index) = shader->map_shader;
		free(shader);
	}
	while (saber_models_globals.bitmaps)
	{
		struct saber_bitmap *bitmap = saber_models_globals.bitmaps;

		long index;

		saber_models_globals.bitmaps = bitmap->next;
		if (give_back)
			*bitmap_group_get(bitmap->tag_index) = bitmap->map_group;
		for (index = 0; index < bitmap->count; index++)
			saber_texture_dispose(&bitmap->textures[index]);
		free(bitmap);
	}
}

int saber_bitmaps_contain(
	struct bitmap_data const *bitmap)
{
	struct saber_bitmap const *played;

	for (played = saber_models_globals.bitmaps; played; played = played->next)
	{
		if (bitmap >= played->bitmaps && bitmap < played->bitmaps + played->count)
			return TRUE;
	}

	return FALSE;
}

int saber_bitmaps_read(
	struct bitmap_data *bitmap,
	void *buffer)
{
	struct saber_bitmap const *played;

	for (played = saber_models_globals.bitmaps; played; played = played->next)
	{
		if (bitmap >= played->bitmaps && bitmap < played->bitmaps + played->count)
		{
			struct saber_texture const *texture = &played->textures[bitmap - played->bitmaps];

			if (!texture->pixels || texture->pixels_size < bitmap->pixels_size)
				return FALSE;
			memcpy(buffer, texture->pixels, (size_t)bitmap->pixels_size);
			halo_custom_edition_texels_channels(buffer, _custom_edition_channels_xbox);
			rasterizer_xbox_bitmap_rebuild_hardware_format(bitmap);
			return TRUE;
		}
	}

	return FALSE;
}
