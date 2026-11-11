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
folder, "weapons\pistol\pistol" pistol, "weapons\pistol\fp\fp" pistol_fp
on the Xbox 360 and pistol in the Master Chief Collection,
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
first region's permutations draw the template, its others nothing, except
a region named head: that draws the one head the permutation names (a
marine's "head_charles_helmet-10" the template's head_charles_helmet_10),
not every head at once. A
template's meshes of a rank (an Elite's "metal_major", a Grunt's
"back_major"; an Elite's spec ops' and zealots' in its "_special" template)
are drawn for that rank alone, the rank of an object by its colour (blue
minor, red major, dark spec ops, gold zealot), as Halo draws its ranks one
model coloured. The map's model's parts drawn by transparent shaders (a
Jackal's energy shield) are drawn with the template's, except its glass
when the template has glass of its own. Each of
the template's materials is drawn with one of the map's model's shaders
that can be (model shaders with a base map: the first material the first,
the next the next ...), whose base map becomes the package's texture of
the material (its full texture, from the level's package or
pak_stream.s3dpak beside it, or, in the Master Chief Collection's
packages, from inplace1.ipak, inplace2.ipak, inplace2o.ipak and
common.ipak in that folder, else its 64-pixel TexturesMips64), drawn
without alpha testing. A glass is blended on its texture's alpha. The
shader loses its multipurpose and reflection
maps, which are the classic model's, and the reflection's brightness and
tint, which are added on the texture. A shader or bitmap another model has
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
#include "objects/objects.h"
#include "units/units.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_model_types.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "rasterizer/rasterizer_models.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "rasterizer/rasterizer_lights.h"

/* port/linux/src/d3d8_gl.c: vertices kept on the card */
void *d3d8_gl_static_vertices_new(unsigned long written, float const *packed, unsigned long vertex_count,
	unsigned short const *indices, unsigned long index_count);
void d3d8_gl_static_vertices_update(void *handle, float const *packed);
void d3d8_gl_static_vertices_free(void *handle);
int d3d8_gl_static_vertices_draw(void *handle, unsigned long used);
void *d3d8_gl_static_vertices_new32(unsigned long written, float const *packed, unsigned long vertex_count,
	unsigned long const *indices, unsigned long index_count);
void d3d8_gl_static_vertices_update_range(void *handle, float const *packed, unsigned long first,
	unsigned long count);
int d3d8_gl_static_vertices_draw_ranges(void *handle, unsigned long used, unsigned long const *firsts,
	unsigned long const *counts, unsigned long range_count);
static void saber_level_batches_free(void);

#include "render/render.h"
#include "render/render_cameras_internal.h"
#include "objects/object_lights_rendering.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"
#include "shaders/shader_definitions.h"
#include "shaders/shader_texture_animation.h"
#include "tag_files/tag_groups.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h" /* port: the map's bitmaps' channel orders after Back, port/linux/game/custom_edition_bitmaps.c */
#include "saber.h"
#include "saber_toggle.h" /* port: the Back button's switch, port/linux/game/saber_toggle.c */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "rasterizer/xbox/rasterizer_xbox_pixel_shader.h"

/* the platform layer's (port_config.c, xbox_files.c, xbox_textures.c) */
int config_boolean(const char *name);
const char *config_string(const char *name);
void config_executable_folder(char *path, size_t size);
void platform_log(char const *format, ...);

/* (this file's lines in debug.txt as well as on the standard error, which a
Windows build shows nowhere: why a model of the map is drawn as it is) */
static void saber_log(
	char const *format,
	...)
{
	char line[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	platform_log("%s", line);
	error(_error_silent, "%s", line);
}
#define platform_log saber_log

void halo_custom_edition_texels_channels(void const *texels, unsigned char channel_order);

/* the game's (port) */
char *tag_get_name(long tag_index);
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);
void texture_cache_flush(void);

/* ---------- constants */

/* the package's file, in the folder of display.anniversary_paks (by
default paks, next to the executable) */
#define DEFAULT_PAKS_FOLDER "paks"
#define PAK_EXTENSION ".s3dpak"
/* the package of the full textures, beside the levels' */
#define STREAM_PAK_NAME "pak_stream.s3dpak"
/* the Collection's texture archives, in the same folder */
#define MAXIMUM_IPAKS 4
#define MAXIMUM_TEXTURE_PAKS 8

/* the most a texture's pixels may take (mod_bitmaps.c's: a quarter of the
texture cache, which draws it among others) */
#define MAXIMUM_TEXTURE_SIZE ((long)(HALO_PORT_TEXTURE_CACHE_SIZE / 4))

/* the most textures one bitmap of the map holds (as permutations), and the
most materials a template draws */
#define MAXIMUM_SABER_BITMAP_TEXTURES 32
#define MAXIMUM_SABER_MATERIALS 64
/* a head region's permutations drawn of their own geometry (a marine's
faces), besides the body */
#define MAXIMUM_SABER_HEAD_PERMUTATIONS 16

/* models.c's shader types and model flags */
#define SHADER_TYPE_ENVIRONMENT 3
#define SHADER_TYPE_MODEL 4
#define SHADER_TYPE_TRANSPARENT_GLASS 8
#define SHADER_TYPE_TRANSPARENT_METER 9
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

/* a part's flags of the package's: a vehicle's wreck, drawn once it is
destroyed, and its whole hull, drawn until then (saber_models_part_hidden) */
#define SABER_PART_WRECK_FLAG 0x40000000UL
#define SABER_PART_WHOLE_FLAG 0x20000000UL
/* a part of a group the map's region of its name permutes (a Flood combat
form's arm): its region's index in bits 20..23, and whether it is the
group's damaged copy, drawn at the region's ~damaged, or its whole one */
#define SABER_PART_REGION_FLAG 0x10000000UL
#define SABER_PART_DAMAGED_FLAG 0x08000000UL
#define SABER_PART_REGION_SHIFT 20
#define SABER_PART_REGION_MASK 0xFUL
/* a part of one variant of a group its region permutes (a marine's arms:
bare with gloves, or one of its full sleeves): its region's index in bits
20..23, as above, and in bits 12..19 the mask of the region's first eight
permutations it is drawn at. (Not an index of its model's masks: the model
drawn is the map's tag the template's was copied over, not the copy's) */
#define SABER_PART_VARIANT_FLAG 0x04000000UL
/* the rest of a sheet whose cards are cut from its corner: drawn solid,
though its material is cut out (models.c) */
#define SABER_PART_SOLID_FLAG 0x02000000UL
#define SABER_PART_VARIANT_SHIFT 12
#define SABER_PART_VARIANT_MASK 0xFFUL
#define MAXIMUM_SABER_VARIANT_MASKS 64

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
	/* the change colour that turns its grey plates the colour its armour is
	baked (saber_texture_band_color), drawn as its least rank's primary
	colour (an Elite minor's), and how many baked texels it was taken of;
	none, 0 */
	real_rgb_color band_color;
	long band_weight;
	struct model map_model;
	struct model model;
	struct model_region *regions;
	struct model_region_permutation *permutations;
	/* a geometry of each of the template's levels of detail, for each rank
	it is drawn for (those of the rank `ranks[0]` first, ...), and the
	same again for each head permutation after the body's */
	struct model_geometry geometries[5 * NUMBER_OF_SABER_RANKS * (1 + MAXIMUM_SABER_HEAD_PERMUTATIONS)];
	long geometry_count;
	long level_count;
	long rank_count;
	short ranks[NUMBER_OF_SABER_RANKS];
	struct model_geometry_part *parts;
	long part_count;
	/* the geometries' parts: each a part's, or a part of the map's model's
	(neither theirs to let go of) */
	struct model_geometry_part *geometry_parts;
	/* each variant's permutations, a bit each (SABER_PART_VARIANT_FLAG) */
	unsigned long variant_masks[MAXIMUM_SABER_VARIANT_MASKS];
	long variant_mask_count;
	/* a classic heat meter shrunk onto the template's gauge: its own
	buffer, not the map's */
	struct vertex_buffer meter_buffers[8];
	struct model_geometry_part const *meter_sources[8];
	/* where each fitted meter sits (its centre, in the model's space) */
	real_point3d meter_centres[8];
	/* the classic ammo counter's shift onto the template's counter (the
	assault rifle's), once found, and whether it was looked for */
	real_vector3d counter_shift;
	boolean counter_found;
	boolean counter_looked;
	long meter_buffer_count;
	byte *vertices;
	unsigned long vertices_size;
	boolean vertices_contiguous;
};

/* a shader of the map drawn with a texture of the package: its own
definition, given back as the map goes, and while Back draws the map's own
models (saber_toggle.c) */
struct saber_shader
{
	struct saber_shader *next;
	long tag_index;
	struct shader_model_definition map_shader;
	/* the map's shader as the package's models draw it: with the flags its
	parts are drawn by (saber_models_part_cutout, saber_models_part_blend).
	The package's templates and the map's own models can share a shader, so
the flags are this copy's, which the tags hold while the templates are
	drawn, and not the map's own, which the map's models keep */
	struct shader_model_definition played;
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
	/* the map's own group while the package's textures are not drawn: Back's
	switch gives it back and takes it again (saber_toggle.c) */
	struct bitmap_group classic_group;
	boolean shown;
	long count;
	/* the map's own bitmaps it keeps first, as they are, for the map's other
	shaders that draw this bitmap (the Master Chief's lights, of his armour's
	`cyborg`): the package's textures follow them. 0 for none */
	long classic_count;
	struct bitmap_data bitmaps[MAXIMUM_SABER_BITMAP_TEXTURES];
	struct saber_texture textures[MAXIMUM_SABER_BITMAP_TEXTURES];
	char names[MAXIMUM_SABER_BITMAP_TEXTURES][64];
	/* whether each is cut out by its alpha (saber_texture_cutout) */
	boolean cutouts[MAXIMUM_SABER_BITMAP_TEXTURES];
	/* whether each is glass, blended on its alpha */
	boolean blends[MAXIMUM_SABER_BITMAP_TEXTURES];
	/* whether it is a multipurpose map's: each texture a colour-change mask
	(saber_texture_read_mask), of the permutation of the texture its
	shader's base map has there */
	boolean masks;
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
	/* the Collection's .ipak texture archives in that folder */
	struct saber_pak *ipaks[MAXIMUM_IPAKS];
	long ipak_count;
	boolean ipaks_tried;
	/* the models drawn with ranks' geometries */
	long ranked_count;
	/* the shaders and bitmaps of the map's models that have no template,
	drawn as they are: none of them is played over (a sapling's leaves'
	shader, a tree's too) */
	long *kept_tags;
	long kept_count;
	/* the shaders a scrolling material (the plasma rifle's arc) was given:
	the same model's first-person and third-person copies share their
	shaders, so the other's arc takes it too, and its other materials (the
	heat gauge) do not */
	long scroll_shaders[8];
	long scroll_shader_count;
} saber_models_globals;

static boolean tag_kept(long tag_index);
static boolean map_glass_replaced(long shader_tag);
static boolean map_clear_replaced(long shader_tag);

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
triangles of no area, starting on an even index so it keeps its turn.
Those `skip` marks (when there is one) are left out; none left, one
triangle of no area */
static long part_strip(
	struct saber_mesh_part const *part,
	unsigned char const *skip,
	word *strip)
{
	long count = 0;
	long index;

	for (index = 0; index < part->triangle_count; index++)
	{
		word a = part->triangles[3 * index];

		if (skip && skip[index])
			continue;

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
	if (!count && part->triangle_count > 0)
	{
		strip[count++] = part->triangles[0];
		strip[count++] = part->triangles[0];
		strip[count++] = part->triangles[0];
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
	for (index = 0; index < made->meter_buffer_count; index++)
		rasterizer_vertex_buffer_delete(&made->meter_buffers[index]);
	if (made->geometry_parts)
		free(made->geometry_parts);
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

/* a permutation's name as a template's head is named: lower case, a
hyphen or a space an underscore ("head_charles_helmet-10" is
head_charles_helmet_10) */
static void variant_normalize(
	char const *name,
	char *out,
	long out_size)
{
	long at = 0;

	if (out_size <= 0)
		return;
	while (name[at] && at < out_size - 1)
	{
		char c = name[at];

		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		else if (c == '-' || c == ' ')
			c = '_';
		out[at] = c;
		at++;
	}
	out[at] = 0;
}

/* which of `keys` is `name`, or NONE */
static long permutation_variant_region(struct saber_template const *template_, struct model const *model);

static long head_key_index(
	char keys[][64],
	long count,
	char const *name)
{
	char key[64];
	long index;

	variant_normalize(name, key, (long)sizeof(key));
	for (index = 0; index < count; index++)
	{
		if (!strcmp(keys[index], key))
			return index;
	}

	return NONE;
}

/* a first-person model (weapons\pistol\fp\fp, the hands characters\cyborg\fp\fp).
The view draws it with no size, so Halo's level comes out the lowest, and
the template's most detailed is wanted there */
static boolean model_is_first_person(
	char const *name)
{
	long index;

	if (!name)
		return FALSE;
	for (index = 0; name[index]; index++)
	{
		if ((name[index] == '\\' || name[index] == '/') &&
			(name[index + 1] == 'f' || name[index + 1] == 'F') &&
			(name[index + 2] == 'p' || name[index + 2] == 'P') &&
			(name[index + 3] == '\\' || name[index + 3] == '/' || name[index + 3] == 0))
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* defined below, with the other material tests */
static boolean material_monicle(struct saber_template const *template_, struct saber_mesh_part const *part);
static boolean head_without_monicle(char const *key);
static boolean material_scrolls(char const *material);
static boolean material_lightning(char const *material);

/* whether the part is the classic heat meter */
static boolean part_is_meter(
	struct model const *map_model,
	struct model_geometry_part const *part)
{
	long shader_tag;
	struct shader const *shader;

	if (!map_model || !VALID_INDEX(part->shader_index, map_model->shaders.count))
		return FALSE;
	shader_tag = TAG_BLOCK_GET_ELEMENT(&map_model->shaders, part->shader_index,
		struct model_shader_reference)->shader.index;
	if (shader_tag == NONE)
		return FALSE;
	shader = shader_definition_get(shader_tag);

	return shader && shader->base.type == SHADER_TYPE_TRANSPARENT_METER;
}

/* whether the template's rear meters (`holo`) stand in for a gauge: the
plasma pistol's, which has no gauge of its own. Its classic meter is laid
where they are, and they are not drawn */
static boolean template_gauge_stand_in(
	struct saber_template const *template_)
{
	boolean pistol = FALSE;
	long index;

	for (index = 0; index < template_->material_count; index++)
	{
		if (strstr(template_->materials[index], "gauge"))
			return FALSE;
		if (strstr(template_->materials[index], "plasmapistol"))
			pistol = TRUE;
	}

	return pistol;
}

/* whether a template's part stands in for its gauge, not drawn: the
plasma pistol's rear meters (`holo`). Its own heat ring, of the parts of
`plasmapistol_02`, is left out of their strips (part_strip) */
static boolean part_stands_in(
	struct saber_template const *template_,
	struct saber_mesh_part const *part)
{
	return part->material_index >= 0 && part->material_index < template_->material_count &&
		strstr(template_->materials[part->material_index], "holo") != NULL && template_gauge_stand_in(template_);
}

/* whether a template's part is its gauge, or stands in for it: the plasma
pistol's own heat ring (`plasmapistol_02`), the classic meter laid where
it is, the ring itself not drawn */
static boolean part_gauge(
	struct saber_template const *template_,
	struct saber_mesh_part const *part)
{
	if (part->material_index < 0 || part->material_index >= template_->material_count)
		return FALSE;
	return strstr(template_->materials[part->material_index], "gauge") != NULL ||
		(template_gauge_stand_in(template_) &&
			strstr(template_->materials[part->material_index], "plasmapistol_02") != NULL);
}

/* the height of the template's gauge's face along the axis `normal`, the
outermost of its triangles over `point` (`outward` the face's side, 1 or
-1). FALSE if none is over it */
static boolean gauge_surface(
	struct saber_template const *template_,
	long normal,
	real outward,
	real const point[3],
	real *height)
{
	long first = (normal + 1) % 3;
	long second = (normal + 2) % 3;
	boolean found = FALSE;
	long part_index;

	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part const *part = &template_->parts[part_index];
		long triangle;

		if (part->lod != 0 || !part_gauge(template_, part))
		{
			continue;
		}
		for (triangle = 0; triangle < part->triangle_count; triangle++)
		{
			real corners[3][3];
			real area;
			real weights[3];
			real value;
			long corner;

			for (corner = 0; corner < 3; corner++)
			{
				unsigned short vertex = part->triangles[3 * triangle + corner];
				real_point3d const *position;

				if (vertex >= part->vertex_count)
					break;
				position = &part->vertices[vertex].position;
				corners[corner][0] = position->x;
				corners[corner][1] = position->y;
				corners[corner][2] = position->z;
			}
			if (corner < 3)
				continue;
			area = (corners[1][first] - corners[0][first]) * (corners[2][second] - corners[0][second]) -
				(corners[2][first] - corners[0][first]) * (corners[1][second] - corners[0][second]);
			if (area > -1e-12f && area < 1e-12f)
				continue;
			weights[1] = ((point[first] - corners[0][first]) * (corners[2][second] - corners[0][second]) -
				(corners[2][first] - corners[0][first]) * (point[second] - corners[0][second])) / area;
			weights[2] = ((corners[1][first] - corners[0][first]) * (point[second] - corners[0][second]) -
				(point[first] - corners[0][first]) * (corners[1][second] - corners[0][second])) / area;
			weights[0] = 1.f - weights[1] - weights[2];
			if (weights[0] < -0.001f || weights[1] < -0.001f || weights[2] < -0.001f)
				continue;
			value = weights[0] * corners[0][normal] + weights[1] * corners[1][normal] + weights[2] * corners[2][normal];
			if (!found || value * outward > *height * outward)
				*height = value;
			found = TRUE;
		}
	}

	return found;
}

/* how many parts of the template's most detailed level are its gauge */
static long gauge_part_count(
	struct saber_template const *template_)
{
	long count = 0;
	long part_index;

	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part const *part = &template_->parts[part_index];

		if (part->lod == 0 && part_gauge(template_, part) && part->vertex_count > 0)
		{
			count++;
		}
	}

	return count;
}

/* the template's gauge, in the model's space (its vertices already are).
The whole of it when `metre` is NULL; else the side the metre sits on (the
gauge is drawn on both sides of the gun). FALSE when it has none */
static boolean gauge_box(
	struct saber_template const *template_,
	real_point3d const *metre,
	real_point3d *low,
	real_point3d *high)
{
	real_point3d box_low;
	real_point3d box_high;
	real mid_x;
	real mid_y;
	real mid_z;
	real toward_x;
	real toward_y;
	real toward_z;
	boolean any = FALSE;
	boolean found = FALSE;
	long part_index;
	long index;

	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part const *part = &template_->parts[part_index];

		if (!part_gauge(template_, part) || part->vertex_count <= 0)
		{
			continue;
		}
		for (index = 0; index < part->vertex_count; index++)
		{
			real_point3d const *position = &part->vertices[index].position;

			if (!any)
			{
				box_low = box_high = *position;
				any = TRUE;
			}
			box_low.x = MIN(box_low.x, position->x);
			box_low.y = MIN(box_low.y, position->y);
			box_low.z = MIN(box_low.z, position->z);
			box_high.x = MAX(box_high.x, position->x);
			box_high.y = MAX(box_high.y, position->y);
			box_high.z = MAX(box_high.z, position->z);
		}
	}
	if (!any)
		return FALSE;
	if (!metre)
	{
		*low = box_low;
		*high = box_high;
		return TRUE;
	}
	mid_x = (box_low.x + box_high.x) * 0.5f;
	mid_y = (box_low.y + box_high.y) * 0.5f;
	mid_z = (box_low.z + box_high.z) * 0.5f;
	toward_x = metre->x - mid_x;
	toward_y = metre->y - mid_y;
	toward_z = metre->z - mid_z;
	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part const *part = &template_->parts[part_index];

		if (!part_gauge(template_, part) || part->vertex_count <= 0)
		{
			continue;
		}
		for (index = 0; index < part->vertex_count; index++)
		{
			real_point3d const *position = &part->vertices[index].position;
			real along = (position->x - mid_x) * toward_x + (position->y - mid_y) * toward_y +
				(position->z - mid_z) * toward_z;

			/* (the gauge on the other side of the gun) */
			if (along < 0.f)
				continue;
			if (!found)
			{
				*low = *high = *position;
				found = TRUE;
			}
			low->x = MIN(low->x, position->x);
			low->y = MIN(low->y, position->y);
			low->z = MIN(low->z, position->z);
			high->x = MAX(high->x, position->x);
			high->y = MAX(high->y, position->y);
			high->z = MAX(high->z, position->z);
		}
	}
	if (!found)
	{
		*low = box_low;
		*high = box_high;
	}

	return TRUE;
}

/* the plasma pistol's ring: a flat round face, tilted back and up, at the
end of parts that run along the gun's top too. Its plane is that of its
gauge triangles facing the gun's back; its frame, `normal` out of it,
`across` the gun's width in it and `up` the third; its middle and its
size in that frame. The classic meter is a ring too, tilted alike: its
plane is the one its vertices spread least across */
static void ring_cross(
	real const *a,
	real const *b,
	real *result)
{
	result[0] = a[1] * b[2] - a[2] * b[1];
	result[1] = a[2] * b[0] - a[0] * b[2];
	result[2] = a[0] * b[1] - a[1] * b[0];
}

static real ring_dot(
	real const *a,
	real const *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static boolean ring_unit(
	real *a)
{
	real length = (real)sqrt(ring_dot(a, a));

	if (length <= 1e-9f)
		return FALSE;
	a[0] /= length;
	a[1] /= length;
	a[2] /= length;

	return TRUE;
}

/* the frame of a plane: `across` the gun's width (y) laid in it, and `up`
across times normal's turn, so up, across and normal turn as x, y, z */
static boolean ring_frame(
	real const *normal,
	real *across,
	real *up)
{
	real along;

	across[0] = 0.f;
	across[1] = 1.f;
	across[2] = 0.f;
	along = ring_dot(across, normal);
	across[0] -= normal[0] * along;
	across[1] -= normal[1] * along;
	across[2] -= normal[2] * along;
	if (!ring_unit(across))
		return FALSE;
	ring_cross(across, normal, up);

	return ring_unit(up);
}

/* the eigenvector of the least eigenvalue of a symmetric 3x3 matrix
(Jacobi's turns) */
static void ring_least_axis(
	real matrix[3][3],
	real *axis)
{
	double a[3][3];
	double v[3][3];
	long sweep;
	long row;
	long column;
	long least = 0;

	for (row = 0; row < 3; row++)
	{
		for (column = 0; column < 3; column++)
		{
			a[row][column] = matrix[row][column];
			v[row][column] = row == column ? 1.0 : 0.0;
		}
	}
	for (sweep = 0; sweep < 32; sweep++)
	{
		long p;
		long q;

		for (p = 0; p < 2; p++)
		{
			for (q = p + 1; q < 3; q++)
			{
				double theta;
				double t;
				double c;
				double s;
				long k;

				if (a[p][q] > -1e-18 && a[p][q] < 1e-18)
					continue;
				theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
				t = (theta >= 0.0 ? 1.0 : -1.0) / ((theta >= 0.0 ? theta : -theta) + sqrt(theta * theta + 1.0));
				c = 1.0 / sqrt(t * t + 1.0);
				s = t * c;
				for (k = 0; k < 3; k++)
				{
					double kp = a[k][p];
					double kq = a[k][q];

					a[k][p] = c * kp - s * kq;
					a[k][q] = s * kp + c * kq;
				}
				for (k = 0; k < 3; k++)
				{
					double pk = a[p][k];
					double qk = a[q][k];

					a[p][k] = c * pk - s * qk;
					a[q][k] = s * pk + c * qk;
				}
				for (k = 0; k < 3; k++)
				{
					double kp = v[k][p];
					double kq = v[k][q];

					v[k][p] = c * kp - s * kq;
					v[k][q] = s * kp + c * kq;
				}
			}
		}
	}
	for (row = 1; row < 3; row++)
	{
		if (a[row][row] < a[least][least])
			least = row;
	}
	axis[0] = (real)v[0][least];
	axis[1] = (real)v[1][least];
	axis[2] = (real)v[2][least];
}

/* the classic meter's vertices (`copy`, `count` of `stride`) laid on the
ring, its shape kept, as large as the ring, its face the ring's, just
clear of it. FALSE when the ring is not found. With no `copy`, only the
ring's triangles of `ring_part` marked in `ring_triangles` */
static boolean ring_meter_fit(
	struct saber_template const *template_,
	byte *copy,
	long stride,
	long count,
	real_point3d *placed,
	struct saber_mesh_part const *ring_part,
	unsigned char *ring_triangles)
{
	real gun_low[3];
	real gun_high[3];
	real gun_mid[3];
	real ring_normal[3] = { 0.f, 0.f, 0.f };
	real ring_across[3];
	real ring_up[3];
	real ring_low[3];
	real ring_high[3];
	real ring_mid[3];
	real meter_normal[3];
	real meter_across[3];
	real meter_up[3];
	real meter_low[3];
	real meter_high[3];
	real meter_mid[3];
	real covariance[3][3];
	real offset = 0.f;
	real weight = 0.f;
	real scale;
	real clear;
	real side;
	real full_low_x = 0.f;
	real full_high_x = 0.f;
	boolean any_gun = FALSE;
	boolean any_gauge = FALSE;
	boolean any_ring = FALSE;
	long pass;
	long part_index;
	long index;
	long axis;

	/* (the gun's middle, and which end of the gauge is its back) */
	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part const *part = &template_->parts[part_index];
		long vertex;

		if (part->lod != 0)
			continue;
		for (vertex = 0; vertex < part->vertex_count; vertex++)
		{
			real_point3d const *position = &part->vertices[vertex].position;
			real coordinates[3];

			coordinates[0] = position->x;
			coordinates[1] = position->y;
			coordinates[2] = position->z;
			for (axis = 0; axis < 3; axis++)
			{
				if (!any_gun || coordinates[axis] < gun_low[axis])
					gun_low[axis] = coordinates[axis];
				if (!any_gun || coordinates[axis] > gun_high[axis])
					gun_high[axis] = coordinates[axis];
			}
			any_gun = TRUE;
			if (part_gauge(template_, part))
			{
				if (!any_gauge || position->x < full_low_x)
					full_low_x = position->x;
				if (!any_gauge || position->x > full_high_x)
					full_high_x = position->x;
				any_gauge = TRUE;
			}
		}
	}
	if (!any_gun || !any_gauge || (copy && count <= 2))
		return FALSE;
	for (axis = 0; axis < 3; axis++)
		gun_mid[axis] = (gun_low[axis] + gun_high[axis]) * 0.5f;
	side = (full_low_x + full_high_x) * 0.5f >= gun_mid[0] ? 1.f : -1.f;
	/* (the ring's plane: first the gauge triangles facing back at all,
	then those facing as they do on the whole, then those of them in its
	plane, the strongest there, by area) */
	for (pass = 0; pass < 3; pass++)
	{
		real sum[3] = { 0.f, 0.f, 0.f };
		real offset_sum = 0.f;
		real area_sum = 0.f;

		for (part_index = 0; part_index < template_->part_count; part_index++)
		{
			struct saber_mesh_part const *part = &template_->parts[part_index];
			long triangle;

			if (part->lod != 0 || !part_gauge(template_, part))
				continue;
			for (triangle = 0; triangle < part->triangle_count; triangle++)
			{
				real corners[3][3];
				real edge_a[3];
				real edge_b[3];
				real facing[3];
				real area;
				real centre;
				long corner;

				for (corner = 0; corner < 3; corner++)
				{
					long vertex = part->triangles[triangle * 3 + corner];
					real_point3d const *position;

					if (vertex >= part->vertex_count)
						break;
					position = &part->vertices[vertex].position;
					corners[corner][0] = position->x;
					corners[corner][1] = position->y;
					corners[corner][2] = position->z;
				}
				if (corner < 3)
					continue;
				for (axis = 0; axis < 3; axis++)
				{
					edge_a[axis] = corners[1][axis] - corners[0][axis];
					edge_b[axis] = corners[2][axis] - corners[0][axis];
				}
				ring_cross(edge_a, edge_b, facing);
				area = (real)sqrt(ring_dot(facing, facing));
				if (area <= 1e-12f)
					continue;
				facing[0] /= area;
				facing[1] /= area;
				facing[2] /= area;
				if (facing[0] * side < 0.f)
				{
					facing[0] = -facing[0];
					facing[1] = -facing[1];
					facing[2] = -facing[2];
				}
				if (pass == 0 ? facing[0] * side < 0.4f : ring_dot(facing, ring_normal) < 0.9f)
					continue;
				centre = (ring_dot(corners[0], facing) + ring_dot(corners[1], facing) +
					ring_dot(corners[2], facing)) / 3.f;
				if (pass == 2)
				{
					real away = (ring_dot(corners[0], ring_normal) + ring_dot(corners[1], ring_normal) +
						ring_dot(corners[2], ring_normal)) / 3.f - offset;

					if (away < 0.f)
						away = -away;
					if (away > (gun_high[0] - gun_low[0]) * 0.02f)
						continue;
				}
				if (pass == 2 && part == ring_part && ring_triangles)
					ring_triangles[triangle] = 1;
				for (axis = 0; axis < 3; axis++)
					sum[axis] += facing[axis] * area;
				offset_sum += centre * area;
				area_sum += area;
				if (pass == 2)
				{
					for (corner = 0; corner < 3; corner++)
					{
						for (axis = 0; axis < 3; axis++)
						{
							real along = axis == 0 ? ring_dot(corners[corner], ring_up) :
								(axis == 1 ? ring_dot(corners[corner], ring_across) : ring_dot(corners[corner], ring_normal));

							if (!any_ring || along < ring_low[axis])
								ring_low[axis] = along;
							if (!any_ring || along > ring_high[axis])
								ring_high[axis] = along;
						}
						any_ring = TRUE;
					}
				}
			}
		}
		if (area_sum <= 1e-12f)
			return FALSE;
		if (pass < 2)
		{
			ring_normal[0] = sum[0];
			ring_normal[1] = sum[1];
			ring_normal[2] = sum[2];
			if (!ring_unit(ring_normal) || !ring_frame(ring_normal, ring_across, ring_up))
				return FALSE;
			offset = offset_sum / area_sum;
			weight = area_sum;
		}
	}
	if (!any_ring || weight <= 0.f)
		return FALSE;
	if (!copy)
	{
		/* (and the ring's rim: the part's triangles within its round, from
		its face to a quarter of its width behind it) */
		real width = ring_high[1] - ring_low[1];

		if (ring_part && ring_triangles && width > 0.f)
		{
			long triangle;

			for (triangle = 0; triangle < ring_part->triangle_count; triangle++)
			{
				long corner;

				for (corner = 0; corner < 3; corner++)
				{
					long vertex = ring_part->triangles[triangle * 3 + corner];
					real point[3];
					real along[3];

					if (vertex >= ring_part->vertex_count)
						break;
					point[0] = ring_part->vertices[vertex].position.x;
					point[1] = ring_part->vertices[vertex].position.y;
					point[2] = ring_part->vertices[vertex].position.z;
					along[0] = ring_dot(point, ring_up);
					along[1] = ring_dot(point, ring_across);
					along[2] = ring_dot(point, ring_normal);
					if (along[0] < ring_low[0] - width * 0.03f || along[0] > ring_high[0] + width * 0.03f ||
						along[1] < ring_low[1] - width * 0.03f || along[1] > ring_high[1] + width * 0.03f ||
						along[2] < ring_low[2] - width * 0.25f || along[2] > ring_high[2] + width * 0.03f)
					{
						break;
					}
				}
				if (corner == 3)
					ring_triangles[triangle] = 1;
			}
		}

		return TRUE;
	}
	/* (the classic meter's plane, its face away from the gun's middle) */
	for (index = 0; index < 3; index++)
		meter_mid[index] = 0.f;
	for (index = 0; index < count; index++)
	{
		real_point3d const *position = (real_point3d const *)(copy + index * stride);

		meter_mid[0] += position->x;
		meter_mid[1] += position->y;
		meter_mid[2] += position->z;
	}
	for (axis = 0; axis < 3; axis++)
	{
		long other;

		meter_mid[axis] /= (real)count;
		for (other = 0; other < 3; other++)
			covariance[axis][other] = 0.f;
	}
	for (index = 0; index < count; index++)
	{
		real_point3d const *position = (real_point3d const *)(copy + index * stride);
		real away[3];
		long other;

		away[0] = position->x - meter_mid[0];
		away[1] = position->y - meter_mid[1];
		away[2] = position->z - meter_mid[2];
		for (axis = 0; axis < 3; axis++)
		{
			for (other = 0; other < 3; other++)
				covariance[axis][other] += away[axis] * away[other];
		}
	}
	ring_least_axis(covariance, meter_normal);
	{
		real outward[3];

		for (axis = 0; axis < 3; axis++)
			outward[axis] = meter_mid[axis] - gun_mid[axis];
		if (ring_dot(meter_normal, outward) < 0.f)
		{
			meter_normal[0] = -meter_normal[0];
			meter_normal[1] = -meter_normal[1];
			meter_normal[2] = -meter_normal[2];
		}
	}
	if (!ring_unit(meter_normal) || !ring_frame(meter_normal, meter_across, meter_up))
		return FALSE;
	for (index = 0; index < count; index++)
	{
		real_point3d const *position = (real_point3d const *)(copy + index * stride);
		real point[3];

		point[0] = position->x;
		point[1] = position->y;
		point[2] = position->z;
		for (axis = 0; axis < 3; axis++)
		{
			real along = axis == 0 ? ring_dot(point, meter_up) :
				(axis == 1 ? ring_dot(point, meter_across) : ring_dot(point, meter_normal));

			if (index == 0 || along < meter_low[axis])
				meter_low[axis] = along;
			if (index == 0 || along > meter_high[axis])
				meter_high[axis] = along;
		}
	}
	scale = 0.f;
	for (axis = 0; axis < 2; axis++)
	{
		real meter_span = meter_high[axis] - meter_low[axis];
		real ratio;

		if (meter_span <= 1e-6f)
			continue;
		ratio = (ring_high[axis] - ring_low[axis]) / meter_span;
		if (scale == 0.f || ratio < scale)
			scale = ratio;
	}
	if (scale <= 0.f)
		return FALSE;
	for (axis = 0; axis < 2; axis++)
	{
		ring_mid[axis] = (ring_low[axis] + ring_high[axis]) * 0.5f;
		meter_mid[axis] = (meter_low[axis] + meter_high[axis]) * 0.5f;
	}
	clear = (ring_high[1] - ring_low[1]) * 0.03f;
	ring_mid[2] = ring_high[2] + clear;
	/* (each vertex: its place in the meter's frame, scaled, to the same
	place in the ring's; its depth from the meter's back on the ring's
	face) */
	for (index = 0; index < count; index++)
	{
		real_point3d *position = (real_point3d *)(copy + index * stride);
		real point[3];
		real frame[3];

		point[0] = position->x;
		point[1] = position->y;
		point[2] = position->z;
		frame[0] = ring_mid[0] + (ring_dot(point, meter_up) - meter_mid[0]) * scale;
		frame[1] = ring_mid[1] + (ring_dot(point, meter_across) - meter_mid[1]) * scale;
		frame[2] = ring_mid[2] + (ring_dot(point, meter_normal) - meter_low[2]) * scale;
		position->x = ring_up[0] * frame[0] + ring_across[0] * frame[1] + ring_normal[0] * frame[2];
		position->y = ring_up[1] * frame[0] + ring_across[1] * frame[1] + ring_normal[1] * frame[2];
		position->z = ring_up[2] * frame[0] + ring_across[2] * frame[1] + ring_normal[2] * frame[2];
	}
	placed->x = ring_up[0] * ring_mid[0] + ring_across[0] * ring_mid[1] + ring_normal[0] * ring_mid[2];
	placed->y = ring_up[1] * ring_mid[0] + ring_across[1] * ring_mid[1] + ring_normal[1] * ring_mid[2];
	placed->z = ring_up[2] * ring_mid[0] + ring_across[2] * ring_mid[1] + ring_normal[2] * ring_mid[2];
	platform_log("anniversary: the heat meter laid on the ring facing %.2f %.2f %.2f (its own %.2f %.2f %.2f), %.3f across, at %.3f %.3f %.3f, %.2f of its size",
		ring_normal[0], ring_normal[1], ring_normal[2], meter_normal[0], meter_normal[1], meter_normal[2],
		ring_high[1] - ring_low[1], placed->x, placed->y, placed->z, scale);

	return TRUE;
}

/* a copy of the classic meter's vertices, shrunk and moved so its centre
sits on the outer face of the gauge on its side of the gun. The map's
buffer is not changed. `placed` is that centre */
static boolean meter_buffer_fit(
	struct model_geometry_part const *source,
	struct saber_template const *template_,
	struct vertex_buffer *fitted,
	real_point3d *placed)
{
	real_point3d glow;
	real_point3d ghigh;
	real_point3d full_low;
	real_point3d full_high;
	real_point3d mlow;
	real_point3d mhigh;
	real_point3d metre;
	real factor = 1.f;
	real centre_x;
	real centre_y;
	real centre_z;
	real place_x;
	real place_y;
	real place_z;
	real mid_x;
	real mid_y;
	real mid_z;
	real out_x;
	real out_y;
	real out_z;
	real out_len;
	real shell_half;
	real meter_half;
	real push;
	real span;
	real extent;
	real gextent;
	long stride;
	long index;
	byte *locked = NULL;
	byte *copy = NULL;
	struct vertex_buffer const *buffer = &source->vertex_buffer;
	boolean fitted_ok = FALSE;
	/* (a gauge of one side only, the plasma rifle's: the meter laid over
	it box to box, its glyph on the gauge's) */
	boolean one_sided = FALSE;
	/* (and of two panes, the plasma rifle's either side of its ridge, the
	meter on its side's; else one face, the plasma pistol's ring) */
	boolean paned = FALSE;
	/* (the middle of the gun, of its most detailed level) */
	real gun_mid[3] = { 0.f, 0.f, 0.f };

	if (!buffer->hardware_format || buffer->count <= 0)
		return FALSE;
	if (buffer->type == _rasterizer_vertex_type_model_compressed)
		stride = (long)sizeof(struct model_vertex_compressed);
	else if (buffer->type == _rasterizer_vertex_type_model_uncompressed)
		stride = (long)sizeof(struct model_vertex_uncompressed);
	else
		return FALSE;
	IDirect3DVertexBuffer8_Lock((IDirect3DVertexBuffer8 *)buffer->hardware_format, 0, 0, &locked,
		D3DLOCK_READONLY);
	if (!locked)
		return FALSE;
	mlow = *(real_point3d const *)locked;
	mhigh = mlow;
	for (index = 0; index < buffer->count; index++)
	{
		real_point3d const *position = (real_point3d const *)(locked + index * stride);

		mlow.x = MIN(mlow.x, position->x);
		mlow.y = MIN(mlow.y, position->y);
		mlow.z = MIN(mlow.z, position->z);
		mhigh.x = MAX(mhigh.x, position->x);
		mhigh.y = MAX(mhigh.y, position->y);
		mhigh.z = MAX(mhigh.z, position->z);
	}
	metre.x = (mlow.x + mhigh.x) * 0.5f;
	metre.y = (mlow.y + mhigh.y) * 0.5f;
	metre.z = (mlow.z + mhigh.z) * 0.5f;
	if (!gauge_box(template_, &metre, &glow, &ghigh))
	{
		IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)buffer->hardware_format);
		return FALSE;
	}
	extent = mhigh.x - mlow.x;
	gextent = ghigh.x - glow.x;
	if (extent > 0.0001f && gextent > 0.0001f)
		factor = MIN(factor, gextent / extent);
	extent = mhigh.y - mlow.y;
	gextent = ghigh.y - glow.y;
	if (extent > 0.0001f && gextent > 0.0001f)
		factor = MIN(factor, gextent / extent);
	extent = mhigh.z - mlow.z;
	gextent = ghigh.z - glow.z;
	if (extent > 0.0001f && gextent > 0.0001f)
		factor = MIN(factor, gextent / extent);
	if (factor > 1.f)
		factor = 1.f;
	copy = malloc((size_t)stride * (size_t)buffer->count);
	if (!copy)
	{
		IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)buffer->hardware_format);
		return FALSE;
	}
	memcpy(copy, locked, (size_t)stride * (size_t)buffer->count);
	IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)buffer->hardware_format);
	locked = NULL;
	/* (the plasma pistol's: the meter laid on its ring, of the ring's
	plane, not its box) */
	if (template_gauge_stand_in(template_) && ring_meter_fit(template_, copy, stride, buffer->count, placed, NULL, NULL))
	{
		fitted_ok = rasterizer_vertex_buffer_new(fitted, buffer->type, buffer->count, copy,
			stride * buffer->count) ? TRUE : FALSE;
		free(copy);

		return fitted_ok;
	}
	centre_x = metre.x;
	centre_y = metre.y;
	centre_z = metre.z;
	place_x = (glow.x + ghigh.x) * 0.5f;
	place_y = (glow.y + ghigh.y) * 0.5f;
	place_z = (glow.z + ghigh.z) * 0.5f;
	/* the shell is drawn opaque. A meter centred in it fails the depth
	test, and the window stays the static gauge. The fill is the classic
	meter, so it has to sit on the outer face, just clear of the shell */
	push = 0.f;
	if (gauge_box(template_, NULL, &full_low, &full_high))
	{
		mid_x = (full_low.x + full_high.x) * 0.5f;
		mid_y = (full_low.y + full_high.y) * 0.5f;
		mid_z = (full_low.z + full_high.z) * 0.5f;
		out_x = place_x - mid_x;
		out_y = place_y - mid_y;
		out_z = place_z - mid_z;
		out_len = square_root(out_x * out_x + out_y * out_y + out_z * out_z);
		/* (one gauge, across the top: the whole of it is the meter's, not a
		side of it. The classic meter sits to one side of the gun's middle,
		which halved the box it was fitted to) */
		/* (or the plasma pistol's ring, of several parts one over another) */
		if (gauge_part_count(template_) == 1 || template_gauge_stand_in(template_))
		{
			/* (its two panes either side of the top's ridge, one part: the
			meter is the pane's on its side of the gun, across the axis the
			meter is thinnest along, facing out along it) */
			real meter_extent[3];
			real meter_mid[3];
			real full_mid[3];
			real side_low[3];
			real side_high[3];
			real side;
			long normal = 0;
			long axis;
			boolean any_side = FALSE;
			long part_index;

			meter_extent[0] = mhigh.x - mlow.x;
			meter_extent[1] = mhigh.y - mlow.y;
			meter_extent[2] = mhigh.z - mlow.z;
			meter_mid[0] = metre.x;
			meter_mid[1] = metre.y;
			meter_mid[2] = metre.z;
			full_mid[0] = mid_x;
			full_mid[1] = mid_y;
			full_mid[2] = mid_z;
			for (axis = 1; axis < 3; axis++)
			{
				if (meter_extent[axis] < meter_extent[normal])
					normal = axis;
			}
			side = meter_mid[normal] >= full_mid[normal] ? 1.f : -1.f;
			/* (the gun's middle, and whether the gauge has a gap down its
			middle across the meter's face: two panes) */
			{
				real gun_low[3];
				real gun_high[3];
				real full_extent = 0.f;
				boolean any_gun = FALSE;

				paned = TRUE;
				for (part_index = 0; part_index < template_->part_count; part_index++)
				{
					struct saber_mesh_part const *part = &template_->parts[part_index];
					long vertex;

					if (part->lod != 0)
						continue;
					for (vertex = 0; vertex < part->vertex_count; vertex++)
					{
						real_point3d const *position = &part->vertices[vertex].position;
						real coordinates[3];

						coordinates[0] = position->x;
						coordinates[1] = position->y;
						coordinates[2] = position->z;
						for (axis = 0; axis < 3; axis++)
						{
							if (!any_gun || coordinates[axis] < gun_low[axis])
								gun_low[axis] = coordinates[axis];
							if (!any_gun || coordinates[axis] > gun_high[axis])
								gun_high[axis] = coordinates[axis];
						}
						any_gun = TRUE;
					}
				}
				for (axis = 0; any_gun && axis < 3; axis++)
					gun_mid[axis] = (gun_low[axis] + gun_high[axis]) * 0.5f;
				full_extent = normal == 0 ? full_high.x - full_low.x :
					(normal == 1 ? full_high.y - full_low.y : full_high.z - full_low.z);
				for (part_index = 0; paned && part_index < template_->part_count; part_index++)
				{
					struct saber_mesh_part const *part = &template_->parts[part_index];
					long vertex;

					if (part->lod != 0 || !part_gauge(template_, part))
						continue;
					for (vertex = 0; vertex < part->vertex_count; vertex++)
					{
						real_point3d const *position = &part->vertices[vertex].position;
						real value = normal == 0 ? position->x : (normal == 1 ? position->y : position->z);

						if (value - full_mid[normal] < full_extent * 0.1f &&
							full_mid[normal] - value < full_extent * 0.1f)
						{
							paned = FALSE;
							break;
						}
					}
				}
			}
			for (part_index = 0; paned && part_index < template_->part_count; part_index++)
			{
				struct saber_mesh_part const *part = &template_->parts[part_index];
				long vertex;

				if (part->lod != 0 || !part_gauge(template_, part))
				{
					continue;
				}
				for (vertex = 0; vertex < part->vertex_count; vertex++)
				{
					real_point3d const *position = &part->vertices[vertex].position;
					real coordinates[3];

					coordinates[0] = position->x;
					coordinates[1] = position->y;
					coordinates[2] = position->z;
					if ((coordinates[normal] - full_mid[normal]) * side <= 0.f)
						continue;
					for (axis = 0; axis < 3; axis++)
					{
						if (!any_side || coordinates[axis] < side_low[axis])
							side_low[axis] = coordinates[axis];
						if (!any_side || coordinates[axis] > side_high[axis])
							side_high[axis] = coordinates[axis];
					}
					any_side = TRUE;
				}
			}
			if (!paned)
			{
				/* (one face: the whole of it, facing out along its thinnest
				axis, away from the gun's middle) */
				real extents[3];
				long thinnest = 0;

				glow = full_low;
				ghigh = full_high;
				extents[0] = full_high.x - full_low.x;
				extents[1] = full_high.y - full_low.y;
				extents[2] = full_high.z - full_low.z;
				for (axis = 1; axis < 3; axis++)
				{
					if (extents[axis] < extents[thinnest])
						thinnest = axis;
				}
				side = full_mid[thinnest] >= gun_mid[thinnest] ? 1.f : -1.f;
				/* (the plasma pistol's ring is the back of parts that run along
				the top too: its face the back, along the gun's length, and its
				box the parts' backmost seventh) */
				if (template_gauge_stand_in(template_))
				{
					real back;
					real reach;
					boolean any_back = FALSE;

					thinnest = 0;
					side = full_mid[0] >= gun_mid[0] ? 1.f : -1.f;
					back = side > 0.f ? full_high.x : full_low.x;
					reach = extents[0] / 7.f;
					for (part_index = 0; part_index < template_->part_count; part_index++)
					{
						struct saber_mesh_part const *part = &template_->parts[part_index];
						long vertex;

						if (part->lod != 0 || !part_gauge(template_, part))
							continue;
						for (vertex = 0; vertex < part->vertex_count; vertex++)
						{
							real_point3d const *position = &part->vertices[vertex].position;
							real coordinates[3];

							if ((back - position->x) * side > reach)
								continue;
							coordinates[0] = position->x;
							coordinates[1] = position->y;
							coordinates[2] = position->z;
							for (axis = 0; axis < 3; axis++)
							{
								if (!any_back || coordinates[axis] < side_low[axis])
									side_low[axis] = coordinates[axis];
								if (!any_back || coordinates[axis] > side_high[axis])
									side_high[axis] = coordinates[axis];
							}
							any_back = TRUE;
						}
					}
					if (any_back)
					{
						glow.x = side_low[0];
						glow.y = side_low[1];
						glow.z = side_low[2];
						ghigh.x = side_high[0];
						ghigh.y = side_high[1];
						ghigh.z = side_high[2];
					}
				}
				out_x = thinnest == 0 ? side : 0.f;
				out_y = thinnest == 1 ? side : 0.f;
				out_z = thinnest == 2 ? side : 0.f;
				one_sided = TRUE;
				out_len = 1.f;
			}
			else if (any_side)
			{
				glow.x = side_low[0];
				glow.y = side_low[1];
				glow.z = side_low[2];
				ghigh.x = side_high[0];
				ghigh.y = side_high[1];
				ghigh.z = side_high[2];
				out_x = normal == 0 ? side : 0.f;
				out_y = normal == 1 ? side : 0.f;
				out_z = normal == 2 ? side : 0.f;
				one_sided = TRUE;
				out_len = 1.f;
			}
			else
			{
				glow = full_low;
				ghigh = full_high;
				out_len = 0.f;
			}
		}
		/* (a gauge of one side only, the plasma rifle's on its top: its
		side's box is the whole, so no way out of it. Out along its thinnest
		axis, away from the middle of the gun) */
		if (out_len <= 0.0001f)
		{
			real_point3d gun_low;
			real_point3d gun_high;
			real thin_x = full_high.x - full_low.x;
			real thin_y = full_high.y - full_low.y;
			real thin_z = full_high.z - full_low.z;
			boolean any_vertex = FALSE;
			long part_index;

			for (part_index = 0; part_index < template_->part_count; part_index++)
			{
				struct saber_mesh_part const *part = &template_->parts[part_index];
				long vertex;

				for (vertex = 0; vertex < part->vertex_count; vertex++)
				{
					real_point3d const *position = &part->vertices[vertex].position;

					if (!any_vertex)
					{
						gun_low = *position;
						gun_high = *position;
						any_vertex = TRUE;
					}
					gun_low.x = MIN(gun_low.x, position->x);
					gun_low.y = MIN(gun_low.y, position->y);
					gun_low.z = MIN(gun_low.z, position->z);
					gun_high.x = MAX(gun_high.x, position->x);
					gun_high.y = MAX(gun_high.y, position->y);
					gun_high.z = MAX(gun_high.z, position->z);
				}
			}
			if (any_vertex)
			{
				one_sided = TRUE;
				out_x = out_y = out_z = 0.f;
				if (thin_x <= thin_y && thin_x <= thin_z)
					out_x = place_x >= (gun_low.x + gun_high.x) * 0.5f ? 1.f : -1.f;
				else if (thin_y <= thin_z)
					out_y = place_y >= (gun_low.y + gun_high.y) * 0.5f ? 1.f : -1.f;
				else
					out_z = place_z >= (gun_low.z + gun_high.z) * 0.5f ? 1.f : -1.f;
				out_len = 1.f;
			}
		}
		if (out_len > 0.0001f)
		{
			out_x /= out_len;
			out_y /= out_len;
			out_z /= out_len;
			span = (ghigh.x - glow.x) * out_x;
			if (span < 0.f)
				span = -span;
			shell_half = span;
			span = (ghigh.y - glow.y) * out_y;
			if (span < 0.f)
				span = -span;
			shell_half += span;
			span = (ghigh.z - glow.z) * out_z;
			if (span < 0.f)
				span = -span;
			shell_half = (shell_half + span) * 0.5f;
			span = (mhigh.x - mlow.x) * out_x;
			if (span < 0.f)
				span = -span;
			meter_half = span;
			span = (mhigh.y - mlow.y) * out_y;
			if (span < 0.f)
				span = -span;
			meter_half += span;
			span = (mhigh.z - mlow.z) * out_z;
			if (span < 0.f)
				span = -span;
			meter_half = (meter_half + span) * factor * 0.5f;
			span = ghigh.x - glow.x;
			if (span < 0.f)
				span = -span;
			push = span;
			span = ghigh.y - glow.y;
			if (span < 0.f)
				span = -span;
			if (span > push)
				push = span;
			span = ghigh.z - glow.z;
			if (span < 0.f)
				span = -span;
			if (span > push)
				push = span;
			push = shell_half + meter_half + push * 0.02f;
			place_x += out_x * push;
			place_y += out_y * push;
			place_z += out_z * push;
		}
	}
	for (index = 0; index < buffer->count; index++)
	{
		real_point3d *position = (real_point3d *)(copy + index * stride);

		if (one_sided)
		{
			/* (the meter's box onto the gauge's. The classic meter lies on the
			gun's side, facing out of it; the gauge on its top: the meter is
			turned a quarter about the axis they share, its face to the
			gauge's, then laid box to box and set out of the face a fiftieth
			of its depth, clear of it) */
			real meter_low[3];
			real meter_high[3];
			real gauge_low[3];
			real gauge_high[3];
			real gauge_mid[3];
			real point[3];
			real out[3];
			real result[3];
			long gauge_normal = out_x != 0.f ? 0 : (out_y != 0.f ? 1 : 2);
			long meter_normal = 0;
			long sources[3];
			real signs[3];
			long axis;

			meter_low[0] = mlow.x; meter_low[1] = mlow.y; meter_low[2] = mlow.z;
			meter_high[0] = mhigh.x; meter_high[1] = mhigh.y; meter_high[2] = mhigh.z;
			gauge_low[0] = glow.x; gauge_low[1] = glow.y; gauge_low[2] = glow.z;
			gauge_high[0] = ghigh.x; gauge_high[1] = ghigh.y; gauge_high[2] = ghigh.z;
			gauge_mid[0] = mid_x; gauge_mid[1] = mid_y; gauge_mid[2] = mid_z;
			point[0] = position->x; point[1] = position->y; point[2] = position->z;
			out[0] = out_x; out[1] = out_y; out[2] = out_z;
			for (axis = 0; axis < 3; axis++)
			{
				sources[axis] = axis;
				signs[axis] = 1.f;
			}
			for (axis = 1; axis < 3; axis++)
			{
				if (meter_high[axis] - meter_low[axis] < meter_high[meter_normal] - meter_low[meter_normal])
					meter_normal = axis;
			}
			if (meter_normal != gauge_normal)
			{
				/* (the meter's outward face, of its side of the gauge's middle,
				turned to the gauge's; the other turned axis so the turn is a
				turn, not a mirror) */
				real meter_out = (meter_low[meter_normal] + meter_high[meter_normal]) * 0.5f >=
					(paned ? gauge_mid[meter_normal] : gun_mid[meter_normal]) ? 1.f : -1.f;
				real turn = meter_out * (out[gauge_normal] >= 0.f ? 1.f : -1.f);

				sources[gauge_normal] = meter_normal;
				signs[gauge_normal] = turn;
				sources[meter_normal] = gauge_normal;
				signs[meter_normal] = -turn;
			}
			for (axis = 0; axis < 3; axis++)
			{
				long source_axis = sources[axis];
				real span = meter_high[source_axis] - meter_low[source_axis];
				real along = span > 0.0001f ? (point[source_axis] - meter_low[source_axis]) / span : 0.5f;

				if (signs[axis] < 0.f)
					along = 1.f - along;
				/* (the classic meter's glyph is not the whole of its box, as the
				gauge's is of the pane's: its box reaches past the glyph's end
				away from the prongs, and a little past its edges. Its box laid
				over a little more than the pane's lays the glyph on the glyph:
				along the gun, from a fifth before the pane's low end, the blob's,
				to a thirtieth past its high, the prongs'; across it, a twentieth
				past each edge) */
				if (axis != gauge_normal && paned)
				{
					long longest = axis;
					long other;

					for (other = 0; other < 3; other++)
					{
						if (other != gauge_normal &&
							gauge_high[other] - gauge_low[other] > gauge_high[longest] - gauge_low[longest])
						{
							longest = other;
						}
					}
					/* (the prongs kept where the fit above lays them, a thirtieth
					past the pane's high end; the glyph's other end, its top, on
					the remastered glyph's: three tenths of the pane past the low
					end, where its blob's ovals reach) */
					if (axis == longest)
						along = -0.547f + along * 1.587f;
					else
						along = -0.05f + along * 1.10f;
					result[axis] = gauge_low[axis] + along * (gauge_high[axis] - gauge_low[axis]) +
						out[axis] * (gauge_high[gauge_normal] - gauge_low[gauge_normal]) * 0.02f;
				}
				else
				{
					result[axis] = gauge_low[axis] + along * (gauge_high[axis] - gauge_low[axis]) +
						out[axis] * (gauge_high[gauge_normal] - gauge_low[gauge_normal]) * 0.02f;
				}
				/* (one face, the pistol's ring: the meter kept its shape, as
				large as the face's narrower side allows, centred on it) */
				if (!paned && axis != gauge_normal)
				{
					real scale = 0.f;
					long other;

					for (other = 0; other < 3; other++)
					{
						real meter_span = meter_high[sources[other]] - meter_low[sources[other]];
						real ratio;

						if (other == gauge_normal || meter_span <= 0.0001f)
							continue;
						ratio = (gauge_high[other] - gauge_low[other]) / meter_span;
						if (scale == 0.f || ratio < scale)
							scale = ratio;
					}
					result[axis] = (gauge_low[axis] + gauge_high[axis]) * 0.5f +
						(along - 0.5f) * (meter_high[source_axis] - meter_low[source_axis]) * scale;
				}
			}
			/* (and onto the gauge's curved face where it is over it, else the
			meter's own bow, laid in the gauge's depth, sinks under the shell) */
			{
				real height;

				if (gauge_surface(template_, gauge_normal, out[gauge_normal] >= 0.f ? 1.f : -1.f, result, &height))
				{
					result[gauge_normal] = height +
						out[gauge_normal] * (gauge_high[gauge_normal] - gauge_low[gauge_normal]) * 0.04f;
				}
			}
			position->x = result[0];
			position->y = result[1];
			position->z = result[2];
		}
		else
		{
			position->x = place_x + (position->x - centre_x) * factor;
			position->y = place_y + (position->y - centre_y) * factor;
			position->z = place_z + (position->z - centre_z) * factor;
		}
	}
	if (one_sided)
	{
		place_x = (glow.x + ghigh.x) * 0.5f;
		place_y = (glow.y + ghigh.y) * 0.5f;
		place_z = (glow.z + ghigh.z) * 0.5f;
		factor = 1.f;
	}
	placed->x = place_x;
	placed->y = place_y;
	placed->z = place_z;
	fitted_ok = rasterizer_vertex_buffer_new(fitted, buffer->type, buffer->count, copy,
		stride * buffer->count) ? TRUE : FALSE;
	free(copy);
	if (fitted_ok)
		platform_log("anniversary: the heat meter %.3f %.3f %.3f to %.3f %.3f %.3f on the gauge %.3f %.3f %.3f to %.3f %.3f %.3f%s",
			mlow.x, mlow.y, mlow.z, mhigh.x, mhigh.y, mhigh.z, glow.x, glow.y, glow.z, ghigh.x, ghigh.y, ghigh.z,
			one_sided ? ", box to box" : "");
	if (fitted_ok)
		platform_log("anniversary: the heat meter fitted to the gauge, %.2f of its size, %.3f clear of the shell",
			factor, push);

	return fitted_ok;
}

/* the classic meter, when the template has a gauge, drawn from a buffer
shrunk to that gauge */
static void meter_part_place(
	struct saber_model *made,
	struct model const *map_model,
	struct saber_template const *template_,
	struct model_geometry_part const *source,
	struct model_geometry_part *part)
{
	long which;
	struct vertex_buffer fitted;
	real_point3d placed;

	if (!part_is_meter(map_model, source))
		return;
	for (which = 0; which < made->meter_buffer_count; which++)
	{
		if (made->meter_sources[which] == source)
			break;
	}
	if (which == made->meter_buffer_count)
	{
		if (made->meter_buffer_count >= (long)NUMBEROF(made->meter_buffers) ||
			!meter_buffer_fit(source, template_, &fitted, &placed))
		{
			return;
		}
		made->meter_buffers[which] = fitted;
		made->meter_sources[which] = source;
		made->meter_centres[which] = placed;
		made->meter_buffer_count++;
	}
	part->vertex_buffer = made->meter_buffers[which];
	part->centroid = made->meter_centres[which];
}

/* the name of the shader a map's model's part is drawn by, or "" */
static char const *part_shader_name(
	struct model const *map_model,
	struct model_geometry_part const *part)
{
	long shader_tag;

	if (!map_model || !VALID_INDEX(part->shader_index, map_model->shaders.count))
		return "";
	shader_tag = TAG_BLOCK_GET_ELEMENT(&map_model->shaders, part->shader_index,
		struct model_shader_reference)->shader.index;

	return shader_tag == NONE ? "" : tag_get_name(shader_tag);
}

/* whether a map's model's part is of the assault rifle's classic ammo
counter: its numbers, icons, data and compass */
static boolean part_is_counter(
	struct model const *map_model,
	struct model_geometry_part const *part)
{
	char const *name = part_shader_name(map_model, part);

	return strstr(name, "assault rifle") != NULL &&
		(strstr(name, "\\numbers") != NULL || strstr(name, "\\icons") != NULL ||
			strstr(name, "\\ar data") != NULL || strstr(name, "\\compass") != NULL);
}

/* the middle of a part's vertices (of its buffer); FALSE if it cannot be
read */
static boolean part_buffer_middle(
	struct model_geometry_part const *part,
	real_point3d *low,
	real_point3d *high)
{
	struct vertex_buffer const *buffer = &part->vertex_buffer;
	byte *locked = NULL;
	long stride;
	long index;

	if (!buffer->hardware_format || buffer->count <= 0)
		return FALSE;
	if (buffer->type == _rasterizer_vertex_type_model_compressed)
		stride = (long)sizeof(struct model_vertex_compressed);
	else if (buffer->type == _rasterizer_vertex_type_model_uncompressed)
		stride = (long)sizeof(struct model_vertex_uncompressed);
	else
		return FALSE;
	IDirect3DVertexBuffer8_Lock((IDirect3DVertexBuffer8 *)buffer->hardware_format, 0, 0, &locked,
		D3DLOCK_READONLY);
	if (!locked)
		return FALSE;
	for (index = 0; index < buffer->count; index++)
	{
		real_point3d const *position = (real_point3d const *)(locked + index * stride);

		low->x = MIN(low->x, position->x);
		low->y = MIN(low->y, position->y);
		low->z = MIN(low->z, position->z);
		high->x = MAX(high->x, position->x);
		high->y = MAX(high->y, position->y);
		high->z = MAX(high->z, position->z);
	}
	IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)buffer->hardware_format);

	return TRUE;
}

/* the classic ammo counter (the assault rifle's) moved onto the template's
own counter, its digits (`digits`, not drawn): the classic numbers' middle
on theirs. The classic sits high on the remastered display */
static void counter_part_place(
	struct saber_model *made,
	struct model const *map_model,
	struct saber_template const *template_,
	struct model_geometry_part const *source,
	struct model_geometry_part *part)
{
	long which;

	if (!part_is_counter(map_model, source))
		return;
	if (!made->counter_looked)
	{
		real_point3d low = { 1e9f, 1e9f, 1e9f };
		real_point3d high = { -1e9f, -1e9f, -1e9f };
		real_point3d digits_low = { 1e9f, 1e9f, 1e9f };
		real_point3d digits_high = { -1e9f, -1e9f, -1e9f };
		boolean any_numbers = FALSE;
		boolean any_digits = FALSE;
		long geometry_index;
		long part_index;

		made->counter_looked = TRUE;
		/* (the classic numbers, of the map's model's most detailed level) */
		for (geometry_index = 0; geometry_index < map_model->geometries.count && !any_numbers; geometry_index++)
		{
			struct model_geometry const *geometry = TAG_BLOCK_GET_ELEMENT(&map_model->geometries, geometry_index,
				struct model_geometry);

			for (part_index = 0; part_index < geometry->parts.count; part_index++)
			{
				struct model_geometry_part const *numbers = TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index,
					struct model_geometry_part);

				if (strstr(part_shader_name(map_model, numbers), "\\numbers") &&
					part_buffer_middle(numbers, &low, &high))
				{
					any_numbers = TRUE;
				}
			}
		}
		for (part_index = 0; part_index < template_->part_count; part_index++)
		{
			struct saber_mesh_part const *digits = &template_->parts[part_index];
			long vertex;

			if (digits->lod != 0 || digits->material_index < 0 || digits->material_index >= template_->material_count ||
				!strstr(template_->materials[digits->material_index], "digits"))
			{
				continue;
			}
			for (vertex = 0; vertex < digits->vertex_count; vertex++)
			{
				real_point3d const *position = &digits->vertices[vertex].position;

				digits_low.x = MIN(digits_low.x, position->x);
				digits_low.y = MIN(digits_low.y, position->y);
				digits_low.z = MIN(digits_low.z, position->z);
				digits_high.x = MAX(digits_high.x, position->x);
				digits_high.y = MAX(digits_high.y, position->y);
				digits_high.z = MAX(digits_high.z, position->z);
				any_digits = TRUE;
			}
		}
		/* (its digits are an effect's, left out of the template as it is
		read: where they were on its display, wpn_assault_display, of the
		display's box, 0.29 of it back to front, its middle across and 0.39
		bottom to top) */
		if (!any_digits)
		{
			real_point3d display_low = { 1e9f, 1e9f, 1e9f };
			real_point3d display_high = { -1e9f, -1e9f, -1e9f };
			boolean any_display = FALSE;

			for (part_index = 0; part_index < template_->part_count; part_index++)
			{
				struct saber_mesh_part const *display = &template_->parts[part_index];
				long vertex;

				if (display->lod != 0 || display->material_index < 0 ||
					display->material_index >= template_->material_count ||
					strcmp(template_->materials[display->material_index], "wpn_assault_display") != 0)
				{
					continue;
				}
				for (vertex = 0; vertex < display->vertex_count; vertex++)
				{
					real_point3d const *position = &display->vertices[vertex].position;

					display_low.x = MIN(display_low.x, position->x);
					display_low.y = MIN(display_low.y, position->y);
					display_low.z = MIN(display_low.z, position->z);
					display_high.x = MAX(display_high.x, position->x);
					display_high.y = MAX(display_high.y, position->y);
					display_high.z = MAX(display_high.z, position->z);
					any_display = TRUE;
				}
			}
			if (any_display)
			{
				real_point3d middle;

				middle.x = display_low.x + (display_high.x - display_low.x) * 0.29f;
				middle.y = (display_low.y + display_high.y) * 0.5f;
				middle.z = display_low.z + (display_high.z - display_low.z) * 0.39f;
				digits_low = middle;
				digits_high = middle;
				any_digits = TRUE;
			}
		}
		if (!any_numbers || !any_digits)
		{
			platform_log("anniversary: the classic ammo counter not moved: %s", !any_numbers ?
				"its numbers' vertices cannot be read" : "the template has no digits nor display");
		}
		if (any_numbers && any_digits)
		{
			made->counter_shift.i = (digits_low.x + digits_high.x - low.x - high.x) * 0.5f;
			made->counter_shift.j = (digits_low.y + digits_high.y - low.y - high.y) * 0.5f;
			made->counter_shift.k = (digits_low.z + digits_high.z - low.z - high.z) * 0.5f;
			/* (a shift as far as the counter is wide, or more, is not one) */
			made->counter_found =
				made->counter_shift.i * made->counter_shift.i + made->counter_shift.j * made->counter_shift.j +
					made->counter_shift.k * made->counter_shift.k <
				((high.x - low.x) * (high.x - low.x) + (high.y - low.y) * (high.y - low.y) +
					(high.z - low.z) * (high.z - low.z)) * 4.f;
			platform_log("anniversary: the classic ammo counter %s by %.4f %.4f %.4f, onto the template's digits",
				made->counter_found ? "moved" : "not moved, too far", made->counter_shift.i, made->counter_shift.j,
				made->counter_shift.k);
		}
	}
	if (!made->counter_found)
		return;
	for (which = 0; which < made->meter_buffer_count; which++)
	{
		if (made->meter_sources[which] == source)
			break;
	}
	if (which == made->meter_buffer_count)
	{
		struct vertex_buffer const *buffer = &source->vertex_buffer;
		byte *locked = NULL;
		byte *copy;
		long stride;
		long index;
		boolean made_ok;

		if (made->meter_buffer_count >= (long)NUMBEROF(made->meter_buffers) || !buffer->hardware_format ||
			buffer->count <= 0)
		{
			return;
		}
		if (buffer->type == _rasterizer_vertex_type_model_compressed)
			stride = (long)sizeof(struct model_vertex_compressed);
		else if (buffer->type == _rasterizer_vertex_type_model_uncompressed)
			stride = (long)sizeof(struct model_vertex_uncompressed);
		else
			return;
		copy = malloc((size_t)stride * (size_t)buffer->count);
		if (!copy)
			return;
		IDirect3DVertexBuffer8_Lock((IDirect3DVertexBuffer8 *)buffer->hardware_format, 0, 0, &locked,
			D3DLOCK_READONLY);
		if (!locked)
		{
			free(copy);
			return;
		}
		memcpy(copy, locked, (size_t)stride * (size_t)buffer->count);
		IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)buffer->hardware_format);
		for (index = 0; index < buffer->count; index++)
		{
			real_point3d *position = (real_point3d *)(copy + index * stride);

			position->x += made->counter_shift.i;
			position->y += made->counter_shift.j;
			position->z += made->counter_shift.k;
		}
		made_ok = rasterizer_vertex_buffer_new(&made->meter_buffers[which], buffer->type, buffer->count, copy,
			stride * buffer->count) ? TRUE : FALSE;
		free(copy);
		if (!made_ok)
			return;
		made->meter_sources[which] = source;
		made->meter_centres[which].x = source->centroid.x + made->counter_shift.i;
		made->meter_centres[which].y = source->centroid.y + made->counter_shift.j;
		made->meter_centres[which].z = source->centroid.z + made->counter_shift.k;
		made->meter_buffer_count++;
	}
	part->vertex_buffer = made->meter_buffers[which];
	part->centroid = made->meter_centres[which];
}

/* the map's model's region a template's part is the whole or damaged copy
of (its group's name, "left_arm", as the region's, "left arm"), when the
region has a damaged permutation (~damaged) to draw it by: NONE otherwise */
static long template_part_region(
	struct model const *map_model,
	struct saber_mesh_part const *part)
{
	char group[64];
	long region_index;

	if (!part->region[0])
		return NONE;
	variant_normalize(part->region, group, (long)sizeof(group));
	for (region_index = 0; region_index < map_model->regions.count && region_index <= (long)SABER_PART_REGION_MASK;
		region_index++)
	{
		struct model_region const *region = TAG_BLOCK_GET_ELEMENT(&map_model->regions, region_index, struct model_region);
		char name[64];
		long permutation;

		variant_normalize(region->name, name, (long)sizeof(name));
		if (strcmp(name, group) != 0)
			continue;
		for (permutation = 0; permutation < region->permutations.count; permutation++)
		{
			if (TAG_BLOCK_GET_ELEMENT(&region->permutations, permutation, struct model_region_permutation)->name[0] == '~')
				return region_index;
		}
		return NONE;
	}

	return NONE;
}

/* the variant of its group a template's part is, as the map's region of the
group's name permutes it (a marine's "arms": "__base" bare with gloves,
"arms_full_sleeve_100"): its model's mask of the region's permutations it is
drawn at, its index among made's (*region the region's), NONE when the
map's model has no such region or it is drawn at every permutation. The
"__base" is drawn at a permutation no variant is named */
static long template_part_variant(
	struct saber_model *made,
	struct saber_template const *template_,
	struct model const *map_model,
	struct saber_mesh_part const *part,
	long *region)
{
	char group[64];
	long region_index;
	unsigned long mask = 0;
	long index;

	*region = NONE;
	variant_normalize(part->region, group, (long)sizeof(group));
	for (region_index = 0; region_index < map_model->regions.count && region_index <= (long)SABER_PART_REGION_MASK;
		region_index++)
	{
		struct model_region const *map_region = TAG_BLOCK_GET_ELEMENT(&map_model->regions, region_index,
			struct model_region);
		char name[64];
		long permutation;

		variant_normalize(map_region->name, name, (long)sizeof(name));
		if (strcmp(name, group) != 0)
			continue;
		for (permutation = 0; permutation < map_region->permutations.count && permutation < 32; permutation++)
		{
			char permutation_name[64];
			boolean named = FALSE;
			long other;

			variant_normalize(TAG_BLOCK_GET_ELEMENT(&map_region->permutations, permutation,
				struct model_region_permutation)->name, permutation_name, (long)sizeof(permutation_name));
			if (!strcmp(permutation_name, part->group_variant))
			{
				mask |= 1UL << permutation;
				continue;
			}
			/* (the base at a permutation none of the group's variants is) */
			for (other = 0; other < template_->part_count && !named; other++)
			{
				struct saber_mesh_part const *sibling = &template_->parts[other];

				if (sibling->group_variant[0] && !strcmp(sibling->region, part->region) &&
					!strcmp(sibling->group_variant, permutation_name))
				{
					named = TRUE;
				}
			}
			if (!named && !strcmp(part->group_variant, "__base"))
				mask |= 1UL << permutation;
		}
		if (map_region->permutations.count <= 1 || map_region->permutations.count > 8 ||
			mask == (1UL << map_region->permutations.count) - 1)
		{
			return NONE;
		}
		(void)made;
		(void)index;
		*region = region_index;
		return (long)mask;
	}

	return NONE;
}

/* The model the template makes of the map's model: the map's, its every
permutation drawing the template's geometry (`shader_indices`, the shader
each part is drawn with). NULL, with *reason saying why, when it cannot be
made. A region named head draws one head, the permutation's, and the body
is drawn by another region. */
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
	/* the template's levels of detail there are parts of */
	short levels[5];
	long level_count = 0;
	long level;
	long made_index;
	long rank_index;
	long default_rank = 0;
	/* the map's model's parts drawn by transparent shaders, and the region
	each came from (a part of a region of more than one permutation is
	drawn only of the region's first permutation's geometry: the permutation
	otherwise chosen does not draw the same, a shield generator's shield
	gone when its region is permuted to ~damaged) */
	struct model_geometry_part const *clear_parts[MAXIMUM_PARTS_PER_MODEL_GEOMETRY];
	long clear_regions[MAXIMUM_PARTS_PER_MODEL_GEOMETRY];
	long clear_count = 0;
	long geometry_part_count = 0;
	/* A gauge of the template's own, so the classic heat meter is shrunk to
	that gauge: drawn at the classic gun's size, it spills past the side.
	A monicle of its own, so the classic visor (marine_hud) is not drawn
	over the face: its scroll is the green flicker, and on the anniversary
	head it runs across the face */
	boolean own_meter = FALSE;
	boolean own_monicle = FALSE;
	/* whether the template has a wreck (its parts flagged) */
	boolean template_wrecked = FALSE;
	/* a region named head, the template's heads (its nodes under one named
	head), and the region that draws the body */
	long head_region = NONE;
	long body_region = 0;
	/* a region of more than one permutation whose clear parts its permutations
	other than the first must not draw (a shield generator's shield, gone
	when the region is permuted to ~damaged): each permutation draws the
	geometry without the region's clear parts. NONE for none */
	long hideable_region = NONE;
	/* the geometry of each level and rank without the hideable region's
	clear parts (after the heads'), NONE for none */
	long plain_base = NONE;
	/* when the hideable region is not the one that draws the body (a
	Jackal's shield, its region of its own beside its arms'), the geometry
	of each level and rank of the region's clear parts alone, after the plain
	ones: the region's first permutation draws it, its others nothing, and
	the body is drawn plain. NONE for none */
	long clear_base = NONE;
	char head_keys[MAXIMUM_SABER_HEAD_PERMUTATIONS][64];
	long head_count = 0;
	long body_span;
	long which;
	boolean first_person;

	*reason = "out of memory";
	if (!made)
		return NULL;
	csmemset(made, 0, sizeof(*made));
	first_person = model_is_first_person(tag_get_name(tag_index));
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
	/* (its ranks, the least first; one of every rank's, if it has none) */
	for (rank_index = _saber_rank_minor; rank_index < NUMBER_OF_SABER_RANKS; rank_index++)
	{
		for (part_index = 0; part_index < template_->part_count; part_index++)
		{
			if (template_->parts[part_index].rank == rank_index)
				break;
		}
		if (part_index < template_->part_count)
			made->ranks[made->rank_count++] = (short)rank_index;
	}
	if (!made->rank_count)
		made->ranks[made->rank_count++] = _saber_rank_all;
	made->level_count = level_count;
	for (part_index = 0; part_index < template_->material_count; part_index++)
	{
		if (strstr(template_->materials[part_index], "gauge"))
			own_meter = TRUE;
		if (template_gauge_stand_in(template_))
			own_meter = TRUE;
		if (strstr(template_->materials[part_index], "monicle"))
			own_monicle = TRUE;
	}
	/* (the map's model's transparent parts, of each region's first
	permutation's most detailed geometry; a model whose parts have local
	nodes has none drawn so. Its glass is drawn as it always was, before the
	package's glass, except a Pelican's canopy: the package's has the frame
	and the panes both, and the map's pane over them draws the classic glass
	across the anniversary cockpit. A Warthog's windscreen must stay the
	map's: the package's material for glass is not the pane's mesh there,
	and leaving the map's out draws no window at all. Its visor is left out
	when the template has a monicle: the classic green flicker, drawn across
	the anniversary face) */
	if (!TEST_FLAG(map_model->flags, MODEL_PARTS_HAVE_LOCAL_NODES_BIT))
	{
		short seen[16];
		long seen_count = 0;

		for (region_index = 0; region_index < map_model->regions.count && seen_count < (long)NUMBEROF(seen);
			region_index++)
		{
			struct model_region const *map_region = TAG_BLOCK_GET_ELEMENT(&map_model->regions, region_index,
				struct model_region);
			struct model_geometry const *geometry;
			short geometry_index;
			long other;

			if (map_region->permutations.count <= 0)
				continue;
			geometry_index = TAG_BLOCK_GET_ELEMENT(&map_region->permutations, 0,
				struct model_region_permutation)->geometry_indices[4];
			if (!VALID_INDEX(geometry_index, map_model->geometries.count))
				continue;
			for (other = 0; other < seen_count && seen[other] != geometry_index; other++)
			{
			}
			if (other < seen_count)
				continue;
			seen[seen_count++] = geometry_index;
			geometry = TAG_BLOCK_GET_ELEMENT(&map_model->geometries, geometry_index, struct model_geometry);
			for (part_index = 0; part_index < geometry->parts.count && clear_count < MAXIMUM_PARTS_PER_MODEL_GEOMETRY;
				part_index++)
			{
				struct model_geometry_part const *part = TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index,
					struct model_geometry_part);
				long shader_tag = VALID_INDEX(part->shader_index, map_model->shaders.count) ?
					TAG_BLOCK_GET_ELEMENT(&map_model->shaders, part->shader_index,
						struct model_shader_reference)->shader.index : NONE;
				short shader_type = shader_tag == NONE ? (short)SHADER_TYPE_MODEL :
					((struct shader *)shader_definition_get(shader_tag))->base.type;
				char const *shader_name = shader_tag == NONE ? NULL : tag_get_name(shader_tag);

				if (shader_tag != NONE && shader_type != SHADER_TYPE_MODEL && shader_type != SHADER_TYPE_ENVIRONMENT &&
					!(own_monicle && shader_name && strstr(shader_name, "hud")) &&
					!(shader_type == SHADER_TYPE_TRANSPARENT_GLASS && map_glass_replaced(shader_tag)) &&
					!map_clear_replaced(shader_tag))
				{
					clear_regions[clear_count] = region_index;
					clear_parts[clear_count++] = part;
				}
			}
		}
	}
	/* a region of more than one permutation whose clear parts its other
	permutations must not draw: the first such of those the clear parts came
	from (a shield generator's shield is drawn of its first permutation's
	geometry with the template's, so its ~damaged permutation must draw a
	geometry without it) */
	for (region_index = 0; region_index < map_model->regions.count && hideable_region == NONE;
		region_index++)
	{
		long index;

		for (index = 0; index < clear_count; index++)
		{
			if (clear_regions[index] == region_index)
			{
				if (TAG_BLOCK_GET_ELEMENT(&map_model->regions, region_index,
					struct model_region)->permutations.count > 1)
				hideable_region = region_index;
				break;
			}
		}
	}
	made->tag_index = tag_index;
	made->map_model = *map_model;
	made->model = *map_model;
	made->model.flags &= ~FLAG(MODEL_PARTS_HAVE_LOCAL_NODES_BIT);
	/* (the map's model's texture scale kept, for the map's parts drawn
	with the template's, a Jackal's shield, whose coordinates it scales:
	the template's are divided by it as they are made, so drawn as they
	are) */
	made->model.base_map_scale.i = map_model->base_map_scale.i > 0.01f ? map_model->base_map_scale.i : 1.f;
	made->model.base_map_scale.j = map_model->base_map_scale.j > 0.01f ? map_model->base_map_scale.j : 1.f;

	/* a region named head draws one of the template's heads (the nodes
	under one named head). The body is the other meshes, drawn by the
	first region that is not the head, so the faces are not stacked */
	body_span = level_count * made->rank_count;
	for (region_index = 0; region_index < map_model->regions.count; region_index++)
	{
		struct model_region const *map_region = TAG_BLOCK_GET_ELEMENT(&map_model->regions, region_index,
			struct model_region);

		if (!_stricmp(map_region->name, "head"))
			head_region = region_index;
	}
	/* (or the region whose permutations are the template's bones, a
	boulder's: each of its rocks drawn as a head is) */
	if (head_region == NONE)
		head_region = permutation_variant_region(template_, map_model);
	if (head_region != NONE && level_count > 0)
	{
		for (part_index = 0; part_index < template_->part_count; part_index++)
		{
			char const *key = template_->parts[part_index].variant;
			long other;

			if (!key[0])
				continue;
			for (other = 0; other < head_count && strcmp(head_keys[other], key) != 0; other++)
			{
			}
			if (other < head_count || head_count >= MAXIMUM_SABER_HEAD_PERMUTATIONS)
				continue;
			memcpy(head_keys[head_count], key, sizeof(head_keys[head_count]) - 1);
			head_keys[head_count][sizeof(head_keys[head_count]) - 1] = 0;
			head_count++;
		}
		while (head_count > 0 &&
			body_span * (1 + head_count) > (long)NUMBEROF(made->geometries))
		{
			head_count--;
		}
		body_region = NONE;
		for (region_index = 0; region_index < map_model->regions.count; region_index++)
		{
			if (region_index != head_region)
			{
				body_region = region_index;
				break;
			}
		}
	}
	/* the plain geometries follow the heads', of each rank and level (the
	room for them is checked before they are built, below) */
	if (hideable_region != NONE)
		plain_base = body_span * (1 + head_count);
	if (hideable_region != NONE && hideable_region != (head_count ? body_region : 0))
		clear_base = plain_base + body_span;
	if (clear_base != NONE && clear_base + body_span > (long)NUMBEROF(made->geometries))
		plain_base = clear_base = NONE;
	/* the map's regions and permutations: the body by one region, a head
	by the head region's permutation of its name */
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
			/* (a close model, Halo's highest level, the template's most
			detailed, each lower level its next, and the rest its least.
			A first-person model is drawn with no size, so that choice is
			its lowest, and it is the most detailed at every level. The
			body by the first region that is not the head; a head by the
			permutation of its name, "__base" when the name is not one of
			the template's) */
			which = NONE;
			if (head_count && region_index == head_region)
			{
				which = head_key_index(head_keys, head_count, permutation->name);
				if (which == NONE)
					which = head_key_index(head_keys, head_count, "__base");
				if (which == NONE)
					which = 0;
			}
			for (detail = 0; detail < 5; detail++)
			{
				short slot = first_person ? 0 : (short)MIN(level_count - 1, 4 - detail);

				if (head_count && region_index == head_region)
					permutation->geometry_indices[detail] = (short)(body_span + which * body_span + slot);
				else if (clear_base != NONE && region_index == hideable_region)
					/* (the shield of its own region: there while the region is
					its first permutation, gone at its others, a Jackal's
					~shield_off as it dies) */
					/* (its others the map's own geometry of that permutation, its
					shield generator, which the template has none of: the map's,
					found among this model's below, marked until then) */
					permutation->geometry_indices[detail] = permutation_index == 0 ?
						(short)(clear_base + slot) :
						(short)(VALID_INDEX(permutation->geometry_indices[detail], map_model->geometries.count) &&
							/* (a character's, whose bitmaps keep the map's own first;
							a shield's other permutation, ~shield_off, not a Grunt's
							shellback, a whole other head and back) */
							strstr(tag_get_name(tag_index), "characters\\") != NULL &&
							strstr(permutation->name, "shield") != NULL ?
							-2 - permutation->geometry_indices[detail] : NONE);
				else if (clear_base != NONE && (head_count ? region_index == body_region : region_index == 0))
					/* (and the body without it) */
					permutation->geometry_indices[detail] = (short)(plain_base + slot);
				else if (head_count ? region_index != body_region : region_index != 0)
					permutation->geometry_indices[detail] = NONE;
				else if (plain_base != NONE && region_index == hideable_region && permutation_index > 0)
					/* (a permutation other than the first (a shield generator's
					~damaged) draws the geometry without the region's clear
					parts, that the shield goes when the region is permuted;
					the rank's block of it, as the body's is) */
					permutation->geometry_indices[detail] = (short)(plain_base + slot);
				else
					permutation->geometry_indices[detail] = slot;
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
	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		if (template_->parts[part_index].wreck)
			template_wrecked = TRUE;
	}
	for (made_index = 0; made_index < template_->part_count; made_index++)
	{
		struct saber_mesh_part const *source = &template_->parts[made_index];
		struct model_geometry_part *part = &made->parts[made_index];
		struct model_vertex_uncompressed *scaled = malloc(sizeof(*scaled) * ((size_t)source->vertex_count + 1));
		long strip_count;
		long index;

		*reason = "out of memory";
		if (!scaled)
			goto refused;
		part->shader_index = shader_indices[made_index];
		part->previous_part_index = NONE;
		part->next_part_index = NONE;
		part->centroid_primary_node_weight = 1.f;
		part_centroid_node(part, source);
		/* (a template with a wreck: its whole hull drawn while it lives, its
		wreck once it is destroyed, saber_models_part_hidden) */
		{
			long region = template_part_region(map_model, source);

			long variant_mask_index = NONE;

			if (source->card == 2)
				part->flags |= SABER_PART_SOLID_FLAG;

			if (region == NONE && source->group_variant[0])
				variant_mask_index = template_part_variant(made, template_, map_model, source, &region);
			if (variant_mask_index != NONE)
			{
				part->flags |= SABER_PART_VARIANT_FLAG | ((unsigned long)region << SABER_PART_REGION_SHIFT) |
					((unsigned long)variant_mask_index << SABER_PART_VARIANT_SHIFT);
			}
			else if (region != NONE)
			{
				part->flags |= SABER_PART_REGION_FLAG | ((unsigned long)region << SABER_PART_REGION_SHIFT) |
					(source->wreck ? SABER_PART_DAMAGED_FLAG : 0);
			}
			else if (template_wrecked)
			{
				part->flags |= source->wreck ? SABER_PART_WRECK_FLAG : SABER_PART_WHOLE_FLAG;
			}
		}
		for (index = 0; index < source->vertex_count; index++)
		{
			scaled[index] = source->vertices[index];
			/* (its coordinates divided by the map's model's texture scale,
			which the game multiplies them by; kept within the compressed
			range they were in, a scale a little under 1 (a Jackal's 0.997)
			taking an edge's 1 past it, where it wraps) */
			{
				real u = scaled[index].texcoord.x / made->model.base_map_scale.i;
				real v = scaled[index].texcoord.y / made->model.base_map_scale.j;

				if (scaled[index].texcoord.x >= -1.05f && scaled[index].texcoord.x <= 1.05f)
					u = u > 1.f ? 1.f : (u < -1.f ? -1.f : u);
				if (scaled[index].texcoord.y >= -1.05f && scaled[index].texcoord.y <= 1.05f)
					v = v > 1.f ? 1.f : (v < -1.f ? -1.f : v);
				scaled[index].texcoord.x = u;
				scaled[index].texcoord.y = v;
			}
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
		{
			/* (the plasma pistol's heat ring left out of its part, the classic
			meter laid where it was: of its ring's parts, and of its glow's,
			`plasmapistol_04`, a second ring over the first) */
			unsigned char *skip = NULL;

			if ((part_gauge(template_, source) ||
					(source->material_index >= 0 && source->material_index < template_->material_count &&
						strstr(template_->materials[source->material_index], "plasmapistol_04") != NULL)) &&
				template_gauge_stand_in(template_) && source->triangle_count > 0)
			{
				/* (malloc, as free is this build's: its calloc is the heap's own) */
				skip = malloc((size_t)source->triangle_count);
				if (skip)
					memset(skip, 0, (size_t)source->triangle_count);
				if (skip && !ring_meter_fit(template_, NULL, 0, 0, NULL, source, skip))
				{
					free(skip);
					skip = NULL;
				}
			}
			strip_count = part_strip(source, skip, strips);
			/* (this build's free halts on no pointer) */
			if (skip)
				free(skip);
		}
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
	/* its geometries: of each rank, each level's parts of every rank and of
	that one, the map's transparent parts, and a plain one of each rank and
	level without a region's parts (a shield generator's shield) */
	made->geometry_parts = malloc(sizeof(*made->geometry_parts) *
		((size_t)MAXIMUM_PARTS_PER_MODEL_GEOMETRY * (size_t)level_count * (size_t)made->rank_count *
			(size_t)(1 + head_count + (hideable_region != NONE ? 2 : 0)) + 1));
	*reason = "out of memory";
	if (!made->geometry_parts)
		goto refused;
	for (rank_index = 0; rank_index < made->rank_count; rank_index++)
	{
		short rank = made->ranks[rank_index];

		if (rank == _saber_rank_minor)
			default_rank = rank_index;
		for (level = 0; level < level_count; level++)
		{
			struct model_geometry *geometry = &made->geometries[rank_index * level_count + level];
			long first = geometry_part_count;
			long index;

			for (part_index = 0; part_index < made->part_count; part_index++)
			{
				struct saber_mesh_part const *source = &template_->parts[part_index];

				if (MIN(source->lod, 4) != levels[level] ||
					(source->rank != _saber_rank_all && source->rank != rank) ||
					(head_count && source->variant[0]) ||
					material_monicle(template_, source) || part_stands_in(template_, source) ||
					geometry_part_count - first >= MAXIMUM_PARTS_PER_MODEL_GEOMETRY)
				{
					continue;
				}
				made->geometry_parts[geometry_part_count++] = made->parts[part_index];
			}
			for (index = 0; index < clear_count && geometry_part_count - first < MAXIMUM_PARTS_PER_MODEL_GEOMETRY;
				index++)
			{
				struct model_geometry_part *part = &made->geometry_parts[geometry_part_count++];

				*part = *clear_parts[index];
				part->previous_part_index = NONE;
				part->next_part_index = NONE;
				/* (a fitted meter's buffer is this model's, and its centroid
				placed: Back draws the map's part with the map's own, whose
				buffer is given back with the model, saber_toggle.c) */
				if (own_meter)
					meter_part_place(made, map_model, template_, clear_parts[index], part);
				else
				{
					part->vertex_buffer = clear_parts[index]->vertex_buffer;
					part->centroid = clear_parts[index]->centroid;
				}
				counter_part_place(made, map_model, template_, clear_parts[index], part);
			}
			geometry->parts.count = geometry_part_count - first;
			geometry->parts.address = made->geometry_parts + first;
			geometry->parts.definition = NULL;
		}
	}
	/* each head permutation its own geometries, after the body's: the
	meshes under that head, and the body too when no other region draws it */
	for (which = 0; which < head_count; which++)
	{
		for (rank_index = 0; rank_index < made->rank_count; rank_index++)
		{
			short rank = made->ranks[rank_index];

			for (level = 0; level < level_count; level++)
			{
				long geo_index = body_span + which * body_span + rank_index * level_count + level;
				struct model_geometry *geometry;
				long first;
				long index;

				if (geo_index < 0 || geo_index >= (long)NUMBEROF(made->geometries))
					continue;
				geometry = &made->geometries[geo_index];
				first = geometry_part_count;
				for (part_index = 0; part_index < made->part_count; part_index++)
				{
					struct saber_mesh_part const *source = &template_->parts[part_index];

					if (MIN(source->lod, 4) != levels[level] ||
						(source->rank != _saber_rank_all && source->rank != rank) ||
						part_stands_in(template_, source) ||
						geometry_part_count - first >= MAXIMUM_PARTS_PER_MODEL_GEOMETRY)
					{
						continue;
					}
					if (source->variant[0])
					{
						if (strcmp(source->variant, head_keys[which]) != 0)
							continue;
					}
					else if (body_region != NONE)
					{
						continue;
					}
					/* (the eyepiece is its helmet's, and the plain head's.
					The capped sergeant does not wear it) */
					if (material_monicle(template_, source) && head_without_monicle(head_keys[which]))
						continue;
					made->geometry_parts[geometry_part_count++] = made->parts[part_index];
				}
				if (body_region == NONE)
				{
					for (index = 0; index < clear_count &&
						geometry_part_count - first < MAXIMUM_PARTS_PER_MODEL_GEOMETRY; index++)
					{
						struct model_geometry_part *part = &made->geometry_parts[geometry_part_count++];

						*part = *clear_parts[index];
						part->previous_part_index = NONE;
						part->next_part_index = NONE;
						if (own_meter)
							meter_part_place(made, map_model, template_, clear_parts[index], part);
						counter_part_place(made, map_model, template_, clear_parts[index], part);
					}
				}
				geometry->parts.count = geometry_part_count - first;
				geometry->parts.address = made->geometry_parts + first;
				geometry->parts.definition = NULL;
			}
		}
	}
	made->geometry_count = body_span * (1 + head_count);
	if (made->geometry_count > (long)NUMBEROF(made->geometries))
		made->geometry_count = (long)NUMBEROF(made->geometries);
	/* the plain geometries are not built where there is no room for them:
	the body's geometries then keep the region's clear parts, and every
	permutation of the region draws them (as the port drew them before) */
	if (plain_base != NONE && plain_base + level_count * made->rank_count > (long)NUMBEROF(made->geometries))
		plain_base = NONE;
	if (plain_base != NONE)
	{
		/* the geometries without the region's parts: its permutations other
		than the first's (a shield generator's ~damaged) */
		for (rank_index = 0; rank_index < made->rank_count; rank_index++)
		{
			for (level = 0; level < level_count; level++)
			{
				struct model_geometry *geometry = &made->geometries[plain_base + rank_index * level_count + level];
				long first = geometry_part_count;
				long index;

				for (part_index = 0; part_index < made->part_count; part_index++)
				{
					struct saber_mesh_part const *source = &template_->parts[part_index];

					if (MIN(source->lod, 4) != levels[level] ||
						(source->rank != _saber_rank_all && source->rank != made->ranks[rank_index]) ||
						(head_count && source->variant[0]) ||
						material_monicle(template_, source) || part_stands_in(template_, source) ||
						geometry_part_count - first >= MAXIMUM_PARTS_PER_MODEL_GEOMETRY)
					{
						continue;
					}
					made->geometry_parts[geometry_part_count++] = made->parts[part_index];
				}
				for (index = 0; index < clear_count && geometry_part_count - first < MAXIMUM_PARTS_PER_MODEL_GEOMETRY;
					index++)
				{
					struct model_geometry_part *part;

					if (clear_regions[index] == hideable_region)
						continue;
					part = &made->geometry_parts[geometry_part_count++];
					*part = *clear_parts[index];
					part->previous_part_index = NONE;
					part->next_part_index = NONE;
					if (own_meter)
						meter_part_place(made, map_model, template_, clear_parts[index], part);
					else
					{
						part->vertex_buffer = clear_parts[index]->vertex_buffer;
						part->centroid = clear_parts[index]->centroid;
					}
					counter_part_place(made, map_model, template_, clear_parts[index], part);
				}
				geometry->parts.count = geometry_part_count - first;
				geometry->parts.address = made->geometry_parts + first;
				geometry->parts.definition = NULL;
			}
		}
		made->geometry_count = plain_base + level_count * made->rank_count;
	}
	if (plain_base != NONE && clear_base != NONE)
	{
		/* the hideable region's clear parts alone, of each rank and level */
		for (rank_index = 0; rank_index < made->rank_count; rank_index++)
		{
			for (level = 0; level < level_count; level++)
			{
				struct model_geometry *geometry = &made->geometries[clear_base + rank_index * level_count + level];
				long first = geometry_part_count;
				long index;

				for (index = 0; index < clear_count && geometry_part_count - first < MAXIMUM_PARTS_PER_MODEL_GEOMETRY;
					index++)
				{
					struct model_geometry_part *part;

					if (clear_regions[index] != hideable_region)
						continue;
					part = &made->geometry_parts[geometry_part_count++];
					*part = *clear_parts[index];
					part->previous_part_index = NONE;
					part->next_part_index = NONE;
					part->vertex_buffer = clear_parts[index]->vertex_buffer;
					part->centroid = clear_parts[index]->centroid;
				}
				/* (and with it the map's own geometry of the region's second
				permutation, a Jackal's shield generator (~shield_off), which
				the template has none of: there with the shield, as without
				it. A character's, whose bitmaps keep the map's own first) */
				if (strstr(tag_get_name(tag_index), "characters\\"))
				{
					struct model_region const *hideable = TAG_BLOCK_GET_ELEMENT(&map_model->regions, hideable_region,
						struct model_region);
					short map_index = hideable->permutations.count > 1 &&
						strstr(TAG_BLOCK_GET_ELEMENT(&hideable->permutations, 1, struct model_region_permutation)->name,
							"shield") != NULL ?
						TAG_BLOCK_GET_ELEMENT(&hideable->permutations, 1, struct model_region_permutation)->geometry_indices[4] :
						(short)NONE;

					if (VALID_INDEX(map_index, map_model->geometries.count))
					{
						struct model_geometry const *generator = TAG_BLOCK_GET_ELEMENT(&map_model->geometries, map_index,
							struct model_geometry);
						long generator_part;

						for (generator_part = 0; generator_part < generator->parts.count &&
							geometry_part_count - first < MAXIMUM_PARTS_PER_MODEL_GEOMETRY; generator_part++)
						{
							struct model_geometry_part *part = &made->geometry_parts[geometry_part_count++];

							*part = *TAG_BLOCK_GET_ELEMENT(&generator->parts, generator_part, struct model_geometry_part);
							part->previous_part_index = NONE;
							part->next_part_index = NONE;
						}
					}
				}
				geometry->parts.count = geometry_part_count - first;
				geometry->parts.address = made->geometry_parts + first;
				geometry->parts.definition = NULL;
			}
		}
		made->geometry_count = clear_base + level_count * made->rank_count;
	}
	/* (the map's own geometries the hideable region's other permutations
	draw, a Jackal's shield generator, copied after the template's) */
	{
		long map_indices[16];
		long made_indices[16];
		long copied = 0;
		long permutation;

		for (permutation = 0; permutation < next; permutation++)
		{
			long detail;

			for (detail = 0; detail < 5; detail++)
			{
				short *index = &made->permutations[permutation].geometry_indices[detail];
				long map_index;
				long which;

				if (*index > -2)
					continue;
				map_index = -2 - *index;
				for (which = 0; which < copied && map_indices[which] != map_index; which++)
				{
				}
				if (which == copied)
				{
					if (copied >= (long)NUMBEROF(map_indices) ||
						made->geometry_count >= (long)NUMBEROF(made->geometries))
					{
						*index = NONE;
						continue;
					}
					made->geometries[made->geometry_count] = *TAG_BLOCK_GET_ELEMENT(&map_model->geometries, map_index,
						struct model_geometry);
					map_indices[copied] = map_index;
					made_indices[copied] = made->geometry_count++;
					copied++;
				}
				*index = (short)made_indices[which];
			}
		}
	}
	made->model.geometries.count = made->geometry_count;
	made->model.geometries.address = made->geometries;
	made->model.geometries.definition = NULL;
	/* (Halo's permutations the least rank's, saber_models_geometry_index
	another's) */
	for (part_index = 0; part_index < next; part_index++)
	{
		long detail;

		for (detail = 0; detail < 5; detail++)
		{
			if (made->permutations[part_index].geometry_indices[detail] != NONE)
				made->permutations[part_index].geometry_indices[detail] += (short)(default_rank * level_count);
		}
	}
	if (head_count)
	{
		platform_log("anniversary: %s's head is %ld permutations", tag_get_name(tag_index), head_count);
	}

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

/* (model_play: a model of no model shader, its shaders the structure's
(an a10 barricade's steel and plate), draws its template with another's,
borrowed: its index among the model's shaders this) */
#define BORROWED_SHADER_INDEX (-2)

/* model_shader_tag, or `borrowed` for BORROWED_SHADER_INDEX */
static long play_shader_tag(
	struct model const *model,
	long borrowed,
	short shader_index)
{
	return shader_index == BORROWED_SHADER_INDEX ? borrowed : model_shader_tag(model, shader_index);
}

static boolean tag_kept(long tag_index);
static boolean scroll_shader(long shader_tag);

/* a model shader of the map's to draw a model of none with: one a model
drawn as a template already plays over (its maps already its base map
alone), else the first that no model drawn as it is draws with. NONE when
there is none */
static long borrowed_model_shader(void)
{
	struct saber_shader const *played;
	struct tag_iterator iterator;
	long shader_tag;

	for (played = saber_models_globals.shaders; played; played = played->next)
	{
		struct shader const *shader = shader_definition_get(played->tag_index);

		/* (not one that scrolls, a plasma rifle's arc's, nor changes colour) */
		if (shader && shader->base.type == SHADER_TYPE_MODEL && !tag_kept(played->tag_index) &&
			!scroll_shader(played->tag_index) &&
			((struct shader_model_definition const *)shader)->model.change_color_source <= 0 &&
			((struct shader_model_definition const *)shader)->model.base_map.index != NONE &&
			!tag_kept(((struct shader_model_definition const *)shader)->model.base_map.index))
		{
			return played->tag_index;
		}
	}
	tag_iterator_new(&iterator, SHADER_DEFINITION_TAG);
	while ((shader_tag = tag_iterator_next(&iterator)) != NONE)
	{
		struct shader const *shader = shader_definition_get(shader_tag);

		if (shader && shader->base.type == SHADER_TYPE_MODEL && !tag_kept(shader_tag) &&
			((struct shader_model_definition const *)shader)->model.base_map.index != NONE &&
			!tag_kept(((struct shader_model_definition const *)shader)->model.base_map.index))
		{
			return shader_tag;
		}
	}

	return NONE;
}

/* whether the map's shader `shader_tag` is the glass a Pelican's cockpit is
drawn with: `vehicles\pelican\shaders\pelican_glass`, which the package's
model draws for itself. Its panes are not a model shader's, so there is no
bitmap to tell them by. The map's other panes are drawn as they always were
(a Warthog's windscreen is `warthog windshield`) */
static boolean map_glass_replaced(
	long shader_tag)
{
	char const *name = shader_tag == NONE ? NULL : tag_get_name(shader_tag);

	return name && strstr(name, "pelican") != NULL;
}

/* whether the map's transparent shader `shader_tag` draws a part the
package's model has of its own, which drawn over it would show the classic
texture: a Warthog's chain gun's ammunition belt (`chain gun belt ammo`, the
package's gun has its belt) and the plate under the gun (`warthog lights`,
behind the cab), and the Master Chief's lights (`light shield`, `light
shield generator`): panes the classic armour covered but for its slits,
which the anniversary's does not, and its own lights are in its texture */
static boolean map_clear_replaced(
	long shader_tag)
{
	char const *name = shader_tag == NONE ? NULL : tag_get_name(shader_tag);

	/* (and the plasma grenade's decal: the template's glyph is drawn) */
	return name && (strstr(name, "belt") != NULL || strstr(name, "warthog lights") != NULL ||
		strstr(name, "plasma grenade\\shaders\\pg decal") != NULL ||
		/* (and the needler's needles: the template's are drawn, and go with
		their frames as the needler empties. Their glow is `needler luminous`
		(`needler luminous 3p` held by another), their cores `needler core`) */
		strstr(name, "needler luminous") != NULL || strstr(name, "needler\\shaders\\needler core") != NULL ||
		strstr(name, "cyborg\\shaders\\light shield") != NULL);
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

/* the Collection's texture archives in the level's folder, once */
static void ipaks_open(
	void)
{
	static char const *const names[MAXIMUM_IPAKS] = {
		"inplace1.ipak", "inplace2.ipak", "inplace2o.ipak", "common.ipak"
	};
	long index;

	if (saber_models_globals.ipaks_tried)
		return;
	saber_models_globals.ipaks_tried = TRUE;
	for (index = 0; index < MAXIMUM_IPAKS; index++)
	{
		char path[1100];
		char const *reason;
		struct saber_pak *pak;

		snprintf(path, sizeof(path), "%s%s", saber_models_globals.folder, names[index]);
		pak = saber_pak_open(path, &reason);
		if (!pak)
		{
			if (strcmp(reason, "there is no such file"))
				platform_log("anniversary: no textures from %s: %s", path, reason);
			continue;
		}
		saber_models_globals.ipaks[saber_models_globals.ipak_count++] = pak;
		platform_log("anniversary: textures from %s", path);
	}
}

/* where a texture is looked for, most detail first: the level's package,
the 360's pak_stream, the Collection's ipaks, then the level's 64-pixel
mipmaps. `sources` may be NULL. */
static long texture_paks(
	struct saber_pak *pak,
	struct saber_pak **paks,
	long *types,
	char const **sources)
{
	long count = 0;
	long index;

	paks[count] = pak;
	types[count] = _saber_entry_textures;
	if (sources)
		sources[count] = "the level's package";
	count++;
	paks[count] = saber_stream_pak();
	types[count] = _saber_entry_textures;
	if (sources)
		sources[count] = "pak_stream";
	count++;
	ipaks_open();
	for (index = 0; index < saber_models_globals.ipak_count; index++)
	{
		paks[count] = saber_models_globals.ipaks[index];
		types[count] = _saber_entry_textures;
		if (sources)
			sources[count] = "an ipak";
		count++;
	}
	paks[count] = pak;
	types[count] = _saber_entry_textures_mips64;
	if (sources)
		sources[count] = "the level's 64-pixel mipmaps";
	count++;

	return count;
}

/* The texture `texture_name`, of the most detail there is that the texture
cache can draw: the level's package's full texture, else the stream's
(pak_stream.s3dpak, beside the level's: most full textures are there), else
the Collection's ipaks in that folder, else the level's 64-pixel one.
FALSE, with *reason saying why, when there is none this build draws. */
static boolean texture_find(
	struct saber_pak *pak,
	char const *texture_name,
	struct saber_texture *texture,
	char const **source,
	char const **reason)
{
	struct saber_pak *paks[MAXIMUM_TEXTURE_PAKS];
	long types[MAXIMUM_TEXTURE_PAKS];
	char const *sources[MAXIMUM_TEXTURE_PAKS];
	long count;
	long index;

	count = texture_paks(pak, paks, types, sources);
	*reason = "the packages have not got its texture";
	for (index = 0; index < count; index++)
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

static boolean bitmap_describe(struct saber_texture const *texture, long bitmap_tag, struct bitmap_data *bitmap);

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
	if (!bitmap_describe(texture, bitmap_tag, bitmap))
	{
		*reason = "its texture's pixels are not all there";
		saber_texture_dispose(texture);
		return FALSE;
	}

	return TRUE;
}

/* A bitmap of the map's bitmap tag `bitmap_tag` of a texture read; FALSE if
its pixels are not all there. */
static boolean bitmap_describe(
	struct saber_texture const *texture,
	long bitmap_tag,
	struct bitmap_data *bitmap)
{
	csmemset(bitmap, 0, sizeof(*bitmap));
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

	return bitmap->pixels_size > 0 && bitmap->pixels_size <= texture->pixels_size;
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
memory for it. Back's switch puts the map's own back (saber_toggle.c).
A shader played over before is not plugged again: its package copy is the
one first made, and Back gives that one back. */
static boolean shader_plug(
	long shader_tag)
{
	struct shader_model_definition *shader = (struct shader_model_definition *)shader_definition_get(shader_tag);
	struct saber_shader *played;

	if (shader_played(shader_tag))
		return TRUE;
	played = malloc(sizeof(*played));

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
	/* (the classic value lights the back of a face as the front is lit.
	An anniversary interior's faces point inward, so the sun outside
	lit the bay; the texture is already its own shade) */
	shader->model.translucency = 0.f;
	/* (the classic reflection is the classic cube, which this shader no
	longer samples. Its colour is added on the texture, and a light
	interior panel clips to white; the package's texture carries its
	own highlight) */
	shader->model.perpendicular_brightness = 0.f;
	shader->model.parallel_brightness = 0.f;
	csmemset(&shader->model.perpendicular_tint_color, 0,
		sizeof(shader->model.perpendicular_tint_color));
	csmemset(&shader->model.parallel_tint_color, 0,
		sizeof(shader->model.parallel_tint_color));
	/* (a period of zero makes the illumination fraction an infinity, and
	zero bounds times that is not zero) */
	if (shader->model.self_illumination_animation_period == 0.f)
		shader->model.self_illumination_animation_period = 1.f;
	/* (the classic scroll is the visor's. A material that shares the
	shader, the face, scrolls with it) */
	csmemset(&shader->model.texture_animation, 0, sizeof(shader->model.texture_animation));
	/* (what the package's models draw it by, for Back's switch to give
	back as they are drawn again) */
	played->played = *shader;

	return TRUE;
}

/* the package's colour-change mask of the texture `texture_name` (its
"_mpmask", else, if `banded`, its "_br"), of the most detail there is, or
FALSE */
/* the name's end of the mask mask_find found last, for the log */
static char const *mask_found_suffix = "mask";

static boolean mask_find(
	struct saber_pak *pak,
	char const *texture_name,
	boolean banded,
	struct saber_texture const *base,
	struct saber_texture *mask)
{
	struct saber_pak *paks[MAXIMUM_TEXTURE_PAKS];
	long types[MAXIMUM_TEXTURE_PAKS];
	long count;
	long suffix;
	long index;

	count = texture_paks(pak, paks, types, NULL);
	for (suffix = 0; suffix < (banded ? 2 : 1); suffix++)
	{
		char name[96];

		snprintf(name, sizeof(name), "%s%s", texture_name, suffix ? "_br" : "_mpmask");
		for (index = 0; index < count; index++)
		{
			long entry = paks[index] ? saber_pak_find(paks[index], name, types[index]) : NONE;
			unsigned char *bytes;
			unsigned long size = 0;
			boolean read;

			if (entry == NONE)
				continue;
			bytes = saber_pak_read(paks[index], entry, &size);
			read = bytes && saber_texture_read_mask(mask, bytes, size, suffix, base);
			if (bytes)
				free(bytes);
			if (read)
			{
				mask_found_suffix = suffix ? "_br" : "_mpmask";
				return TRUE;
			}
		}
	}

	return FALSE;
}

/* Puts a mask (or none) at a permutation of a multipurpose map played over,
blank masks before it where it has none; FALSE if it cannot. */
static boolean mask_set_put(
	struct saber_bitmap *set,
	long slot,
	struct saber_texture *mask)
{
	if (slot < 0 || slot >= MAXIMUM_SABER_BITMAP_TEXTURES)
		return FALSE;
	while (set->count <= slot)
	{
		struct saber_texture *blank = &set->textures[set->count];

		if (!saber_texture_blank(blank) || !bitmap_describe(blank, set->tag_index, &set->bitmaps[set->count]))
			return FALSE;
		set->count++;
	}
	if (mask)
	{
		struct bitmap_data bitmap;

		if (!bitmap_describe(mask, set->tag_index, &bitmap))
			return FALSE;
		saber_texture_dispose(&set->textures[slot]);
		set->textures[slot] = *mask;
		set->bitmaps[slot] = bitmap;
	}
	bitmap_group_get(set->tag_index)->bitmaps.count = set->count;

	return TRUE;
}

/* how many of the map's own bitmaps of the bitmap `bitmap_tag` a set of the
package's textures keeps first (saber_bitmap classic_count): the map's other
shaders that draw it draw them still (the Master Chief's lights draw his
armour's bitmap `cyborg`, and drew the package's armour as a pane) */
static long classic_bitmap_count(
	long bitmap_tag)
{
	struct bitmap_group const *group = bitmap_tag == NONE ? NULL : bitmap_group_get(bitmap_tag);

	/* (a character's alone: a Warthog's dashboard displays, which draw its
	hull's bitmap too, are drawn as they were, of the package's textures) */
	if (!group || group->bitmaps.count <= 0 || !strstr(tag_get_name(bitmap_tag), "characters\\"))
		return 0;
	return MIN(group->bitmaps.count, MAXIMUM_SABER_BITMAP_TEXTURES / 4);
}

/* whether the map's shader `shader_tag` changes colour (a grunt's, an
Elite's rank), as the map has it, before the package's models change it */
static boolean shader_changes_color(
	long shader_tag)
{
	struct saber_shader const *played;
	struct shader_model_definition const *shader;

	for (played = saber_models_globals.shaders; played; played = played->next)
	{
		if (played->tag_index == shader_tag)
		{
			return played->map_shader.model.change_color_source > 0 &&
				played->map_shader.model.multipurpose_map.index != NONE;
		}
	}
	shader = (struct shader_model_definition const *)shader_definition_get(shader_tag);
	return shader && shader->model.change_color_source > 0 && shader->model.multipurpose_map.index != NONE;
}

/* whether a template has the marine's eyepiece, which takes the last of
the usable shaders for itself */
static boolean template_has_monicle(
	struct saber_template const *template_)
{
	long index;

	for (index = 0; index < template_->material_count; index++)
	{
		if (material_scrolls(template_->materials[index]))
			return TRUE;
	}

	return FALSE;
}

/* whether the packages have a colour-change mask for the texture
`texture_name` (its "_mpmask", or if `banded` its "_br"), as mask_find
would look for it */
static boolean mask_exists(
	struct saber_pak *pak,
	char const *texture_name,
	boolean banded)
{
	struct saber_pak *paks[MAXIMUM_TEXTURE_PAKS];
	long types[MAXIMUM_TEXTURE_PAKS];
	long count = texture_paks(pak, paks, types, NULL);
	long suffix;
	long index;

	for (suffix = 0; suffix < (banded ? 2 : 1); suffix++)
	{
		char name[96];

		snprintf(name, sizeof(name), "%s%s", texture_name, suffix ? "_br" : "_mpmask");
		for (index = 0; index < count; index++)
		{
			if (paks[index] && saber_pak_find(paks[index], name, types[index]) != NONE)
				return TRUE;
		}
	}

	return FALSE;
}

/* Has a material keep its classic shader's colour change (a grunt's or an
Elite's rank), masked as the package's "_mpmask" or "_br" texture masks it:
its shader's multipurpose map the masks, each at its material's
permutation (a material with none, a mask of nothing). Nothing if the
classic shader has no colour change. (A "_br" only if `banded`, a
character's, by its skeleton: c_uplink's would mask its whole body in
scenery's black; and only where the material's texture `base` is grey: an
Elite's grey plates, not its armour baked blue or red.) */
static void material_mask_play(
	struct saber_pak *pak,
	long shader_tag,
	char const *texture_name,
	long permutation,
	boolean banded,
	struct saber_texture const *base)
{
	struct shader_model_definition *shader = (struct shader_model_definition *)shader_definition_get(shader_tag);
	struct saber_shader const *played;
	struct saber_bitmap *set;
	struct saber_texture mask;
	boolean found;
	long mask_tag;

	for (played = saber_models_globals.shaders; played && played->tag_index != shader_tag; played = played->next)
	{
	}
	if (!played || played->map_shader.model.change_color_source <= 0 ||
		played->map_shader.model.multipurpose_map.index == NONE)
	{
		if (mask_exists(pak, texture_name, banded))
			platform_log("anniversary: %s has a colour-change mask, but its shader changes no colour", texture_name);
		return;
	}
	mask_tag = played->map_shader.model.multipurpose_map.index;
	if (tag_kept(mask_tag))
		return;
	set = bitmap_played_get(mask_tag);
	/* (a bitmap that is a base map's too is left as it is) */
	if (set && !set->masks)
		return;
	found = mask_find(pak, texture_name, banded, base, &mask);
	/* (none of its own: a blank one at its place still, not the map's mask,
	whose layout is the map's texture's, which would change its colour in
	blotches over the package's: an Elite major's armour, ch_elite_officer_01) */
	if (!found)
		platform_log("anniversary: %s has no colour-change mask: drawn with none", texture_name);
	if (!set)
	{
		struct bitmap_group *group = bitmap_group_get(mask_tag);

		set = malloc(sizeof(*set));
		if (!set)
		{
			if (found)
				saber_texture_dispose(&mask);
			return;
		}
		csmemset(set, 0, sizeof(*set));
		set->tag_index = mask_tag;
		set->masks = TRUE;
		set->map_group = *group;
		/* (the map's own group given back with the map's models: a set made
		over map's models only, as the map loads) */
		set->classic_group = *group;
		group->sequences.count = 0;
		group->bitmaps.count = 0;
		group->bitmaps.address = set->bitmaps;
		group->bitmaps.definition = NULL;
		set->next = saber_models_globals.bitmaps;
		saber_models_globals.bitmaps = set;
	}
	if (!mask_set_put(set, permutation, found ? &mask : NULL))
	{
		if (found)
			saber_texture_dispose(&mask);
		return;
	}
	if (found)
		platform_log("anniversary: %s's colour change masked by its %s, %dx%d, %ld%% of it, at %ld of %s (%s's)",
			texture_name, mask_found_suffix, mask.width, mask.height, saber_texture_mask_coverage(), permutation,
			tag_get_name(mask_tag), tag_get_name(shader_tag));
	/* (the package's shader takes the mask, and only it: the map's models share
	the tag, and Back gives them their own shader back, saber_toggle.c. The
tag is written only while the templates are drawn, so that what it holds
matches what is drawn) */
	{
		struct saber_shader *plugged;

		for (plugged = saber_models_globals.shaders; played && plugged; plugged = plugged->next)
		{
			if (plugged->tag_index != shader_tag)
				continue;
			/* (both the package's copy and the tag: the copy is taken again from
			the tag after everything the models change, and Back can have put the
			map's models back before either) */
			plugged->played.model.multipurpose_map.index = mask_tag;
			plugged->played.model.change_color_source = played->map_shader.model.change_color_source;
			shader->model.multipurpose_map.index = mask_tag;
			shader->model.change_color_source = played->map_shader.model.change_color_source;
			break;
		}
	}
}

/* whether a material is a glow drawn over another's geometry, its name
that one's and "_sfx" ("ch_flood_infection_fronds_01_sfx"): a dark texture
of no alpha, additive in the remastered game, drawn solid over the fronds
it covered them black */
static boolean material_glow_overlay(
	char const *material)
{
	size_t length = strlen(material);

	return length > 4 && !strcmp(material + length - 4, "_sfx");
}

/* whether a part's material is an effect's (a muzzle flash, a light's
cone, an overshield, a weapon's counter's digits), which a model shader
would draw solid: such parts are left out */
static boolean material_effect(
	char const *material)
{
	/* (and an energy shield's: a Jackal's, whose own the map's model draws;
	a field generator's, whose the map's model draws and hides by permuting
	its region when the shield breaks. And the arc between a plasma pistol's
	or rifle's prongs, not drawn at all) */
	return !strncmp(material, "sfx_", 4) || !strncmp(material, "lig_", 4) || !strncmp(material, "fx_", 3) ||
		strstr(material, "digits") != NULL || !strncmp(material, "c_shield", 8) || !strncmp(material, "sc_white", 8) ||
		strstr(material, "shield") != NULL || strstr(material, "lightning") != NULL ||
		material_glow_overlay(material);
}

/* whether a material is a plant's leaves or branches, by its name
("tree_hr_branches_01", "grs_fern_01") */
static boolean material_foliage(
	char const *material)
{
	static char const *const words[] = { "branch", "leaf", "leaves", "grass", "grs_", "fern", "foliage", "frond",
		"palm", "vine", "flower", "shrub", "needle", "plant", "bush" };
	long index;

	for (index = 0; index < (long)NUMBEROF(words); index++)
	{
		if (strstr(material, words[index]))
			return TRUE;
	}

	return FALSE;
}

/* whether a material is a leaf card (or a far tree's flat stand-in), whose
texture's alpha is the card's shape. A trunk's bark is not: its name has
neither of these, and an empty alpha is filled in so the trunk stays */
static boolean material_card(
	char const *material)
{
	return strstr(material, "branch") != NULL || strstr(material, "leaf") != NULL ||
		strstr(material, "needle") != NULL || strstr(material, "frond") != NULL ||
		strstr(material, "foliage") != NULL || strstr(material, "farplane") != NULL;
}

/* a window (glass_dirty_01, vhc_pelican_glass_01): blended, not filled solid */
static boolean material_glass(
	char const *material)
{
	return strstr(material, "glass") != NULL;
}

/* the arc between a plasma rifle's barrels: the bolt on a black plate */
static boolean material_lightning(
	char const *material)
{
	return strstr(material, "lightning") != NULL;
}

/* whether a scrolling material of another model scrolls the shader
`shader_tag` (saber_models_globals scroll_shaders) */
static boolean scroll_shader(
	long shader_tag)
{
	long index;

	for (index = 0; index < saber_models_globals.scroll_shader_count; index++)
	{
		if (saber_models_globals.scroll_shaders[index] == shader_tag)
			return TRUE;
	}

	return FALSE;
}

/* a material whose texture scrolls, on a shader of its own: the marine's
eyepiece's lens, the arc between a plasma rifle's prongs */
static boolean material_scrolls(
	char const *material)
{
	return strstr(material, "monicle") != NULL || material_lightning(material);
}

/* a material whose alpha is its shape: a decal, dirt, a grate. A shaded
alpha on one of these stays a cutout; on a solid part it does not */
static boolean material_punched(
	char const *material)
{
	return strstr(material, "alpha") != NULL || strstr(material, "decal") != NULL ||
		strstr(material, "dirt") != NULL || strstr(material, "grate") != NULL;
}

/* the marine's green eyepiece. It hangs from the helmet heads and the plain
one, not from the capped sergeant (Marcus, Johnson), and not from the body */
static boolean material_monicle(
	struct saber_template const *template_,
	struct saber_mesh_part const *part)
{
	if (part->material_index < 0 || part->material_index >= template_->material_count)
		return FALSE;

	return strstr(template_->materials[part->material_index], "monicle") != NULL;
}

/* a capped head wears no eyepiece */
static boolean head_without_monicle(
	char const *key)
{
	return strstr(key, "marcus") != NULL || strstr(key, "johnson") != NULL;
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
	/* (and a far plane's stand-ins, a tree's flat cards of itself seen from
	afar: unless they are all it has, a sapling's leaves and trunk, in which
	case their texture's alpha cuts them out) */
	{
		boolean others = FALSE;

		for (index = 0; index < template_->part_count; index++)
		{
			if (!strstr(template_->materials[template_->parts[index].material_index], "farplane"))
				others = TRUE;
		}
		kept = 0;
		for (index = 0; index < template_->part_count; index++)
		{
			struct saber_mesh_part *part = &template_->parts[index];

			if (others && strstr(template_->materials[part->material_index], "farplane"))
			{
				free(part->vertices);
				free(part->triangles);
				continue;
			}
			template_->parts[kept++] = *part;
		}
		template_->part_count = kept;
	}
	{
		/* (at each level, every rank's and each rank's own together.
		Each head permutation is geometries of its own, apart from the
		body, so it is counted apart: counted together, a marine's eight
		heads fill one geometry and the later heads, the plain one among
		them, are let go) */
		char variants[1 + MAXIMUM_SABER_HEAD_PERMUTATIONS][64];
		long level_parts[5][1 + MAXIMUM_SABER_HEAD_PERMUTATIONS][NUMBER_OF_SABER_RANKS];
		long variant_count;
		long variant_index;

		csmemset(variants, 0, sizeof(variants));
		csmemset(level_parts, 0, sizeof(level_parts));
		variant_count = 1;
		kept = 0;
		for (index = 0; index < template_->part_count; index++)
		{
			struct saber_mesh_part *part = &template_->parts[index];
			long level = MIN(part->lod, 4);
			long rank = part->rank > 0 && part->rank < NUMBER_OF_SABER_RANKS ? part->rank : _saber_rank_all;
			long count;
			long other;

			variant_index = 0;
			if (part->variant[0])
			{
				for (variant_index = 1; variant_index < variant_count; variant_index++)
				{
					if (!strcmp(variants[variant_index], part->variant))
						break;
				}
				if (variant_index == variant_count)
				{
					if (variant_count >= (long)NUMBEROF(variants))
					{
						free(part->vertices);
						free(part->triangles);
						continue;
					}
					csstrncpy(variants[variant_count], part->variant, sizeof(variants[0]) - 1);
					variant_count++;
				}
			}
			count = level_parts[level][variant_index][_saber_rank_all];
			for (other = 1; other < NUMBER_OF_SABER_RANKS; other++)
			{
				if (rank == _saber_rank_all || other == rank)
					count = MAX(count, level_parts[level][variant_index][_saber_rank_all] +
						level_parts[level][variant_index][other]);
			}
			if (count >= MAXIMUM_PARTS_PER_MODEL_GEOMETRY)
			{
				free(part->vertices);
				free(part->triangles);
				continue;
			}
			part->rank = (short)rank;
			level_parts[level][variant_index][rank]++;
			template_->parts[kept++] = *part;
		}
		template_->part_count = kept;
	}
}

/* the template's parts less those far from the rest (let go of): a
part's middle, or a skin compound's bone, more than four times the reach of
the part that spreads furthest, from that part's middle. Measured from the
part with the most vertices, a pelican's pilot (a metre across) let the
hull go, fourteen metres off. c_uplink__h's crates sit 69 metres off its
station, past four times the station's reach */
/* whether a template's bone is under one with `word` in its name: a whip
antenna's ("pos antenna", "antenna 0".."antenna 6", a Warthog's), or a
wreck's ("_damaged" and its "damaged geom" bones, a Wraith's destroyed
hull, skinned into the compound with its whole one) */
static boolean template_bone_under(
	struct saber_template const *template_,
	long bone,
	char const *word)
{
	long depth;

	for (depth = 0; bone >= 0 && bone < template_->bone_count && depth < 64; depth++)
	{
		char const *name = template_->bones[bone];

		/* (an antenna is a whip's chain, "pos antenna" and "antenna 0".., a
		Warthog's: an Infection form's "frame antenna ..." bones hold its
		fronds and its head, which are drawn) */
		if (!strcmp(word, "antenna") ?
			(!strncmp(name, "antenna", 7) || !strncmp(name, "pos antenna", 11)) :
			strstr(name, word) != NULL)
		{
			return TRUE;
		}
		bone = template_->bone_parents[bone];
	}

	return FALSE;
}

/* Takes away the triangles of a template's whip antenna (a Warthog's, which
Anniversary's graphics do not draw), and moves its wreck's (a Wraith's)
into parts of their own, marked a wreck's: drawn in place of the whole
hull once the vehicle is destroyed. A triangle any of whose corners those
bones hold goes; the vertices stay, unused */
static void template_antenna_remove(
	struct saber_template *template_)
{
	boolean *antenna;
	boolean *wreck;
	long bone;
	long part_index;
	long part_total;
	boolean any = FALSE;

	if (template_->bone_count <= 0)
		return;
	antenna = malloc(sizeof(*antenna) * (size_t)template_->bone_count);
	wreck = malloc(sizeof(*wreck) * (size_t)template_->bone_count);
	if (!antenna || !wreck)
	{
		if (antenna)
			free(antenna);
		if (wreck)
			free(wreck);
		return;
	}
	for (bone = 0; bone < template_->bone_count; bone++)
	{
		antenna[bone] = template_bone_under(template_, bone, "antenna");
		wreck[bone] = !antenna[bone] && template_bone_under(template_, bone, "damaged");
		if (antenna[bone] || wreck[bone])
			any = TRUE;
	}
	part_total = template_->part_count;
	for (part_index = 0; any && part_index < part_total; part_index++)
	{
		struct saber_mesh_part *part = &template_->parts[part_index];
		unsigned short *wrecked = NULL;
		long wrecked_count = 0;
		long triangle;
		long kept = 0;
		long dropped_count = 0;

		if (part->wreck)
			continue;
		for (triangle = 0; triangle < part->triangle_count; triangle++)
		{
			/* 0 kept, 1 the antenna's, 2 the wreck's */
			long whose = 0;
			long corner;

			for (corner = 0; corner < 3 && whose != 1; corner++)
			{
				long index = part->triangles[3 * triangle + corner];
				struct model_vertex_uncompressed const *vertex;
				long influence;

				if (index >= part->vertex_count)
					continue;
				vertex = &part->vertices[index];
				for (influence = 0; influence < 2; influence++)
				{
					long slot = vertex->nodes[influence];

					if (vertex->node_weights[influence] > 0.5f && slot >= 0 && slot < template_->bone_count)
					{
						if (antenna[slot])
							whose = 1;
						else if (wreck[slot])
							whose = 2;
					}
				}
			}
			if (whose == 1)
			{
				dropped_count++;
				continue;
			}
			if (whose == 2)
			{
				if (!wrecked)
				{
					wrecked = malloc(sizeof(*wrecked) * 3 * (size_t)part->triangle_count);
					if (!wrecked)
						continue;
				}
				wrecked[3 * wrecked_count] = part->triangles[3 * triangle];
				wrecked[3 * wrecked_count + 1] = part->triangles[3 * triangle + 1];
				wrecked[3 * wrecked_count + 2] = part->triangles[3 * triangle + 2];
				wrecked_count++;
				continue;
			}
			part->triangles[3 * kept] = part->triangles[3 * triangle];
			part->triangles[3 * kept + 1] = part->triangles[3 * triangle + 1];
			part->triangles[3 * kept + 2] = part->triangles[3 * triangle + 2];
			kept++;
		}
		part->triangle_count = kept;
		if (dropped_count)
			platform_log("anniversary: %ld of a part's triangles are its antenna's, not drawn", dropped_count);
		if (wrecked_count)
		{
			struct saber_mesh_part *parts = realloc(template_->parts,
				sizeof(*template_->parts) * ((size_t)template_->part_count + 1));
			struct model_vertex_uncompressed *vertices;

			if (parts)
			{
				template_->parts = parts;
				part = &template_->parts[part_index];
			}
			vertices = parts ? malloc(sizeof(*vertices) * (size_t)part->vertex_count) : NULL;
			if (vertices)
			{
				struct saber_mesh_part *copy = &template_->parts[template_->part_count];

				memcpy(vertices, part->vertices, sizeof(*vertices) * (size_t)part->vertex_count);
				*copy = *part;
				copy->vertices = vertices;
				copy->triangles = wrecked;
				copy->triangle_count = wrecked_count;
				copy->layer_weights = NULL;
				copy->layer_count = 0;
				copy->wreck = TRUE;
				template_->part_count++;
				platform_log("anniversary: %ld of a part's triangles are its wreck's, drawn once it is destroyed",
					wrecked_count);
			}
			else
			{
				free(wrecked);
			}
		}
		else if (wrecked)
		{
			free(wrecked);
		}
	}
	/* (a part left with no triangles, all of them its wreck's or its
	antenna's, is taken out: a geometry part of none halts the renderer,
	count>0) */
	{
		long kept = 0;

		for (part_index = 0; part_index < template_->part_count; part_index++)
		{
			struct saber_mesh_part *part = &template_->parts[part_index];

			if (part->triangle_count <= 0)
			{
				if (part->vertices)
					free(part->vertices);
				if (part->triangles)
					free(part->triangles);
				if (part->layer_weights)
					free(part->layer_weights);
				continue;
			}
			if (kept != part_index)
				template_->parts[kept] = *part;
			kept++;
		}
		template_->part_count = kept;
	}
	free(antenna);
	free(wreck);
}

static void template_strays_remove(
	struct saber_template *template_)
{
	real_point3d middles[MAXIMUM_PARTS_PER_MODEL_GEOMETRY * 5 * NUMBER_OF_SABER_RANKS];
	real radii[MAXIMUM_PARTS_PER_MODEL_GEOMETRY * 5 * NUMBER_OF_SABER_RANKS];
	long largest = NONE;
	long kept = 0;
	long index;

	if (template_->part_count > (long)NUMBEROF(middles))
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
		if (largest == NONE || radii[index] > radii[largest])
			largest = index;
	}
	if (largest == NONE || radii[largest] <= 0.f)
		return;
	for (index = 0; index < template_->part_count; index++)
	{
		struct saber_mesh_part *part = &template_->parts[index];
		real_vector3d apart;
		real anchor_apart = 0.f;

		/* (or a skin compound's whose bone stands far off: another of the
		scene's copies, put back over the template by its bone, as
		c_uplink__h's "uplink 1" and its crates, a ghost's second hull) */
		if (part->compound)
		{
			real_point3d anchor;
			real_vector3d to_anchor;

			anchor.x = part->anchor[0];
			anchor.y = part->anchor[1];
			anchor.z = part->anchor[2];
			vector_from_points3d(&middles[largest], &anchor, &to_anchor);
			anchor_apart = magnitude3d(&to_anchor);
		}
		vector_from_points3d(&middles[largest], &middles[index], &apart);
		if (magnitude3d(&apart) > 4.f * radii[largest] || anchor_apart > 4.f * radii[largest])
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

/* a template's jaw: its "b jaw" bone and those under it (the lower lips),
which have no node of the map's model and so stood still on its head. They
are skinned to a node the model has not, the one after its last, which
render_model makes each frame its head's turned about the jaw's hinge by
the unit's mouth aperture (the sound's mouth data, unit_set_mouth_aperture) */
enum
{
	MAXIMUM_SABER_JAWS = 32,
	/* (the jaw's turn wide open; Halo's aperture is 0..1) */
};

#define SABER_JAW_OPEN_ANGLE 0.30f

static struct saber_jaw
{
	long model_tag;
	short head_node;
	short jaw_node;
	/* (in the model's default pose: the hinge, and the axis it turns about,
	opening the mouth for a positive angle) */
	real_point3d pivot;
	real_vector3d axis;
} saber_jaws[MAXIMUM_SABER_JAWS];
static long saber_jaw_count = 0;

/* whether a template's bone is its jaw or under it */
static boolean template_bone_jaw(
	struct saber_template const *template_,
	long bone)
{
	long depth;

	for (depth = 0; bone >= 0 && bone < template_->bone_count && depth < 64; depth++)
	{
		if (!strcmp(template_->bones[bone], "b jaw"))
			return TRUE;
		bone = template_->bone_parents[bone];
	}

	return FALSE;
}

/* a point (w 1) or vector (w 0) of a template's model space as a vertex of
its bone `slot`'s is placed (template_nodes_map): in that bone's own space,
scaled, then where `node` is by default */
static void template_point_placed(
	struct saber_template const *template_,
	long slot,
	struct model_node const *node,
	real scale,
	real x,
	real y,
	real z,
	real w,
	real_vector3d *placed)
{
	real local[3];

	bind_transform(template_->bone_binds[slot], x, y, z, w, local);
	if (w != 0.f)
	{
		real_point3d point;
		real_point3d moved;

		point.x = local[0] * scale;
		point.y = local[1] * scale;
		point.z = local[2] * scale;
		matrix4x3_inverse_transform_point(&node->runtime_default_inverse_matrix, &point, &moved);
		placed->i = moved.x;
		placed->j = moved.y;
		placed->k = moved.z;
	}
	else
	{
		real_vector3d direction;

		direction.i = local[0];
		direction.j = local[1];
		direction.k = local[2];
		matrix4x3_inverse_transform_vector(&node->runtime_default_inverse_matrix, &direction, placed);
	}
}

/* the jaw of a template mapped to `model` (model_tag's): its hinge and
axis, and which node it is skinned to. FALSE when it has none, or the
model's head has no node, or the model has no room for one more */
static boolean template_jaw_find(
	struct saber_template const *template_,
	struct model const *model,
	short const *nodes,
	long const *binds,
	real scale,
	struct saber_jaw *jaw)
{
	long bone;
	long jaw_bone = NONE;
	long lip_bone = NONE;
	long slot;
	struct model_node const *head;
	real_vector3d pivot;
	real_vector3d lip;
	real_vector3d axis;
	real_vector3d down;

	if (model->nodes.count + 1 >= RASTERIZER_MAXIMUM_NODES_PER_MODEL ||
		model->nodes.count + 1 > MAXIMUM_NODES_PER_MODEL)
	{
		return FALSE;
	}
	for (bone = 0; bone < template_->bone_count; bone++)
	{
		if (!strcmp(template_->bones[bone], "b jaw"))
			jaw_bone = bone;
	}
	if (jaw_bone == NONE || nodes[jaw_bone] == NONE || binds[jaw_bone] == NONE || binds[jaw_bone] == jaw_bone)
		return FALSE;
	/* (the chin: the lower lip's middle, else the jaw's farthest child) */
	for (bone = 0; bone < template_->bone_count; bone++)
	{
		if (template_->bone_parents[bone] == jaw_bone && strstr(template_->bones[bone], " c lip lower"))
		{
			lip_bone = bone;
			break;
		}
	}
	if (lip_bone == NONE)
	{
		real farthest = 0.f;

		for (bone = 0; bone < template_->bone_count; bone++)
		{
			real dx;
			real dz;

			if (template_->bone_parents[bone] != jaw_bone)
				continue;
			dx = template_->bone_origins[bone][0] - template_->bone_origins[jaw_bone][0];
			dz = template_->bone_origins[bone][2] - template_->bone_origins[jaw_bone][2];
			if (dx * dx + dz * dz > farthest)
			{
				farthest = dx * dx + dz * dz;
				lip_bone = bone;
			}
		}
	}
	if (lip_bone == NONE)
		return FALSE;
	slot = binds[jaw_bone];
	head = TAG_BLOCK_GET_ELEMENT(&model->nodes, nodes[jaw_bone], struct model_node);
	template_point_placed(template_, slot, head, scale, template_->bone_origins[jaw_bone][0],
		template_->bone_origins[jaw_bone][1], template_->bone_origins[jaw_bone][2], 1.f, &pivot);
	template_point_placed(template_, slot, head, scale, template_->bone_origins[lip_bone][0],
		template_->bone_origins[lip_bone][1], template_->bone_origins[lip_bone][2], 1.f, &lip);
	/* (the hinge's axis is across the face, the template's y; the chin goes
	down, the template's -z, as the mouth opens) */
	template_point_placed(template_, slot, head, scale, 0.f, 1.f, 0.f, 0.f, &axis);
	template_point_placed(template_, slot, head, scale, 0.f, 0.f, -1.f, 0.f, &down);
	if (normalize3d(&axis) < 0.0001f)
		return FALSE;
	{
		real_matrix4x3 turn;
		real_point3d arm;
		real_point3d swung;
		real_vector3d swing;

		/* (which way a turn by matrix4x3_rotation_from_axis_and_angle
		takes the chin: down, or the axis is turned round) */
		arm.x = lip.i - pivot.i;
		arm.y = lip.j - pivot.j;
		arm.z = lip.k - pivot.k;
		matrix4x3_rotation_from_axis_and_angle(&turn, &axis, (real)sin(0.1), (real)cos(0.1));
		matrix4x3_transform_point(&turn, &arm, &swung);
		swing.i = swung.x - arm.x;
		swing.j = swung.y - arm.y;
		swing.k = swung.z - arm.z;
		if (dot_product3d(&swing, &down) < 0.f)
		{
			axis.i = -axis.i;
			axis.j = -axis.j;
			axis.k = -axis.k;
		}
	}
	jaw->head_node = nodes[jaw_bone];
	jaw->jaw_node = (short)model->nodes.count;
	jaw->pivot.x = pivot.i;
	jaw->pivot.y = pivot.j;
	jaw->pivot.z = pivot.k;
	jaw->axis = axis;

	return TRUE;
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
	long model_tag,
	real scale,
	long *matched_bones,
	long *posed_vertices,
	long *unposed_vertices)
{
	short *nodes = malloc(sizeof(*nodes) * ((size_t)template_->bone_count + 1));
	long *binds = malloc(sizeof(*binds) * ((size_t)template_->bone_count + 1));
	boolean defaults = TRUE;
	boolean has_jaw = FALSE;
	short jaw_node = NONE;
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
	/* (its jaw, skinned to the node after the model's last) */
	{
		struct saber_jaw found;
		long at;

		has_jaw = FALSE;
		if (model_tag != NONE && defaults && template_jaw_find(template_, model, nodes, binds, scale, &found))
		{
			found.model_tag = model_tag;
			for (at = 0; at < saber_jaw_count && saber_jaws[at].model_tag != model_tag; at++)
				;
			if (at < MAXIMUM_SABER_JAWS)
			{
				saber_jaws[at] = found;
				if (at == saber_jaw_count)
					saber_jaw_count++;
				has_jaw = TRUE;
				jaw_node = found.jaw_node;
			}
		}
	}
	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part *part = &template_->parts[part_index];
		long index;

		for (index = 0; index < part->vertex_count; index++)
		{
			struct model_vertex_uncompressed *vertex = &part->vertices[index];
			boolean jawed[2] = { FALSE, FALSE };
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
				jawed[influence] = has_jaw && node != NONE && binds[slot] != slot &&
					template_bone_jaw(template_, slot) ? TRUE : FALSE;
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
				/* (a jaw's vertex is placed by its head's default, as the
				jaw's node is the head's turned) */
				for (influence = 0; influence < 2; influence++)
				{
					if (jawed[influence])
						vertex->nodes[influence] = jaw_node;
				}
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

/* the map's model's region whose permutations are the template's bones
(a boulder's "boulder", base00..base04, each its own rock, skinned into
one compound with the others): NONE when there is none */
static long permutation_variant_region(
	struct saber_template const *template_,
	struct model const *model)
{
	long region_index;

	for (region_index = 0; region_index < model->regions.count; region_index++)
	{
		struct model_region const *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
		long permutation;
		long matched = 0;

		if (region->permutations.count < 2 || !_stricmp(region->name, "head"))
			continue;
		for (permutation = 0; permutation < region->permutations.count; permutation++)
		{
			char key[64];
			long bone;

			variant_normalize(TAG_BLOCK_GET_ELEMENT(&region->permutations, permutation,
				struct model_region_permutation)->name, key, (long)sizeof(key));
			for (bone = 0; bone < template_->bone_count; bone++)
			{
				if (!strcmp(template_->bones[bone], key))
				{
					matched++;
					break;
				}
			}
		}
		if (matched == region->permutations.count)
			return region_index;
	}

	return NONE;
}

/* Has a template whose rocks are the map's permutations (a boulder's
base00..base04, skinned into one compound) draw only the one the map's
object chose: each triangle the variant of the permutation bone over its
first corner's, its part split by variant, as a marine's heads are, and
the region drawn as the head region is (saber_model_new). Drawn whole,
every rock of the five stood in the one's place */
static void template_permutation_variants(
	struct saber_template *template_,
	struct model const *model)
{
	long region_index = permutation_variant_region(template_, model);
	struct model_region const *region;
	long *variant_of;
	long bone;
	long part_total;
	long part_index;
	long split = 0;

	if (region_index == NONE || template_->bone_count <= 0)
		return;
	region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
	variant_of = malloc(sizeof(*variant_of) * (size_t)template_->bone_count);
	if (!variant_of)
		return;
	/* (each bone's permutation: its own name's, or the nearest above it) */
	for (bone = 0; bone < template_->bone_count; bone++)
	{
		long walk = bone;
		long depth;

		variant_of[bone] = NONE;
		for (depth = 0; walk >= 0 && walk < template_->bone_count && depth < 64 && variant_of[bone] == NONE; depth++)
		{
			long permutation;

			for (permutation = 0; permutation < region->permutations.count; permutation++)
			{
				char key[64];

				variant_normalize(TAG_BLOCK_GET_ELEMENT(&region->permutations, permutation,
					struct model_region_permutation)->name, key, (long)sizeof(key));
				if (!strcmp(template_->bones[walk], key))
				{
					variant_of[bone] = permutation;
					break;
				}
			}
			walk = template_->bone_parents[walk];
		}
	}
	part_total = template_->part_count;
	for (part_index = 0; part_index < part_total; part_index++)
	{
		long permutation;
		long first = NONE;

		if (template_->parts[part_index].variant[0])
			continue;
		for (permutation = 0; permutation < region->permutations.count; permutation++)
		{
			struct saber_mesh_part *part = &template_->parts[part_index];
			unsigned short *mine;
			long count = 0;
			long triangle;

			mine = malloc(sizeof(*mine) * 3 * ((size_t)part->triangle_count + 1));
			if (!mine)
				continue;
			for (triangle = 0; triangle < part->triangle_count; triangle++)
			{
				long index = part->triangles[3 * triangle];
				long slot = index < part->vertex_count ? part->vertices[index].nodes[0] : NONE;

				if (slot >= 0 && slot < template_->bone_count && variant_of[slot] == permutation)
				{
					mine[3 * count] = part->triangles[3 * triangle];
					mine[3 * count + 1] = part->triangles[3 * triangle + 1];
					mine[3 * count + 2] = part->triangles[3 * triangle + 2];
					count++;
				}
			}
			if (!count)
			{
				free(mine);
				continue;
			}
			{
				struct saber_mesh_part *parts = realloc(template_->parts,
					sizeof(*template_->parts) * ((size_t)template_->part_count + 1));
				struct model_vertex_uncompressed *vertices;

				if (!parts)
				{
					free(mine);
					continue;
				}
				template_->parts = parts;
				part = &template_->parts[part_index];
				vertices = malloc(sizeof(*vertices) * (size_t)part->vertex_count);
				if (!vertices)
				{
					free(mine);
					continue;
				}
				memcpy(vertices, part->vertices, sizeof(*vertices) * (size_t)part->vertex_count);
				template_->parts[template_->part_count] = *part;
				template_->parts[template_->part_count].vertices = vertices;
				template_->parts[template_->part_count].triangles = mine;
				template_->parts[template_->part_count].triangle_count = count;
				template_->parts[template_->part_count].layer_weights = NULL;
				template_->parts[template_->part_count].layer_count = 0;
				variant_normalize(TAG_BLOCK_GET_ELEMENT(&region->permutations, permutation,
					struct model_region_permutation)->name, template_->parts[template_->part_count].variant,
					(long)sizeof(template_->parts[template_->part_count].variant));
				template_->part_count++;
				if (first == NONE)
					first = permutation;
				split++;
			}
		}
		/* (what is left, of no permutation, stays the body; a part all of
		whose triangles went is emptied, then taken out) */
		if (first != NONE)
		{
			struct saber_mesh_part *part = &template_->parts[part_index];
			long kept = 0;
			long triangle;

			for (triangle = 0; triangle < part->triangle_count; triangle++)
			{
				long index = part->triangles[3 * triangle];
				long slot = index < part->vertex_count ? part->vertices[index].nodes[0] : NONE;

				if (slot >= 0 && slot < template_->bone_count && variant_of[slot] != NONE)
					continue;
				part->triangles[3 * kept] = part->triangles[3 * triangle];
				part->triangles[3 * kept + 1] = part->triangles[3 * triangle + 1];
				part->triangles[3 * kept + 2] = part->triangles[3 * triangle + 2];
				kept++;
			}
			part->triangle_count = kept;
		}
	}
	free(variant_of);
	{
		long kept = 0;

		for (part_index = 0; part_index < template_->part_count; part_index++)
		{
			struct saber_mesh_part *part = &template_->parts[part_index];

			if (part->triangle_count <= 0)
			{
				if (part->vertices)
					free(part->vertices);
				if (part->triangles)
					free(part->triangles);
				if (part->layer_weights)
					free(part->layer_weights);
				continue;
			}
			if (kept != part_index)
				template_->parts[kept] = *part;
			kept++;
		}
		template_->part_count = kept;
	}
	if (split)
		platform_log("anniversary: the template's %ld parts of the map's %s permutations, each drawn as its own",
			split, region->name);
}

/* Turns a vehicle's floors under its own roof away from the sky: a
Pelican's bay. Halo lights a model by its normals alone, shadowing none of
it, so the bay's floor, facing up, took the sun as the hull's top does and
stood out bright and flat in the dark bay. A vertex facing up with the
model's own geometry straight above it (of its level of detail) is turned
mostly sideways, keeping its lean across, so it takes the ambient light the
walls do. Its texture's shading is its own */
static void template_covered_floors_shade(
	struct saber_template *template_)
{
	long part_index;
	long turned = 0;

	for (part_index = 0; part_index < template_->part_count; part_index++)
	{
		struct saber_mesh_part *part = &template_->parts[part_index];
		long vertex;

		for (vertex = 0; vertex < part->vertex_count; vertex++)
		{
			struct model_vertex_uncompressed *point = &part->vertices[vertex];
			real px = point->position.x;
			real py = point->position.y;
			real pz = point->position.z;
			boolean covered = FALSE;
			long other_index;

			if (point->normal.k < 0.7f)
				continue;
			for (other_index = 0; other_index < template_->part_count && !covered; other_index++)
			{
				struct saber_mesh_part const *other = &template_->parts[other_index];
				long triangle;

				if (other->lod != part->lod)
					continue;
				for (triangle = 0; triangle < other->triangle_count && !covered; triangle++)
				{
					real_point3d const *a = &other->vertices[other->triangles[3 * triangle]].position;
					real_point3d const *b = &other->vertices[other->triangles[3 * triangle + 1]].position;
					real_point3d const *c = &other->vertices[other->triangles[3 * triangle + 2]].position;
					real d0;
					real d1;
					real d2;
					real area;
					real height;

					/* (above it, by its box first) */
					if (a->z <= pz + 0.05f && b->z <= pz + 0.05f && c->z <= pz + 0.05f)
						continue;
					if ((a->x < px && b->x < px && c->x < px) || (a->x > px && b->x > px && c->x > px) ||
						(a->y < py && b->y < py && c->y < py) || (a->y > py && b->y > py && c->y > py))
					{
						continue;
					}
					/* (the point under the triangle, by its corners' weights
					across) */
					area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
					if (area > -1e-8f && area < 1e-8f)
						continue;
					d0 = ((b->x - px) * (c->y - py) - (c->x - px) * (b->y - py)) / area;
					d1 = ((c->x - px) * (a->y - py) - (a->x - px) * (c->y - py)) / area;
					d2 = 1.f - d0 - d1;
					if (d0 < 0.f || d1 < 0.f || d2 < 0.f)
						continue;
					height = d0 * a->z + d1 * b->z + d2 * c->z;
					if (height > pz + 0.05f)
						covered = TRUE;
				}
			}
			if (covered)
			{
				real across_x = point->normal.i;
				real across_y = point->normal.j;
				real length = (real)sqrt((double)(across_x * across_x + across_y * across_y));
				real_vector3d normal;

				if (length < 0.05f)
				{
					across_x = 1.f;
					across_y = 0.f;
					length = 1.f;
				}
				normal.i = across_x / length;
				normal.j = across_y / length;
				normal.k = 0.35f;
				normalize3d(&normal);
				point->normal = normal;
				turned++;
			}
		}
	}
	if (turned)
		platform_log("anniversary: %ld vertices facing up under the model's roof turned from the sky", turned);
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

/* a model's tag is a weapon's ("weapons\\pistol\\pistol",
"weapons\\pistol\\fp\\fp") */
static boolean weapon_model(
	char const *name)
{
	while (name && *name)
	{
		if (!_strnicmp(name, "weapons\\", 8) || !_strnicmp(name, "weapons/", 8))
			return TRUE;
		name++;
	}

	return FALSE;
}

/* whether a model of the map's is drawn as a template with the structure
the map's own: a weapon's, a vehicle's, a character's or a powerup's (its
tag's top folder), or scenery named after a vehicle */
static boolean model_is_remastered_without_structure(
	char const *model_name)
{
	static char const *const folders[] = { "weapons\\", "vehicles\\", "characters\\", "powerups\\" };
	static char const *const vehicles[] = { "pelican", "banshee", "ghost", "warthog", "scorpion", "wraith",
		"dropship", "lifepod", "shade", "c_gun_turret", "gun turret" };
	char lower[256];
	long index;

	for (index = 0; model_name[index] && index < (long)sizeof(lower) - 1; index++)
	{
		char c = model_name[index];

		lower[index] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c == '/' ? '\\' : c;
	}
	lower[index] = 0;
	for (index = 0; index < (long)NUMBEROF(folders); index++)
	{
		if (!strncmp(lower, folders[index], strlen(folders[index])))
			return TRUE;
	}
	for (index = 0; index < (long)NUMBEROF(vehicles); index++)
	{
		if (strstr(lower, vehicles[index]))
			return TRUE;
	}

	return FALSE;
}

/* the package's template of a model of the map, by the model's name: its
last folder (or, for a first-person model, its folder's, "_fp"), lower
case with underscores for spaces, as it is or followed by "__h" or another
"__" kind, or by the map's name and those ("light_fixture_floor1_b30__h").
The Collection's first-person weapon is the weapon's own template (pistol,
not pistol_fp). The first that has a mesh, read (*template_name its name);
FALSE when none has */
static boolean template_find_named(
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
	pistol_fp, or pistol in the Collection's packages) */
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
		base[length] = 0;
		/* (a character's hands are cyborg_fp either way; a 360 weapon's
		first-person model is pistol_fp, and the Collection's is pistol) */
		if (saber_pak_xbox360(pak) || !weapon_model(model_name))
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
	/* (with no remastered structure, only the weapons', the vehicles', the
	characters' and the powerups' models are drawn as templates, and scenery of a vehicle (a
	crashed Pelican or Banshee, a lifepod, a dropship): the rest, scenery and
	machines, stays the map's, as its structure is) */
	if (!config_boolean("display.remastered_bsp") && !model_is_remastered_without_structure(model_name))
		return FALSE;
	/* (the overshield and the active camouflage stay the map's: their
	templates' cases and cores are drawn by shaders a model shader does not
	draw. Of no template here, their shaders are kept as the map's, and no
	other model borrows them) */
	if (!strcmp(base, "over_shield") || !strncmp(base, "active_camo", 11))
		return FALSE;

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
				if (!read)
					platform_log("anniversary: %s's template %s cannot be read: %s", model_name, name,
						bytes ? reason : "its entry cannot be read");
				if (read)
				{
					template_effects_remove(template_);
					template_strays_remove(template_);
					template_antenna_remove(template_);
					if (template_->part_count > 0)
					{
						snprintf(template_name, template_name_size, "%s", name);
						return TRUE;
					}
					platform_log("anniversary: %s's template %s has no part left to draw", model_name, name);
				}
				saber_template_dispose(template_);
			}
		}
	}

	return FALSE;
}

/* template_find_named by the map's name, else by the level its tag's in
("levels\\a10\\devices\\h oxy tank\\h oxy tank" a10's: d40 has a10's oxy tank,
its template h_oxy_tank_a10__h) */
static boolean template_find(
	struct saber_pak *pak,
	char const *model_name,
	char const *map_name,
	struct saber_template *template_,
	char *template_name,
	size_t template_name_size)
{
	char level[64];
	char const *at = model_name;
	size_t length = 0;

	if (template_find_named(pak, model_name, map_name, template_, template_name, template_name_size))
		return TRUE;
	while (*at && _strnicmp(at, "levels\\", 7))
	{
		at = strchr(at, '\\');
		at = at ? at + 1 : "";
	}
	if (!*at)
		return FALSE;
	at += 7;
	while (at[length] && at[length] != '\\' && length < sizeof(level) - 1)
	{
		level[length] = at[length] >= 'A' && at[length] <= 'Z' ? (char)(at[length] - 'A' + 'a') : at[length];
		length++;
	}
	level[length] = 0;
	if (!length || at[length] != '\\' || !_stricmp(level, map_name))
		return FALSE;

	return template_find_named(pak, model_name, level, template_, template_name, template_name_size);
}

/* The ranks' meshes of the template's "_special" template (an Elite's spec
ops', stealth's and zealots') posed as the template is, put among its own:
those of a rank it has none of. */
static void template_specials_merge(
	struct saber_pak *pak,
	struct saber_template *template_,
	char const *template_name,
	struct model const *model,
	real scale)
{
	struct saber_template special;
	char name[160];
	long entry;
	unsigned long size = 0;
	unsigned char *bytes;
	char const *reason;
	boolean read;
	long matched = 0;
	long posed = 0;
	long unposed = 0;
	long merged = 0;
	long index;
	/* (the ranks the template has of its own) */
	boolean had[NUMBER_OF_SABER_RANKS];

	csmemset(had, 0, sizeof(had));
	for (index = 0; index < template_->part_count; index++)
	{
		if (template_->parts[index].rank > 0 && template_->parts[index].rank < NUMBER_OF_SABER_RANKS)
			had[template_->parts[index].rank] = TRUE;
	}
	snprintf(name, sizeof(name), "%s_special", template_name);
	entry = saber_pak_find(pak, name, _saber_entry_templates);
	if (entry == NONE)
		return;
	bytes = saber_pak_read(pak, entry, &size);
	csmemset(&special, 0, sizeof(special));
	read = bytes && saber_template_read(&special, bytes, size, saber_pak_xbox360(pak), &reason);
	if (bytes)
		free(bytes);
	if (!read)
	{
		saber_template_dispose(&special);
		return;
	}
	template_effects_remove(&special);
	/* (by its own skeleton, else the template's scale) */
	skeleton_scale(&special, model, &scale);
	template_nodes_map(&special, model, NONE, scale, &matched, &posed, &unposed);
	parts_face_halo(&special);
	for (index = 0; index < special.part_count; index++)
	{
		struct saber_mesh_part *part = &special.parts[index];
		struct saber_mesh_part *parts;
		long material;

		if (part->rank == _saber_rank_all || part->material_index < 0 || part->material_index >= special.material_count)
			continue;
		if (part->rank < 0 || part->rank >= NUMBER_OF_SABER_RANKS || had[part->rank])
			continue;
		for (material = 0; material < template_->material_count &&
			strcmp(template_->materials[material], special.materials[part->material_index]); material++)
		{
		}
		if (material == template_->material_count)
		{
			char (*materials)[64];

			if (template_->material_count >= 255)
				continue;
			materials = realloc(template_->materials, sizeof(*materials) * ((size_t)template_->material_count + 1));
			if (!materials)
				continue;
			template_->materials = materials;
			memcpy(materials[material], special.materials[part->material_index], sizeof(materials[material]));
			template_->material_count++;
		}
		parts = realloc(template_->parts, sizeof(*parts) * ((size_t)template_->part_count + 1));
		if (!parts)
			continue;
		template_->parts = parts;
		parts[template_->part_count] = *part;
		parts[template_->part_count].material_index = (short)material;
		template_->part_count++;
		part->vertices = NULL;
		part->triangles = NULL;
		merged++;
	}
	if (merged)
		platform_log("anniversary: %s's ranks with %ld parts of %s", template_name, merged, name);
	saber_template_dispose(&special);
}

/* whether a shader or bitmap is one a model drawn as it is draws with */
static boolean tag_kept(
	long tag_index)
{
	long index;

	for (index = 0; index < saber_models_globals.kept_count; index++)
	{
		if (saber_models_globals.kept_tags[index] == tag_index)
			return TRUE;
	}

	return FALSE;
}

static void tag_keep(
	long tag_index)
{
	long *grown;

	if (tag_index == NONE || tag_kept(tag_index))
		return;
	grown = realloc(saber_models_globals.kept_tags, sizeof(*grown) * ((size_t)saber_models_globals.kept_count + 1));
	if (!grown)
		return;
	saber_models_globals.kept_tags = grown;
	grown[saber_models_globals.kept_count++] = tag_index;
}

/* Keeps the shaders and bitmaps of the map's models that the package has
no template of, so that a model drawn as a template does not play over
what they draw with. */
static void classic_models_keep(
	struct saber_pak *pak,
	char const *map_name)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, MODELS_GROUP_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct model const *model = model_definition_get(tag_index);
		struct saber_template template_;
		char template_name[128];
		short index;

		csmemset(&template_, 0, sizeof(template_));
		if (template_find(pak, tag_get_name(tag_index), map_name, &template_, template_name, sizeof(template_name)))
		{
			saber_template_dispose(&template_);
			continue;
		}
		for (index = 0; index < model->shaders.count; index++)
		{
			long shader_tag = TAG_BLOCK_GET_ELEMENT(&model->shaders, index, struct model_shader_reference)->shader.index;
			struct shader const *shader = shader_tag == NONE ? NULL : shader_definition_get(shader_tag);

			if (!shader)
				continue;
			tag_keep(shader_tag);
			if (shader->base.type == SHADER_TYPE_MODEL)
			{
				struct shader_model_definition const *model_shader = (struct shader_model_definition const *)shader;

				tag_keep(model_shader->model.base_map.index);
				tag_keep(model_shader->model.multipurpose_map.index);
			}
		}
	}
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
	boolean material_blends[MAXIMUM_SABER_MATERIALS];
	/* (a sheet with cards on its clear corner: the corner's rectangle) */
	boolean material_sheets[MAXIMUM_SABER_MATERIALS];
	real material_sheet_bounds[MAXIMUM_SABER_MATERIALS][4];
	struct saber_texture *material_textures[MAXIMUM_SABER_MATERIALS];
	struct bitmap_data *material_bitmap_data[MAXIMUM_SABER_MATERIALS];
	long usable_count = 0;
	long material_count = 0;
	long shader_count = 0;
	/* the eyepiece's shader, which no other material uses, so its scroll
	does not move the face */
	long monicle_shader_tag = NONE;
	/* (the assault rifle's display's one colour, once found, a 565 colour) */
	long display_color = -1;
	/* (the plasma rifle's arc's shader, which scrolls, NONE for none) */
	long lightning_shader_tag = NONE;
	/* (a model shader of another's, for a model of none: BORROWED_SHADER_INDEX) */
	long borrowed_shader = NONE;
	real scale;
	boolean skinned = FALSE;
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
	/* (the renderer skins a part of a map's model by the model's nodes, at
	most so many) */
	reason = "the map's model has more nodes than its parts are drawn with";
	if (definition->nodes.count <= 0 || definition->nodes.count >= RASTERIZER_MAXIMUM_NODES_PER_MODEL)
		goto refused;
	/* (a skinned template's by its skeleton, else by its size) */
	if (skeleton_scale(&template_, definition, &scale))
	{
		skinned = TRUE;
		platform_log("anniversary: %s's units are %f world units, by its skeleton", template_name, scale);
	}
	else
	{
		scale = template_scale(&template_, template_name, definition);
	}
	template_permutation_variants(&template_, definition);
	template_nodes_map(&template_, definition, tag_index, scale, &matched_bones, &posed_vertices, &unposed_vertices);
	parts_face_halo(&template_);
	if (strstr(template_name, "pelican"))
		template_covered_floors_shade(&template_);
	template_specials_merge(pak, &template_, template_name, definition, scale);
	shader_indices = malloc(sizeof(*shader_indices) * ((size_t)template_.part_count + 1));
	reason = "out of memory";
	if (!shader_indices)
		goto refused;
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
				long shader_tag = play_shader_tag(definition, borrowed_shader, (short)index);
				struct shader_model_definition const *shader;
				boolean decal;
				boolean lit;
				long other;

				if (shader_tag == NONE)
					continue;
				shader = (struct shader_model_definition const *)shader_definition_get(shader_tag);
				/* (a visor is a transparent shader: left as it is, so its
				scroll is not cleared with a model shader's) */
				if (((struct shader const *)shader)->base.type != SHADER_TYPE_MODEL)
					continue;
				/* (nor one a model drawn as it is draws with, nor its base map) */
				if (shader->model.base_map.index == NONE || tag_kept(shader_tag) || tag_kept(shader->model.base_map.index))
					continue;
				decal = TEST_FLAG(shader->model.flags, SHADER_MODEL_ALPHA_BLENDED_DECAL_BIT);
				lit = shader_self_illuminated(shader);
				if (pass != (decal ? 2 : lit ? 1 : 0))
					continue;
				for (other = 0; other < usable_count &&
					play_shader_tag(definition, borrowed_shader, usable[other]) != shader_tag; other++)
				{
				}
				if (other == usable_count)
					usable[usable_count++] = (short)index;
			}
		}
	}
	/* (a model of no model shader, of the structure's (an a10 barricade):
	another's) */
	if (!usable_count && (borrowed_shader = borrowed_model_shader()) != NONE)
	{
		usable[usable_count++] = BORROWED_SHADER_INDEX;
		platform_log("anniversary: %s has no model shader: drawn with %s", tag_get_name(tag_index),
			tag_get_name(borrowed_shader));
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
		/* (the eyepiece takes the last shader for itself, as the arc between
		a plasma rifle's prongs does. Another material that would have taken
		it takes one of the rest, so the scroll is the lens's, or the arc's,
		alone) */
		material_shaders[place] = usable[place % usable_count];
		if (usable_count > 1 && material_scrolls(template_.materials[material]))
		{
			long turn;

			material_shaders[place] = usable[usable_count - 1];
			/* (the one another model's arc scrolls already, if this model has
			it: the first-person rifle shares the third-person's shaders) */
			for (turn = 0; turn < usable_count; turn++)
			{
				if (scroll_shader(play_shader_tag(definition, borrowed_shader, usable[turn])))
				{
					material_shaders[place] = usable[turn];
					break;
				}
			}
		}
		else if (usable_count > 1 && material_shaders[place] == usable[usable_count - 1])
			material_shaders[place] = usable[place % (usable_count - 1)];
		/* (and another material not on one another model's arc scrolls) */
		if (usable_count > 1 && !material_scrolls(template_.materials[material]) &&
			scroll_shader(play_shader_tag(definition, borrowed_shader, material_shaders[place])))
		{
			long turn;

			for (turn = 0; turn < usable_count; turn++)
			{
				short candidate = usable[(place + turn) % usable_count];

				if (!scroll_shader(play_shader_tag(definition, borrowed_shader, candidate)) &&
					(candidate != usable[usable_count - 1] || !template_has_monicle(&template_)))
				{
					material_shaders[place] = candidate;
					break;
				}
			}
		}
		/* (a material with a colour-change mask, an Elite's body, takes a
		shader that changes colour, if the map's model has one: the shaders
		are dealt out in turn, and its own may not, which would leave its
		grey plates white for every rank) */
		if (!material_scrolls(template_.materials[material]) &&
			!shader_changes_color(play_shader_tag(definition, borrowed_shader, material_shaders[place])) &&
			mask_exists(pak, template_.materials[material], skinned))
		{
			long turn;

			for (turn = 0; turn < usable_count; turn++)
			{
				short candidate = usable[(place + turn) % usable_count];

				if (shader_changes_color(play_shader_tag(definition, borrowed_shader, candidate)) &&
					(usable_count <= 1 || candidate != usable[usable_count - 1] || !template_has_monicle(&template_)))
				{
					material_shaders[place] = candidate;
					break;
				}
			}
		}
		shader_tag = play_shader_tag(definition, borrowed_shader, material_shaders[place]);
		if (usable_count > 1 && strstr(template_.materials[material], "monicle"))
			monicle_shader_tag = shader_tag;
		if (usable_count > 1 && material_lightning(template_.materials[material]))
		{
			lightning_shader_tag = shader_tag;
			if (!scroll_shader(shader_tag) &&
				saber_models_globals.scroll_shader_count < (long)NUMBEROF(saber_models_globals.scroll_shaders))
			{
				saber_models_globals.scroll_shaders[saber_models_globals.scroll_shader_count++] = shader_tag;
			}
		}
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
		material_blends[place] = FALSE;
		if (played && other < played->count)
		{
			material_permutations[place] = (short)other;
			material_cutouts[place] = played->cutouts[other] ||
				material_card(template_.materials[material]) ||
				strstr(template_.materials[material], "decal") != NULL ? TRUE : FALSE;
			material_blends[place] = played->blends[other] ||
				material_glass(template_.materials[material]) ||
				material_lightning(template_.materials[material]) ? TRUE : FALSE;
			continue;
		}
		existing = played ? played->count : classic_bitmap_count(bitmap_tag);
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
		/* (the bay's plating is near white on the ceiling and the floor, and
		both clip. Two fifths brings it down to the hull's grey) */
		if (strstr(template_.materials[material], "pelican") &&
			strstr(template_.materials[material], "plating"))
			saber_texture_darken(material_textures[place], 2, 5);
		/* (the plasma pistol's own heat ring is drawn black: the classic meter
		lies over it, and the ring's cyan would show around it) */
		if (strstr(template_.materials[material], "plasmapistol_02"))
			saber_texture_blacken(material_textures[place]);
		/* (the Pelican's cockpit glass, half as solid: from inside, through
		two panes of it, it was too dark to see through) */
		if (strstr(template_.materials[material], "pelican") && material_glass(template_.materials[material]))
			saber_texture_scale_alpha(material_textures[place], 1, 2);
		/* (the needler's needles: grey in their texture, the remastered
		game's shader tints them violet) */
		if (!strcmp(template_.materials[material], "h_needle"))
		{
			real const pink[3] = { 1.2f, 0.55f, 1.75f };

			saber_texture_tint(material_textures[place], pink);
		}
		/* (the assault rifle's display, its own counter's marks under the
		classic counter laid on it: all its background's blue. And the
		third person's display over it, in the same place: the same blue, or
		the two flicker through each other) */
		if (!strcmp(template_.materials[material], "wpn_assault_display") ||
			!strcmp(template_.materials[material], "wpn_assault_display_3p_back_01"))
		{
			saber_texture_flatten(material_textures[place], &display_color);
		}
		/* (the eyepiece's mask is nearly black. Green, the lens is the
		display and the marks on it are the brighter part) */
		if (strstr(template_.materials[material], "monicle"))
			saber_texture_monicle(material_textures[place]);
		/* (the rear meters are white. Blue, as the gauge beside them; the
		black of the plate stays the window) */
		if (strstr(template_.materials[material], "holo"))
			saber_texture_holo(material_textures[place]);
		material_cutouts[place] = saber_texture_cutout(material_textures[place],
			material_foliage(template_.materials[material])) ? TRUE : FALSE;
		/* (a leaf card stays cut out by its alpha even when too little of it
		is solid for the test. Filling that alpha in draws the card's
		background, the whole quad) */
		if (!material_cutouts[place] && material_card(template_.materials[material]))
			material_cutouts[place] = TRUE;
		/* (and a decal sheet's: a marine's UNSC patch and rank chevron are
		small marks on a clear card, too little of it solid for the test.
		Filled in, the patch drew its whole dark card) */
		if (!material_cutouts[place] && strstr(template_.materials[material], "decal") != NULL)
			material_cutouts[place] = TRUE;
		/* (the pelican's seats and bay: the texture is a shade, clear and
		solid enough to be taken for a cutout, and testing it discards most
		of the mesh. Filled in, the metal stays. A leaf, a decal, dirt or a
		grate keeps the holes) */
		if (material_cutouts[place] &&
			!material_card(template_.materials[material]) &&
			!material_foliage(template_.materials[material]) &&
			!material_punched(template_.materials[material]) &&
			saber_texture_shaded_alpha(material_textures[place]))
		{
			material_cutouts[place] = FALSE;
		}
		/* (a window keeps its alpha and is blended. Filling it in draws the
		pane solid, and the map's glass over it. The arc is a bolt on black
		with no alpha: the black is punched and the bolt is blended, so the
		plate between the barrels does not draw) */
		if (material_glass(template_.materials[material]) ||
			material_lightning(template_.materials[material]))
			material_blends[place] = TRUE;
		/* (the Pelican's cockpit glass is plotted dark and opaque: its alpha
		keeps a fifth of itself, so the bay shows through it. A Warthog's
		windscreen is plotted as a window already, and keeps its alpha) */
		if (strstr(template_.materials[material], "pelican") &&
			material_glass(template_.materials[material]))
			saber_texture_glass(material_textures[place]);
		if (material_lightning(template_.materials[material]))
			saber_texture_punch_black(material_textures[place]);
		/* (the plasma rifle's gauge is a black pane the classic meter is laid
		over: its black gone, its glyph left under the meter's) */
		if (strstr(template_.materials[material], "plasma") && strstr(template_.materials[material], "gauge"))
		{
			material_blends[place] = TRUE;
			saber_texture_punch_black(material_textures[place]);
		}
		/* (a cut-out card's smaller levels, which the package may hold with
little or none of its alpha clear: the tree's root cards drawn whole and
grey a few steps off, their first level a clean mask. Its levels from the
first whose clear is under half the first's are left off) */
		if (material_cutouts[place] && material_textures[place]->mipmap_count > 0)
		{
			long levels[16];
			long count = saber_texture_alpha_levels(material_textures[place], levels, 16);
			long level;
			char text[160];

			text[0] = 0;
			for (level = 0; level < count; level++)
				snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%ld%%", level ? " " : "", levels[level]);
			/* (and a sheet solid but for a few cut holes, a marine's body,
			ch_marine_01, 1% clear: its smaller levels are none clear, so a
			few steps off the rolled sleeves were drawn whole over the bare
			arms under them, two arms) */
			for (level = 1; level < count && levels[0] >= 1; level++)
			{
				if (levels[level] * 2 < levels[0])
					break;
			}
			if (count > 0 && levels[0] >= 1 && level < count)
			{
				material_textures[place]->mipmap_count = (short)(level - 1);
				material_bitmap_data[place]->mipmap_count = (short)(level - 1);
				material_bitmap_data[place]->pixels_size = bitmap_get_pixel_data_size(material_bitmap_data[place]);
				platform_log("anniversary: %s is cut out by its first %ld levels (clear by level: %s)",
					template_.materials[material], level, text);
			}
			else
			{
				platform_log("anniversary: %s is cut out (clear by level: %s)", template_.materials[material], text);
			}
		}
		/* (a glyph, one colour, its picture its alpha: the plasma grenade's
		green one, blended on it, not a green square. Its first level alone:
		its smaller ones' alpha was drawn half solid) */
		if (saber_texture_alpha_glyph(material_textures[place]))
		{
			long levels[8];
			long level;
			long count = saber_texture_alpha_levels(material_textures[place], levels, 8);
			char text[96];

			text[0] = 0;
			for (level = 0; level < count; level++)
				snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%ld%%", level ? " " : "", levels[level]);
			material_cutouts[place] = FALSE;
			material_blends[place] = TRUE;
			material_textures[place]->mipmap_count = 0;
			material_bitmap_data[place]->mipmap_count = 0;
			material_bitmap_data[place]->pixels_size = bitmap_get_pixel_data_size(material_bitmap_data[place]);
			platform_log("anniversary: %s is a glyph in its alpha: blended on it, its first level alone (clear by level: %s)",
				template_.materials[material], text);
		}
		else if (strstr(template_.materials[material], "grenade") && !material_cutouts[place] &&
			!material_blends[place])
		{
			platform_log("anniversary: %s (format %d) drawn solid", template_.materials[material],
				(int)material_textures[place]->format);
		}
		/* (every material's: why a card is drawn solid) */
		{
			long clear = 0;
			long solid = 0;
			long texels = 0;

			saber_texture_alpha_counts(material_textures[place], material_foliage(template_.materials[material]),
				&clear, &solid, &texels);
			platform_log("anniversary: %s of %s: %ld%% clear, %ld%% solid%s", template_.materials[material], template_name,
				texels ? clear * 100 / texels : 0, texels ? solid * 100 / texels : 0,
				material_foliage(template_.materials[material]) ? " (foliage)" : "");
		}
		platform_log("anniversary: %s (format %d, %dx%d) cut out %d, blended %d, its texture %d", template_.materials[material],
				(int)material_textures[place]->format, (int)material_textures[place]->width,
				(int)material_textures[place]->height, (int)material_cutouts[place], (int)material_blends[place],
				(int)material_permutations[place]);
		/* (a sheet solid but for a corner of hard-edged holes, whose cards
		are drawn of it: a marine's armour's eagle and UNSC, ch_marine_01's
		corner. Its alpha kept and its parts' triangles on the corner split
		into cards that are cut out, the sheet's other triangles drawn solid:
		cut whole, its sleeves showed the arms under them through the
		lettering, and solid, the patches were dark squares) */
		material_sheets[place] = FALSE;
		if (!material_cutouts[place] && !material_blends[place])
		{
			long clear = 0;
			long solid = 0;
			long texels = 0;
			real bounds[4];

			if (saber_texture_alpha_counts(material_textures[place], FALSE, &clear, &solid, &texels) && texels &&
				clear * 300 >= texels && clear * 10 < texels && solid * 100 >= texels * 85 &&
				saber_texture_clear_bounds(material_textures[place], &bounds[0], &bounds[1], &bounds[2], &bounds[3]) &&
				(bounds[2] - bounds[0]) * (bounds[3] - bounds[1]) < 0.1f)
			{
				material_sheets[place] = TRUE;
				material_cutouts[place] = TRUE;
				memcpy(material_sheet_bounds[place], bounds, sizeof(bounds));
				platform_log("anniversary: %s's clear corner %.2f..%.2f, %.2f..%.2f: its cards cut out",
					template_.materials[material], bounds[0], bounds[2], bounds[1], bounds[3]);
			}
		}
		/* (one that is not cut out drawn whole, though its shader be alpha
		tested for another's) */
		if (!material_cutouts[place] && !material_blends[place])
			saber_texture_make_solid(material_textures[place]);
	}
	/* (each sheet's cards split from its parts) */
	{
		long count = template_.part_count;
		long split;

		for (split = 0; split < count; split++)
		{
			short material = template_.parts[split].material_index;
			long place = material >= 0 && material < 256 ? material_places[material] : NONE;
			real const *bounds;
			struct saber_mesh_part *sheet;
			struct saber_mesh_part *grown;
			short *grown_indices;
			long inside = 0;
			long triangle;

			if (place == NONE || !material_sheets[place])
				continue;
			bounds = material_sheet_bounds[place];
			sheet = &template_.parts[split];
			sheet->card = 2;
			for (triangle = 0; triangle < sheet->triangle_count; triangle++)
			{
				long corner;
				boolean all = TRUE;

				for (corner = 0; corner < 3 && all; corner++)
				{
					struct model_vertex_uncompressed const *vertex = &sheet->vertices[sheet->triangles[3 * triangle + corner]];

					all = vertex->texcoord.x >= bounds[0] - 0.004f && vertex->texcoord.x <= bounds[2] + 0.004f &&
						vertex->texcoord.y >= bounds[1] - 0.004f && vertex->texcoord.y <= bounds[3] + 0.004f;
				}
				inside += all;
			}
			if (!inside || inside == sheet->triangle_count)
			{
				if (inside)
					sheet->card = 1;
				continue;
			}
			grown = realloc(template_.parts, sizeof(*grown) * ((size_t)template_.part_count + 1));
			if (!grown)
				continue;
			template_.parts = grown;
			grown_indices = realloc(shader_indices, sizeof(*shader_indices) * ((size_t)template_.part_count + 2));
			if (!grown_indices)
				continue;
			shader_indices = grown_indices;
			sheet = &template_.parts[split];
			{
				struct saber_mesh_part *card = &template_.parts[template_.part_count];
				long kept = 0;
				long taken = 0;

				*card = *sheet;
				card->card = 1;
				card->layer_weights = NULL;
				card->vertex_alpha = NULL;
				card->vertices = malloc(sizeof(*card->vertices) * (size_t)sheet->vertex_count);
				card->triangles = malloc(sizeof(*card->triangles) * 3 * (size_t)inside);
				if (!card->vertices || !card->triangles)
				{
					if (card->vertices)
						free(card->vertices);
					if (card->triangles)
						free(card->triangles);
					continue;
				}
				memcpy(card->vertices, sheet->vertices, sizeof(*card->vertices) * (size_t)sheet->vertex_count);
				for (triangle = 0; triangle < sheet->triangle_count; triangle++)
				{
					long corner;
					boolean all = TRUE;

					for (corner = 0; corner < 3 && all; corner++)
					{
						struct model_vertex_uncompressed const *vertex =
							&sheet->vertices[sheet->triangles[3 * triangle + corner]];

						all = vertex->texcoord.x >= bounds[0] - 0.004f && vertex->texcoord.x <= bounds[2] + 0.004f &&
							vertex->texcoord.y >= bounds[1] - 0.004f && vertex->texcoord.y <= bounds[3] + 0.004f;
					}
					if (all)
					{
						memcpy(&card->triangles[3 * taken], &sheet->triangles[3 * triangle], sizeof(*card->triangles) * 3);
						taken++;
					}
					else
					{
						memmove(&sheet->triangles[3 * kept], &sheet->triangles[3 * triangle], sizeof(*sheet->triangles) * 3);
						kept++;
					}
				}
				sheet->triangle_count = kept;
				card->triangle_count = taken;
				shader_indices[template_.part_count] = shader_indices[split];
				template_.part_count++;
			}
		}
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
		if (material_shaders[index] == BORROWED_SHADER_INDEX)
		{
			struct model_shader_reference *reference = &made->shaders[definition->shaders.count + index];

			csmemset(reference, 0, sizeof(*reference));
			reference->shader.group_tag = tag_get_group_tag(borrowed_shader);
			reference->shader.name = (char *)tag_get_name(borrowed_shader);
			reference->shader.name_length = (long)strlen(tag_get_name(borrowed_shader));
			reference->shader.index = borrowed_shader;
		}
		else
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
			played->classic_group = *group;
			played->shown = TRUE;
			played->count = 0;
			/* (the map's own first, as they are) */
			played->classic_count = classic_bitmap_count(material_bitmaps[index]);
			for (slot = 0; slot < played->classic_count; slot++)
			{
				played->bitmaps[slot] = *TAG_BLOCK_GET_ELEMENT(&played->map_group.bitmaps, slot, struct bitmap_data);
				/* (not cached yet: the map's own may be, in a block of its own) */
				SET_FLAG(played->bitmaps[slot].flags, _bitmap_cached_bit, FALSE);
				played->bitmaps[slot].hardware_format = NULL;
				csstrncpy(played->names[slot], "\001classic", sizeof(played->names[slot]) - 1);
			}
			played->count = played->classic_count;
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
		played->blends[slot] = material_blends[index];
		played->count = MAX(played->count, slot + 1);
		bitmap_group_get(played->tag_index)->bitmaps.count = played->count;
		free(material_textures[index]);
		free(material_bitmap_data[index]);
		material_textures[index] = NULL;
		material_bitmap_data[index] = NULL;
	}
	for (index = 0; index < shader_count; index++)
		shader_plug(shader_tags[index]);
	/* (the lens scrolls. slide is periodic function 6. This shader is the
	eyepiece's alone, so the face does not scroll with it) */
	if (monicle_shader_tag != NONE)
	{
		struct shader_model_definition *shader =
			(struct shader_model_definition *)shader_definition_get(monicle_shader_tag);

		shader->model.texture_animation.v_function = 6;
		shader->model.texture_animation.v_period = 2.f;
		shader->model.texture_animation.v_scale = 1.f;
	}
	/* (and the arc runs along its bolt, as the classic arc flickers: the
	same slide, quicker. Only the package's copy: Back's switch gives the
	map's back) */
	if (lightning_shader_tag != NONE)
	{
		struct shader_model_definition *shader =
			(struct shader_model_definition *)shader_definition_get(lightning_shader_tag);
		struct saber_shader *plugged;

		shader->model.texture_animation.v_function = 6;
		shader->model.texture_animation.v_period = 0.4f;
		shader->model.texture_animation.v_scale = 1.f;
		for (plugged = saber_models_globals.shaders; plugged; plugged = plugged->next)
		{
			if (plugged->tag_index == lightning_shader_tag)
				plugged->played.model.texture_animation = shader->model.texture_animation;
		}
	}
	/* (a shader of a texture cut out by its alpha, leaves', is alpha
	tested again, and drawn from both sides as Saber3D draws leaves. Only in
	the package's copy, never on the tag: the map's models share it, and the
	map's own model must still be drawn one-sided with Back's switch) */
	for (index = 0; index < material_count; index++)
	{
		struct shader_model_definition *shader;
		struct saber_shader *plugged;

		if (!material_cutouts[index])
			continue;
		shader = (struct shader_model_definition *)shader_definition_get(
			play_shader_tag(definition, borrowed_shader, material_shaders[index]));
		for (plugged = saber_models_globals.shaders; plugged; plugged = plugged->next)
		{
			struct shader_model_definition *plugged_tag = (struct shader_model_definition *)shader_definition_get(plugged->tag_index);

			if (plugged_tag == shader || plugged->tag_index == play_shader_tag(definition, borrowed_shader, material_shaders[index]))
			{
				SET_FLAG(plugged->played.model.flags, SHADER_MODEL_NOT_ALPHA_TESTED_BIT, FALSE);
				SET_FLAG(plugged->played.model.flags, SHADER_MODEL_TWO_SIDED_BIT, TRUE);
				break;
			}
		}
	}
	/* (a window is drawn from both sides: a template's glass may be wound the
	other way, a part turned over by its skeleton, and a pane seen from
	inside, a Warthog's windscreen, would not draw at all. Only in the
	package's copy, never on the tag: the map's own models share it) */
	for (index = 0; index < material_count; index++)
	{
		struct saber_shader *plugged;
		short material;

		for (material = 0; material < 256 && material_places[material] != index; material++)
		{
		}
		if (material >= template_.material_count ||
			!material_glass(template_.materials[material]))
		{
			continue;
		}
		for (plugged = saber_models_globals.shaders; plugged; plugged = plugged->next)
		{
			if (plugged->tag_index == play_shader_tag(definition, borrowed_shader, material_shaders[index]))
			{
				SET_FLAG(plugged->played.model.flags, SHADER_MODEL_TWO_SIDED_BIT, TRUE);
				break;
			}
		}
	}
	/* (each material's colour change, where its classic shader had one) */
	for (index = 0; index < material_count; index++)
	{
		short material;

		for (material = 0; material < 256 && material_places[material] != index; material++)
		{
		}
		if (material < template_.material_count)
		{
			struct saber_bitmap const *played = bitmap_played_get(material_bitmaps[index]);

			struct saber_texture const *base = played && material_permutations[index] < played->count ?
				&played->textures[material_permutations[index]] : NULL;
			real band[3];
			long weight = 0;
			/* (a ranked character's armour baked in colour, an Elite minor's
			blue: its mask takes it, and it is turned grey, so each rank's colour
			changes it, the least rank's the band it was) */
			boolean banded = skinned && made->rank_count > 1 && base &&
				mask_exists(pak, template_.materials[material], TRUE) &&
				saber_texture_band_color(base, band, &weight);

			if (skinned)
				platform_log("anniversary: %s drawn by %s, its texture %d of %s",
					template_.materials[material], tag_get_name(play_shader_tag(definition, borrowed_shader, material_shaders[index])),
					(int)material_permutations[index], tag_get_name(material_bitmaps[index]));
			saber_texture_mask_baked(banded);
			saber_texture_mask_plates(skinned && made->rank_count > 1);
			material_mask_play(pak, play_shader_tag(definition, borrowed_shader, material_shaders[index]),
				template_.materials[material], material_permutations[index], skinned, base);
			saber_texture_mask_baked(FALSE);
			saber_texture_mask_plates(FALSE);
			if (banded)
				saber_texture_band_grey((struct saber_texture *)base, band);
			/* (the colour its plates are drawn for its least rank: of the
			material most of its armour is baked in) */
			if (banded && weight > made->band_weight)
			{
				made->band_color.red = band[0];
				made->band_color.green = band[1];
				made->band_color.blue = band[2];
				made->band_weight = weight;
				platform_log("anniversary: %s's plates drawn %.2f %.2f %.2f for its least rank, as %s's armour",
					tag_get_name(tag_index), band[0], band[1], band[2], template_.materials[material]);
			}
		}
	}
	/* (the cyborg's armour takes the game's colour change whole, as the
	classic armour's does: each player's colour over it (the campaign's
	green), no mask. Its _br marks none of it, so masked it took none) */
	if (!_stricmp(template_name, "cyborg_mp") || !_stricmp(template_name, "cyborg"))
	{
		for (index = 0; index < shader_count; index++)
		{
			struct shader_model_definition *shader =
				(struct shader_model_definition *)shader_definition_get(shader_tags[index]);
			struct saber_shader *plugged;

			for (plugged = saber_models_globals.shaders; plugged; plugged = plugged->next)
			{
				if (plugged->tag_index != shader_tags[index] ||
					plugged->map_shader.model.change_color_source <= 0)
				{
					continue;
				}
				shader->model.change_color_source = plugged->map_shader.model.change_color_source;
				shader->model.multipurpose_map.index = NONE;
				plugged->played.model.change_color_source = plugged->map_shader.model.change_color_source;
				plugged->played.model.multipurpose_map.index = NONE;
				platform_log("anniversary: %s takes the game's colour change over the whole armour",
					tag_get_name(shader_tags[index]));
				break;
			}
		}
	}
	*definition = made->model;
	/* (the package's copy of each shader taken now, after everything this model
	affects has changed it: its flags, its mask, its change colour. A shader
	two models share is changed by the second too, so a copy taken as it was
	plugged would not be what the models draw with, and Back's switch would
	give back the wrong one, saber_toggle.c) */
	{
		struct saber_shader *plugged;

		for (plugged = saber_models_globals.shaders; plugged; plugged = plugged->next)
			plugged->played = *(struct shader_model_definition *)shader_definition_get(plugged->tag_index);
	}
	made->next = saber_models_globals.models;
	saber_models_globals.models = made;
	if (made->rank_count > 1)
		saber_models_globals.ranked_count++;
	platform_log("anniversary: %s drawn as the template %s (%ld parts, %ld levels of detail, %ld ranks)",
		tag_get_name(tag_index), template_name, made->part_count, made->level_count, made->rank_count);
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


/* ---------- the level (display.remastered_bsp) */

/* the most textures the level's geometry is drawn with, and how far from
the middle of the world a part of the structure is drawn. The sky is drawn
however far it stands. Far mountains past this are left out (world units) */
#define MAXIMUM_SABER_LEVEL_TEXTURES 512
#define MAXIMUM_SABER_LEVEL_DISTANCE 800.f
/* models.c's rasterizer geometry flags: the sky is drawn with no fog and
no depth, as the map's sky model is */
#define SABER_GEOMETRY_NO_FOG_BIT 2
#define SABER_GEOMETRY_NO_DEPTH_BIT 3
/* sky parts sorted before they are drawn (a level's clouds and planets) */
#define MAXIMUM_SABER_SKY_PARTS 256
/* a metre, in world units */
#define SABER_LEVEL_UNITS (1.f / 3.048f)

/* an entry of the map's tag table (cache_files.c's; menu_tags.c's too) */
struct saber_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

typedef char verify_saber_tag_instance_size[sizeof(struct saber_tag_instance) == 0x20 ? 1 : -1];

/* a part of the level's geometry: its buffers, the texture (of the level's
bitmap) it is drawn with, whether that is cut out, its bounds and the
light it is drawn in (sampled of the structure, as an object's) */
struct saber_level_part
{
	struct vertex_buffer vertex_buffer;
	struct triangle_buffer triangle_buffer;
	/* the shader it is drawn with, kept here: the renderer keeps a pointer
	to it past the draw (rasterizer_environment_fog_screen_model_submit, a
	model drawn in fog). On the stack, crouched in b30's water the fog
	screen read whatever the stack held there as a shader, and as a
	transparent chicago one's map, a tag index that is none ("i don't
	think 01d9005a is a tag index") */
	struct shader_model_definition shader;
	/* its compressed vertices and strips while its buffers are made, which
	copy them: a block of its own, ordinary memory, freed once they are
	made (b40's 2.4 million vertices, kept in the contiguous memory beside
	the buffers' own copies, ran Direct3D out of it) */
	void *memory;
	boolean memory_contiguous;
	short texture;
	boolean cutout;
	/* a sky mesh (clouds, the sky, stars, a planet): drawn by
	saber_models_level_sky_render, not with the structure */
	boolean sky;
	/* the daytime sky: drawn before the stars, so their black shows it */
	boolean back;
	/* a planet or a moon: drawn after the dome, which would cover it */
	boolean in_front;
	/* the halo ring: drawn after the planet, which would cover it */
	boolean over;
	/* blended on its texture's alpha: a planet. The card fades out, and
	cutting it on a line slices the disc */
	boolean blend;
	/* the skydome: the sky is centred on it */
	boolean dome;
	/* the daytime sky's colour behind the dome's stars (a copy of the dome,
	its texture a gradient from the zenith to the horizon): drawn first */
	boolean gradient;
	/* its sorting_transparent_group: the sky is drawn in its order, lowest
	first (NONE with none) */
	short sort_group;
	/* the texture's repeat (1 until a mesh says otherwise). Applied as the
	shader's map scale, so a coordinate kept in -1..1 still tiles */
	real uv_scale[2];
	real_rectangle3d bounds;
	real_point3d centroid;
	real radius;
	short lit_bsp;
	/* ground, a terrain's: lit as the ground is (saber_level_ground_light) */
	boolean terrain;
	/* a sky card's fade, its vertices' alpha (a mountain's and a cloud's
	ends clear): drawn of these, each vertex's alpha times the texture's,
	in place of its buffers. NULL, none */
	real (*fade_positions)[3];
	real (*fade_texcoords)[2];
	unsigned char *fade_alpha;
	unsigned short *fade_triangles;
	long fade_triangle_count;
	struct render_lighting lighting;
	/* a terrain's layers over its base (sand, grass, dust over a cliff),
	drawn blended over it by the weights of its vertices
	(saber_models_level_render): of its vertices their places, texture
	coordinates (repeated) and normals, and the colour they are lit (of
	`lighting`, as each vertex faces); each layer's texture, whether it
	multiplies what is under it (a darkening, a tint) rather than covering
	it, its weight a vertex and the triangles it weighs anything in */
	long layer_count;
	long layer_vertex_count;
	real (*layer_positions)[3];
	/* (each drawn layer's own: a layer's texture is laid by its own set of
	the vertex's coordinates) */
	real (*layer_texcoords[SABER_MAXIMUM_LAYERS])[2];
	real (*layer_normals)[3];
	unsigned char (*layer_lit)[3];
	/* (the light Saber's exporter baked into its vertices, as they are
	lit: NULL with none, when they are lit by `lighting`) */
	unsigned char (*layer_baked)[3];
	/* (the drawn layer that is its base laid whole over it, which covers it,
	and its fog with it: NONE, none) */
	long layer_cover;
	/* (its layers' vertices kept on the card, each drawn layer's, and its
	fog's: NULL, not yet, or none (d3d8_gl_static_vertices_new); each as of
	which of its lights, layer_lit_version's, or -1 a frame's point lights';
	and its fog as of where the camera was) */
	void *layer_vertices[SABER_MAXIMUM_LAYERS];
	long layer_vertices_version[SABER_MAXIMUM_LAYERS];
	boolean layer_vertices_failed;
	void *fog_vertices;
	/* (its layers' places in the level's batches, saber_level_batches: each
	layer's batch, its first vertex there and its first index; and its
	fog's) */
	long batch[SABER_MAXIMUM_LAYERS];
	unsigned long batch_vertex[SABER_MAXIMUM_LAYERS];
	unsigned long batch_index[SABER_MAXIMUM_LAYERS];
	unsigned long fog_batch_vertex;
	unsigned long fog_batch_index;
	boolean fog_batched;
	real_point3d fog_camera;
	long layer_lit_version;
	short layer_lit_bsp;
	short layer_textures[SABER_MAXIMUM_LAYERS];
	boolean layer_multiply[SABER_MAXIMUM_LAYERS];
	unsigned char *layer_weights[SABER_MAXIMUM_LAYERS];
	long layer_triangle_counts[SABER_MAXIMUM_LAYERS];
	unsigned short *layer_triangles[SABER_MAXIMUM_LAYERS];
};

static struct
{
	boolean drawn;
	struct saber_level_part *parts;
	long part_count;
	byte *vertices;
	boolean vertices_contiguous;
	/* its shaders, solid and cut out, of the map's first model shader,
	whose base map is its bitmap */
	struct shader_model_definition shaders[2];
	/* its bitmap: a tag of its own, added to the map's tag table */
	long bitmap_tag;
	struct bitmap_group group;
	struct bitmap_data *bitmaps;
	struct saber_texture *textures;
	long texture_count;
	struct saber_tag_instance *instances;
	struct saber_tag_instance *map_instances;
	long map_instance_count;
	/* where the sky is seen from: the dome's middle, at the horizon */
	boolean has_sky_anchor;
	real_point3d sky_anchor;
	/* the ground's light, in the structure ground_lit_bsp */
	boolean ground_lit;
	short ground_lit_bsp;
	struct render_lighting ground_lighting;
} saber_level;

static void saber_level_layers_free(
	struct saber_level_part *part)
{
	long layer;

	if (part->layer_positions)
		free(part->layer_positions);
	if (part->layer_normals)
		free(part->layer_normals);
	if (part->layer_baked)
		free(part->layer_baked);
	if (part->layer_lit)
		free(part->layer_lit);
	for (layer = 0; layer < SABER_MAXIMUM_LAYERS; layer++)
	{
		if (part->layer_vertices[layer])
			d3d8_gl_static_vertices_free(part->layer_vertices[layer]);
		part->layer_vertices[layer] = NULL;
		if (part->layer_weights[layer])
			free(part->layer_weights[layer]);
		if (part->layer_triangles[layer])
			free(part->layer_triangles[layer]);
		if (part->layer_texcoords[layer])
			free(part->layer_texcoords[layer]);
	}
	if (part->fog_vertices)
		d3d8_gl_static_vertices_free(part->fog_vertices);
	part->fog_vertices = NULL;
	part->layer_vertices_failed = FALSE;
	if (part->fade_positions)
		free(part->fade_positions);
	if (part->fade_texcoords)
		free(part->fade_texcoords);
	if (part->fade_alpha)
		free(part->fade_alpha);
	if (part->fade_triangles)
		free(part->fade_triangles);
	part->fade_positions = NULL;
	part->fade_texcoords = NULL;
	part->fade_alpha = NULL;
	part->fade_triangles = NULL;
	part->fade_triangle_count = 0;
	part->layer_positions = NULL;
	csmemset(part->layer_texcoords, 0, sizeof(part->layer_texcoords));
	part->layer_normals = NULL;
	part->layer_lit = NULL;
	part->layer_baked = NULL;
	csmemset(part->layer_weights, 0, sizeof(part->layer_weights));
	csmemset(part->layer_triangles, 0, sizeof(part->layer_triangles));
	part->layer_count = 0;
}

/* the largest a level's texture is drawn, its larger levels left out:
b30's 185 textures, whole, were 160 MB of the texture cache's 256, held
for the level, and the rest of the map's drawn, an elite's, a covenant
crate's, had no room and were drawn of the default texture, grey in the
distance ("YOU GOT STABBED"); and a ground's base, drawn under its own
layer of the same material and never seen, its least */
#define SABER_LEVEL_TEXTURE_LARGEST 1024
#define SABER_LEVEL_GROUND_BASE_LARGEST 64

/* the most of the texture cache a level's textures are held in: all of
them are drawn each frame, so held there each frame. At 1024 texels b30's
were 100 MB of its 256, and with the rest of a frame's, its pages taken
in pieces, an elite's or a grunt's or a tree's texture found no room and
was drawn white. The largest are halved until they fit */
#define SABER_LEVEL_TEXTURE_BUDGET (40L * 1024L * 1024L)

/* (its pages in the texture cache, of 16 KB) */
static long saber_level_texture_pages(
	struct saber_texture const *texture)
{
	return (texture->pixels_size + 16383L) / 16384L;
}

static void saber_level_textures_fit(
	void)
{
	long total = 0;
	long halved = 0;
	long before;
	long index;

	for (index = 0; index < saber_level.texture_count; index++)
		total += saber_level_texture_pages(&saber_level.textures[index]);
	before = total;
	while (total * 16384L > SABER_LEVEL_TEXTURE_BUDGET)
	{
		long largest = NONE;
		long largest_pages = 0;

		for (index = 0; index < saber_level.texture_count; index++)
		{
			struct saber_texture const *texture = &saber_level.textures[index];
			long pages = saber_level_texture_pages(texture);

			if (texture->pixels && texture->mipmap_count >= 1 && texture->width >= 128 && texture->height >= 128 &&
				pages > largest_pages)
			{
				largest = index;
				largest_pages = pages;
			}
		}
		if (largest == NONE || !saber_texture_halve(&saber_level.textures[largest]))
			break;
		bitmap_describe(&saber_level.textures[largest], saber_level.bitmap_tag, &saber_level.bitmaps[largest]);
		total += saber_level_texture_pages(&saber_level.textures[largest]) - largest_pages;
		halved++;
	}
	platform_log("anniversary: the structure's textures: %ld MB, %ld halved to %ld MB of the texture cache",
		before * 16L / 1024L, halved, total * 16L / 1024L);
}

/* texture_find, its levels larger than `largest` left out */
static boolean level_texture_find(
	struct saber_pak *pak,
	char const *texture_name,
	struct saber_texture *texture,
	char const **source,
	char const **reason,
	long largest)
{
	if (!texture_find(pak, texture_name, texture, source, reason))
		return FALSE;
	while ((texture->width > largest || texture->height > largest) && saber_texture_halve(texture))
	{
	}

	return TRUE;
}

/* The layers of a level terrain's part over its base, of the package's
part: each layer's texture (its slot of the level's bitmap, read as it is
first drawn; none, the layer is not drawn), its weights (its channel of
the vertices' colour) and the triangles it weighs anything in. A layer that
multiplies what is under it is not drawn. */
static void saber_level_layers_make(
	struct saber_pak *pak,
	struct saber_template const *level,
	struct saber_mesh_part const *part,
	short *slots,
	struct saber_level_part *made)
{
	short *layer_slots = slots + level->material_count;
	long layer;
	long vertex;
	/* (each drawn layer's of the part's) */
	long sources[SABER_MAXIMUM_LAYERS];

	made->layer_cover = NONE;
	made->layer_count = 0;
	made->layer_lit_bsp = NONE;
	if (!part->layer_weights || part->layer_count <= 0)
		return;
	for (layer = 0; layer < part->layer_count && layer < SABER_MAXIMUM_LAYERS; layer++)
	{
		short material = part->layer_materials[layer];
		short channel = part->layer_channels[layer];
		long drawn = made->layer_count;
		long triangle;
		unsigned char *weights;
		unsigned short *triangles;

		/* (not a multiplying layer: of the weights this reads, the Forerunner
		structures' dirt, dirt_multi_04, darkens them near black. Which
		weights a multiplying layer has is not known yet) */
		if (part->layer_multiply[layer] || material < 0 || material >= level->material_count ||
			channel < 0 || channel > _saber_layer_channel_whole)
		{
			continue;
		}
		/* (nor a card's own material laid over it, out of the terrain: an ivy
		wall's plants_01 over itself, its texture's alpha made its height,
		drew the leaves' card whole, a dark green plate behind them) */
		if (part->terrain == 2 && material == part->material_index && !strstr(part->region, "(terrain)"))
			continue;
		if (layer_slots[material] == NONE)
		{
			struct saber_texture *texture = &saber_level.textures[saber_level.texture_count];
			char const *source = NULL;
			char const *texture_reason = NULL;

			if (saber_level.texture_count >= MAXIMUM_SABER_LEVEL_TEXTURES ||
				!level_texture_find(pak, level->materials[material], texture, &source, &texture_reason,
					SABER_LEVEL_TEXTURE_LARGEST))
			{
				layer_slots[material] = -2;
				/* (logged: a layer not drawn leaves its base bare where it lies,
				a plain patch edged as the triangles are) */
				platform_log("anniversary: the structure's layer %s is not drawn: %s", level->materials[material],
					saber_level.texture_count >= MAXIMUM_SABER_LEVEL_TEXTURES ? "too many textures" :
					texture_reason ? texture_reason : "no texture");
			}
			else if (!bitmap_describe(texture, saber_level.bitmap_tag, &saber_level.bitmaps[saber_level.texture_count]))
			{
				saber_texture_dispose(texture);
				layer_slots[material] = -2;
				platform_log("anniversary: the structure's layer %s is not drawn: its texture's format is not described",
					level->materials[material]);
			}
			else
			{
				layer_slots[material] = (short)saber_level.texture_count++;
				/* (a covering layer's alpha its height, by which it gives way to
				what is under it; a multiplying one's solid) */
				if (part->layer_multiply[layer])
					saber_texture_make_solid(texture);
				else
					saber_texture_height_alpha(texture);
			}
		}
		if (layer_slots[material] < 0)
			continue;
		weights = malloc((size_t)part->vertex_count);
		triangles = malloc(sizeof(*triangles) * 3 * (size_t)part->triangle_count);
		if (!weights || !triangles)
		{
			if (weights)
				free(weights);
			if (triangles)
				free(triangles);
			continue;
		}
		for (vertex = 0; vertex < part->vertex_count; vertex++)
			weights[vertex] = channel == _saber_layer_channel_whole ? 255 : part->layer_weights[vertex][channel];
		made->layer_triangle_counts[drawn] = 0;
		for (triangle = 0; triangle < part->triangle_count; triangle++)
		{
			unsigned short const *corners = &part->triangles[3 * triangle];

			if (weights[corners[0]] || weights[corners[1]] || weights[corners[2]])
			{
				memcpy(&triangles[3 * made->layer_triangle_counts[drawn]], corners, 3 * sizeof(*corners));
				made->layer_triangle_counts[drawn]++;
			}
		}
		if (!made->layer_triangle_counts[drawn])
		{
			free(weights);
			free(triangles);
			continue;
		}
		made->layer_textures[drawn] = layer_slots[material];
		made->layer_multiply[drawn] = part->layer_multiply[layer] ? TRUE : FALSE;
		made->layer_weights[drawn] = weights;
		made->layer_triangles[drawn] = triangles;
		sources[drawn] = layer;
		if (channel == _saber_layer_channel_whole && made->layer_cover == NONE && drawn == 0)
			made->layer_cover = drawn;
		made->layer_count++;
	}
	if (!made->layer_count)
		return;
	made->layer_vertex_count = part->vertex_count;
	made->layer_positions = malloc(sizeof(*made->layer_positions) * (size_t)part->vertex_count);
	made->layer_normals = malloc(sizeof(*made->layer_normals) * (size_t)part->vertex_count);
	made->layer_lit = malloc(sizeof(*made->layer_lit) * (size_t)part->vertex_count);
	if (!made->layer_positions || !made->layer_normals || !made->layer_lit)
	{
		saber_level_layers_free(made);
		return;
	}
	/* (the baked light, linear, shown as Saber's tone map shows it: its
	brightness's 1/2.2 power, the sunlit sand's, about 200, whole. Read
	as it is, the cliffs in the shade, about 40, were near black) */
	if (part->vertex_light)
	{
		made->layer_baked = malloc(sizeof(*made->layer_baked) * (size_t)part->vertex_count);
		if (made->layer_baked)
		{
			for (vertex = 0; vertex < part->vertex_count; vertex++)
			{
				long channel;

				for (channel = 0; channel < 3; channel++)
				{
					real value = 255.f * (real)pow((double)part->vertex_light[vertex][channel] / 200.0, 1.0 / 2.2);

					made->layer_baked[vertex][channel] = (unsigned char)(value >= 255.f ? 255.f : value);
				}
			}
		}
	}
	for (layer = 0; layer < made->layer_count; layer++)
	{
		made->layer_texcoords[layer] = malloc(sizeof(*made->layer_texcoords[layer]) * (size_t)part->vertex_count);
		if (!made->layer_texcoords[layer])
		{
			saber_level_layers_free(made);
			return;
		}
		for (vertex = 0; vertex < part->vertex_count; vertex++)
		{
			struct model_vertex_uncompressed const *point = &part->vertices[vertex];
			real u = point->texcoord.x;
			real v = point->texcoord.y;

			if (part->layer_texcoords)
			{
				u = part->layer_texcoords[vertex][sources[layer]][0];
				v = part->layer_texcoords[vertex][sources[layer]][1];
			}
			made->layer_texcoords[layer][vertex][0] = u * made->uv_scale[0];
			made->layer_texcoords[layer][vertex][1] = v * made->uv_scale[1];
		}
	}
	for (vertex = 0; vertex < part->vertex_count; vertex++)
	{
		struct model_vertex_uncompressed const *point = &part->vertices[vertex];

		made->layer_positions[vertex][0] = point->position.x;
		made->layer_positions[vertex][1] = point->position.y;
		made->layer_positions[vertex][2] = point->position.z;
		made->layer_normals[vertex][0] = point->normal.i;
		made->layer_normals[vertex][1] = point->normal.j;
		made->layer_normals[vertex][2] = point->normal.k;
	}
}

static void saber_level_free(
	boolean give_back)
{
	long index;

	saber_level_batches_free();
	for (index = 0; saber_level.parts && index < saber_level.part_count; index++)
	{
		rasterizer_triangle_buffer_delete(&saber_level.parts[index].triangle_buffer);
		rasterizer_vertex_buffer_delete(&saber_level.parts[index].vertex_buffer);
		saber_level_layers_free(&saber_level.parts[index]);
		if (saber_level.parts[index].memory)
		{
			if (saber_level.parts[index].memory_contiguous)
				XPhysicalFree(saber_level.parts[index].memory);
			else
				free(saber_level.parts[index].memory);
		}
	}
	if (saber_level.parts)
		free(saber_level.parts);
	if (saber_level.vertices)
	{
		if (saber_level.vertices_contiguous)
			XPhysicalFree(saber_level.vertices);
		else
			free(saber_level.vertices);
	}
	for (index = 0; saber_level.textures && index < saber_level.texture_count; index++)
		saber_texture_dispose(&saber_level.textures[index]);
	if (saber_level.textures)
		free(saber_level.textures);
	if (saber_level.bitmaps)
		free(saber_level.bitmaps);
	if (saber_level.instances)
	{
		long count;
		void *current = cache_files_tag_instances(&count);

		/* (the map's table back, while ours is the one in use) */
		if (give_back && current == (void *)saber_level.instances)
			cache_files_set_tag_instances(saber_level.map_instances, saber_level.map_instance_count);
		if (cache_files_tag_instances(&count) != (void *)saber_level.instances)
			free(saber_level.instances);
	}
	csmemset(&saber_level, 0, sizeof(saber_level));
	saber_level.bitmap_tag = NONE;
}

/* a bitmap tag of the level's own, added to the end of the map's tag
table: NONE if it cannot be */
static long saber_level_bitmap_tag_add(
	void)
{
	long count;
	struct saber_tag_instance *instances = cache_files_tag_instances(&count);
	struct saber_tag_instance *grown;
	struct saber_tag_instance const *bitmap = NULL;
	long salt = 0;
	long index;

	if (!instances || count <= 0 || count >= UNSIGNED_SHORT_MAX - 1)
		return NONE;
	for (index = 0; index < count; index++)
	{
		long instance_salt = (long)((unsigned long)instances[index].tag_index >> 16);

		salt = MAX(salt, instance_salt);
		if (!bitmap && instances[index].group_tag == BITMAP_GROUP_TAG)
			bitmap = &instances[index];
	}
	if (!bitmap)
		return NONE;
	grown = malloc(sizeof(*grown) * ((size_t)count + 1));
	if (!grown)
		return NONE;
	memcpy(grown, instances, sizeof(*grown) * (size_t)count);
	grown[count] = *bitmap;
	grown[count].tag_index = (long)(((unsigned long)(salt + 1) << 16) | (unsigned long)count);
	grown[count].name = "anniversary\\remastered bsp";
	grown[count].base_address = &saber_level.group;
	saber_level.instances = grown;
	saber_level.map_instances = instances;
	saber_level.map_instance_count = count;
	cache_files_set_tag_instances(grown, count + 1);

	return grown[count].tag_index;
}

/* a level's hard surface, never cut out by its alpha (its shine, or its
height): a Forerunner's (fr_, forerunner_), the ground's (t_, ter_), a flat
colour's (sc_), a lifepod's, a light's (lig_), metal, a panel */
static boolean level_material_hard(
	char const *material)
{
	return !strncmp(material, "fr_", 3) || !strncmp(material, "forerunner", 10) || !strncmp(material, "t_", 2) ||
		!strncmp(material, "ter_", 4) || !strncmp(material, "sc_", 3) || !strncmp(material, "lig_", 4) ||
		strstr(material, "lifepod") != NULL || strstr(material, "metal") != NULL || strstr(material, "panel") != NULL;
}

/* whether a level's material is its sky: the clouds, the sky dome, the
stars, the planets and the halo ring. Drawn in place of the map's sky,
not as structure (the ring stands past where structure is drawn) */
static boolean saber_level_material_sky(
	char const *material)
{
	return strncmp(material, "cloud", 5) == 0 || strncmp(material, "basis_sky", 9) == 0 ||
		strncmp(material, "stars", 5) == 0 || strstr(material, "planet") != NULL ||
		strstr(material, "ring") != NULL;
}

/* whether a level's material is drawn as structure: not an effect's, a far
plane's, a lightmap's, water's (the map's water is drawn), the sky's or none */
static boolean saber_level_material_drawn(
	char const *material)
{
	return !material_effect(material) && !strstr(material, "farplane") && !strstr(material, "_lm") &&
		!strstr(material, "water") && !saber_level_material_sky(material);
}

/* the daytime sky's colour `along` from the zenith (0) to the horizon (1):
Anniversary's b30, a hazy blue lightening to a pale haze at the horizon */
static void saber_level_sky_color(
	real along,
	long color[3])
{
	static real const stops[][4] = {
		{ 0.f, 72.f, 116.f, 168.f },
		{ 0.35f, 78.f, 122.f, 172.f },
		{ 0.6f, 88.f, 130.f, 175.f },
		{ 0.8f, 112.f, 150.f, 190.f },
		{ 0.92f, 138.f, 175.f, 212.f },
		{ 1.f, 150.f, 186.f, 222.f } };
	long stop;

	if (along <= 0.f)
		along = 0.f;
	if (along >= 1.f)
		along = 1.f;
	for (stop = 1; stop < (long)NUMBEROF(stops) - 1 && along > stops[stop][0]; stop++)
		;
	{
		real low = stops[stop - 1][0];
		real span = stops[stop][0] - low;
		real mix = span > 0.f ? (along - low) / span : 0.f;
		long channel;

		if (mix < 0.f)
			mix = 0.f;
		if (mix > 1.f)
			mix = 1.f;
		for (channel = 0; channel < 3; channel++)
		{
			color[channel] = (long)(stops[stop - 1][channel + 1] +
				(stops[stop][channel + 1] - stops[stop - 1][channel + 1]) * mix + 0.5f);
		}
	}
}

/* a 4 by 256 DXT1 texture of the daytime sky's colour, the zenith at its
top row and the horizon at its bottom: each block shades from its top
row's colour to its bottom row's */
static boolean saber_level_sky_gradient(
	struct saber_texture *texture)
{
	long const height = 256;
	long block;

	csmemset(texture, 0, sizeof(*texture));
	texture->width = 4;
	texture->height = (short)height;
	texture->mipmap_count = 0;
	texture->format = 14; /* (_bitmap_format_dxt1, bitmap_group.c's) */
	texture->pixels_size = (height / 4) * 8;
	texture->pixels = malloc((size_t)texture->pixels_size);
	if (!texture->pixels)
		return FALSE;
	for (block = 0; block < height / 4; block++)
	{
		unsigned char *bytes = texture->pixels + block * 8;
		long top[3];
		long bottom[3];
		unsigned long packed0;
		unsigned long packed1;
		/* (rows 0..3: c0, 2/3 c0, 1/3 c0, c1) */
		static unsigned char const forward[4] = { 0, 2, 3, 1 };
		static unsigned char const backward[4] = { 1, 3, 2, 0 };
		unsigned char const *rows;
		unsigned long indices = 0;
		long row;

		saber_level_sky_color((real)(block * 4) / (real)(height - 1), top);
		saber_level_sky_color((real)(block * 4 + 3) / (real)(height - 1), bottom);
		packed0 = ((unsigned long)(top[0] >> 3) << 11) | ((unsigned long)(top[1] >> 2) << 5) | (unsigned long)(top[2] >> 3);
		packed1 = ((unsigned long)(bottom[0] >> 3) << 11) | ((unsigned long)(bottom[1] >> 2) << 5) |
			(unsigned long)(bottom[2] >> 3);
		if (packed0 == packed1)
		{
			rows = NULL;
		}
		else if (packed0 > packed1)
		{
			rows = forward;
		}
		else
		{
			unsigned long swap = packed0;

			packed0 = packed1;
			packed1 = swap;
			rows = backward;
		}
		for (row = 0; row < 4; row++)
		{
			unsigned long index = rows ? rows[row] : 0;

			indices |= (index | (index << 2) | (index << 4) | (index << 6)) << (8 * row);
		}
		bytes[0] = (unsigned char)packed0;
		bytes[1] = (unsigned char)(packed0 >> 8);
		bytes[2] = (unsigned char)packed1;
		bytes[3] = (unsigned char)(packed1 >> 8);
		bytes[4] = (unsigned char)indices;
		bytes[5] = (unsigned char)(indices >> 8);
		bytes[6] = (unsigned char)(indices >> 16);
		bytes[7] = (unsigned char)(indices >> 24);
	}

	return TRUE;
}

/* whether display.remastered_bsp_hide names a level part's node, or its
material ("t_cliff_04_a"): a list of
its names' pieces, comma separated ("terrain>1,_terrain_$"), any of which
in the node's name leaves the part out ("$" its name's end). b30's ground
is three meshes over one another (".(terrain)_terrain", "_terrain>0",
"_terrain>1"), within a fraction of a unit; which Anniversary draws is not
known yet */
static boolean level_part_hidden(
	char const *name)
{
	char const *list = config_string("display.remastered_bsp_hide");
	char piece[64];

	while (list && *list && name && *name)
	{
		size_t length = 0;
		boolean at_end = FALSE;

		while (*list == ',' || *list == ' ')
			list++;
		while (list[length] && list[length] != ',' && length < sizeof(piece) - 1)
		{
			piece[length] = list[length];
			length++;
		}
		list += length;
		if (length && piece[length - 1] == '$')
		{
			at_end = TRUE;
			length--;
		}
		piece[length] = 0;
		if (!length)
			continue;
		if (!at_end && strstr(name, piece))
			return TRUE;
		if (at_end)
		{
			char const *found = strstr(name, piece);

			if (found && (found[length] == 0 || found[length] == '_' || found[length] == '.'))
				return TRUE;
		}
	}

	return FALSE;
}

/* Draws the level's structure as the package's level geometry (its
".lg"): read, its parts made buffers, its textures a bitmap of its own,
drawn by saber_models_level_render in place of the structure's. FALSE if
it cannot be. */
static boolean saber_level_play(
	struct saber_pak *pak)
{
	struct saber_template level;
	long entry = NONE;
	long index;
	unsigned long size = 0;
	unsigned char *bytes;
	char const *reason = "out of memory";
	short *slots = NULL;
	long compressed_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_model_compressed);
	long uncompressed_size = rasterizer_geometry_get_vertex_size(_rasterizer_vertex_type_model_uncompressed);
	unsigned long vertices_size = 0;
	struct model_vertex_compressed *vertices;
	word *strips;
	long drawn = 0;
	long triangles = 0;
	long sky_parts = 0;
	long layered_parts = 0;
	real_rectangle3d bounds;
	/* the daytime sky's copy of the dome, and its texture: NONE without */
	long gradient_part = NONE;
	short gradient_slot = NONE;

	csmemset(&level, 0, sizeof(level));
	for (index = 0; index < saber_pak_entry_count(pak); index++)
	{
		if (saber_pak_entry_type(pak, index) == _saber_entry_level_geometry)
		{
			entry = index;
			break;
		}
	}
	if (entry == NONE)
	{
		platform_log("anniversary: no remastered structure: the package has no level geometry");
		return FALSE;
	}
	bytes = saber_pak_read(pak, entry, &size);
	if (!bytes || !saber_level_geometry_read(&level, bytes, size, saber_pak_xbox360(pak), &reason))
	{
		if (bytes)
			free(bytes);
		platform_log("anniversary: no remastered structure: %s: %s", saber_pak_entry_name(pak, entry), reason);
		saber_template_dispose(&level);
		return FALSE;
	}
	free(bytes);

	/* (the daytime sky behind the stars: a copy of the skydome, its texture
	coordinates its height over the horizon, seen from the dome's middle.
	The package's sky is lit by an atmosphere the dome is drawn over, which
	Halo has not: its stars' black is clear, and the gradient shows there) */
	saber_level.has_sky_anchor = FALSE;
	{
		long dome = NONE;

		for (index = 0; index < level.part_count; index++)
		{
			struct saber_mesh_part const *part = &level.parts[index];

			if (part->sky && part->vertex_count > 0 && part->triangle_count > 0 &&
				part->material_index >= 0 && part->material_index < level.material_count &&
				strncmp(level.materials[part->material_index], "stars", 5) == 0)
			{
				dome = index;
				break;
			}
		}
		if (dome != NONE)
		{
			struct saber_mesh_part *parts = realloc(level.parts, sizeof(*level.parts) * ((size_t)level.part_count + 1));

			if (parts)
			{
				struct saber_mesh_part *copy = &parts[level.part_count];
				struct saber_mesh_part const *source;
				real_rectangle3d box;
				long vertex;

				level.parts = parts;
				source = &level.parts[dome];
				box.x0 = box.y0 = box.z0 = REAL_MAX;
				box.x1 = box.y1 = box.z1 = -REAL_MAX;
				for (vertex = 0; vertex < source->vertex_count; vertex++)
				{
					real_point3d const *at = &source->vertices[vertex].position;

					box.x0 = MIN(box.x0, at->x);
					box.x1 = MAX(box.x1, at->x);
					box.y0 = MIN(box.y0, at->y);
					box.y1 = MAX(box.y1, at->y);
					box.z0 = MIN(box.z0, at->z);
					box.z1 = MAX(box.z1, at->z);
				}
				/* (the horizon is the level's ground, z 0, where the dome
				reaches below it; its bottom where it does not) */
				saber_level.sky_anchor.x = (box.x0 + box.x1) / 2.f;
				saber_level.sky_anchor.y = (box.y0 + box.y1) / 2.f;
				saber_level.sky_anchor.z = box.z0 < 0.f && box.z1 > 0.f ? 0.f : box.z0;
				saber_level.sky_anchor.x *= SABER_LEVEL_UNITS;
				saber_level.sky_anchor.y *= SABER_LEVEL_UNITS;
				saber_level.sky_anchor.z *= SABER_LEVEL_UNITS;
				saber_level.has_sky_anchor = TRUE;
				*copy = *source;
				copy->vertices = malloc(sizeof(*copy->vertices) * (size_t)source->vertex_count);
				copy->triangles = malloc(sizeof(*copy->triangles) * 3 * (size_t)source->triangle_count);
				copy->layer_count = 0;
				copy->layer_weights = NULL;
				if (copy->vertices && copy->triangles)
				{
					memcpy(copy->vertices, source->vertices, sizeof(*copy->vertices) * (size_t)source->vertex_count);
					memcpy(copy->triangles, source->triangles, sizeof(*copy->triangles) * 3 * (size_t)source->triangle_count);
					for (vertex = 0; vertex < copy->vertex_count; vertex++)
					{
						struct model_vertex_uncompressed *point = &copy->vertices[vertex];
						real across_x = point->position.x - saber_level.sky_anchor.x / SABER_LEVEL_UNITS;
						real across_y = point->position.y - saber_level.sky_anchor.y / SABER_LEVEL_UNITS;
						real up = point->position.z - saber_level.sky_anchor.z / SABER_LEVEL_UNITS;
						real elevation = (real)atan2((double)up, sqrt((double)(across_x * across_x + across_y * across_y)));
						real along = 1.f - elevation / 1.5707963f;

						if (along > 1.f)
							along = 1.f;
						if (along < 0.f)
							along = 0.f;
						/* (kept off the texture's edge rows, which wrap) */
						point->texcoord.x = 0.5f;
						point->texcoord.y = 0.004f + along * 0.992f;
					}
					copy->uv_repeat = 0;
					copy->uv_scale[0] = 1.f;
					copy->uv_scale[1] = 1.f;
					copy->sort_group = NONE;
					gradient_part = level.part_count;
					level.part_count++;
				}
				else
				{
					if (copy->vertices)
						free(copy->vertices);
					if (copy->triangles)
						free(copy->triangles);
				}
			}
		}
	}

	/* its parts drawn: of a material drawn, not far off (the sky however far
	it stands); in world units */
	saber_level.texture_count = 0;
	saber_level.ground_lit = FALSE;
	saber_level.ground_lit_bsp = NONE;
	/* (each material's texture as a base, and after them as a layer: a
	layer's alpha is its height, saber_texture_height_alpha) */
	slots = malloc(sizeof(*slots) * (4 * (size_t)level.material_count + 1));
	saber_level.textures = malloc(sizeof(*saber_level.textures) * MAXIMUM_SABER_LEVEL_TEXTURES);
	saber_level.bitmaps = malloc(sizeof(*saber_level.bitmaps) * MAXIMUM_SABER_LEVEL_TEXTURES);
	saber_level.parts = malloc(sizeof(*saber_level.parts) * ((size_t)level.part_count + 1));
	if (!slots || !saber_level.textures || !saber_level.bitmaps || !saber_level.parts)
		goto refused;
	csmemset(saber_level.parts, 0, sizeof(*saber_level.parts) * ((size_t)level.part_count + 1));
	csmemset(saber_level.textures, 0, sizeof(*saber_level.textures) * MAXIMUM_SABER_LEVEL_TEXTURES);
	csmemset(saber_level.parts, 0, sizeof(*saber_level.parts) * ((size_t)level.part_count + 1));
	for (index = 0; index < 2 * level.material_count; index++)
		slots[index] = NONE;
	/* (and after them whether a material's cut out is logged) */
	for (index = 2 * level.material_count; index < 4 * level.material_count; index++)
		slots[index] = FALSE;
	/* (and after them whether a material is laid over the ground or a cliff
	as a layer: its alpha its height, never a cutout's. moss_02, cut, holed
	b30's cliffs wherever it covered them whole) */
	for (index = 0; index < level.part_count; index++)
	{
		long layer;

		for (layer = 0; layer < level.parts[index].layer_count; layer++)
		{
			short layered = level.parts[index].layer_materials[layer];

			if (layered >= 0 && layered < level.material_count)
				slots[3 * level.material_count + layered] = TRUE;
		}
	}
	saber_level.bitmap_tag = saber_level_bitmap_tag_add();
	reason = "a bitmap of its own cannot be added to the map's tags";
	if (saber_level.bitmap_tag == NONE)
		goto refused;
	bounds.x0 = bounds.y0 = bounds.z0 = REAL_MAX;
	bounds.x1 = bounds.y1 = bounds.z1 = -REAL_MAX;
	for (index = 0; index < level.part_count; index++)
	{
		struct saber_mesh_part *part = &level.parts[index];
		struct saber_level_part *made = &saber_level.parts[saber_level.part_count];
		short material = part->material_index;
		long vertex;
		boolean sky;

		if (material < 0 || material >= level.material_count || part->vertex_count <= 0 || part->triangle_count <= 0 ||
			level_part_hidden(part->region) || level_part_hidden(level.materials[material]))
		{
			part->vertex_count = 0;
			continue;
		}
		sky = part->sky || saber_level_material_sky(level.materials[material]) || index == gradient_part;
		/* (the package's "basis_sky" is a moon's card, not the daytime sky:
		the sky is drawn in its objects' sorting_transparent_group order) */
		made->back = FALSE;
		made->in_front = sky && strstr(level.materials[material], "planet") != NULL;
		made->over = sky && strstr(level.materials[material], "ring") != NULL;
		made->gradient = index == gradient_part;
		made->dome = sky && !made->gradient && strncmp(level.materials[material], "stars", 5) == 0;
		made->sort_group = part->sort_group;
		/* (everything of the sky but the dome and the gradient is blended on
		its texture's alpha: a planet's card is clear and its limb fades, a
		cloud's edge is soft, the ring's card is clear around the band. A
		test would cut them) */
		made->blend = sky && !made->gradient;
		if (!sky && !saber_level_material_drawn(level.materials[material]))
		{
			part->vertex_count = 0;
			continue;
		}
		made->sky = sky;
		made->bounds.x0 = made->bounds.y0 = made->bounds.z0 = REAL_MAX;
		made->bounds.x1 = made->bounds.y1 = made->bounds.z1 = -REAL_MAX;
		for (vertex = 0; vertex < part->vertex_count; vertex++)
		{
			struct model_vertex_uncompressed *point = &part->vertices[vertex];

			point->position.x *= SABER_LEVEL_UNITS;
			point->position.y *= SABER_LEVEL_UNITS;
			point->position.z *= SABER_LEVEL_UNITS;
			point->nodes[0] = 0;
			point->nodes[1] = 0;
			point->node_weights[0] = 1.f;
			point->node_weights[1] = 0.f;
			made->bounds.x0 = MIN(made->bounds.x0, point->position.x);
			made->bounds.x1 = MAX(made->bounds.x1, point->position.x);
			made->bounds.y0 = MIN(made->bounds.y0, point->position.y);
			made->bounds.y1 = MAX(made->bounds.y1, point->position.y);
			made->bounds.z0 = MIN(made->bounds.z0, point->position.z);
			made->bounds.z1 = MAX(made->bounds.z1, point->position.z);
		}
		made->centroid.x = (made->bounds.x0 + made->bounds.x1) / 2.f;
		made->centroid.y = (made->bounds.y0 + made->bounds.y1) / 2.f;
		made->centroid.z = (made->bounds.z0 + made->bounds.z1) / 2.f;
		{
			real_vector3d across;

			across.i = made->bounds.x1 - made->bounds.x0;
			across.j = made->bounds.y1 - made->bounds.y0;
			across.k = made->bounds.z1 - made->bounds.z0;
			made->radius = magnitude3d(&across) / 2.f;
		}
		if (!sky && magnitude3d((real_vector3d *)&made->centroid) > MAXIMUM_SABER_LEVEL_DISTANCE)
		{
			part->vertex_count = 0;
			continue;
		}
		/* (the daytime sky's texture: made, not read) */
		if (made->gradient)
		{
			if (gradient_slot == NONE && saber_level.texture_count < MAXIMUM_SABER_LEVEL_TEXTURES)
			{
				struct saber_texture *texture = &saber_level.textures[saber_level.texture_count];

				if (saber_level_sky_gradient(texture) &&
					bitmap_describe(texture, saber_level.bitmap_tag, &saber_level.bitmaps[saber_level.texture_count]))
				{
					gradient_slot = (short)saber_level.texture_count++;
				}
				else
				{
					saber_texture_dispose(texture);
				}
			}
			if (gradient_slot == NONE)
			{
				part->vertex_count = 0;
				continue;
			}
		}
		/* (its texture: read as its material is first drawn) */
		else if (slots[material] == NONE)
		{
			struct saber_texture *texture = &saber_level.textures[saber_level.texture_count];
			char const *source = NULL;
			char const *texture_reason = NULL;

			if (saber_level.texture_count >= MAXIMUM_SABER_LEVEL_TEXTURES ||
				!level_texture_find(pak, level.materials[material], texture, &source, &texture_reason,
					!strncmp(level.materials[material], "t_", 2) || !strncmp(level.materials[material], "ter_", 4) ?
						SABER_LEVEL_GROUND_BASE_LARGEST : SABER_LEVEL_TEXTURE_LARGEST))
			{
				slots[material] = -2;
			}
			else if (!bitmap_describe(texture, saber_level.bitmap_tag, &saber_level.bitmaps[saber_level.texture_count]))
			{
				saber_texture_dispose(texture);
				slots[material] = -2;
			}
			else
			{
				slots[material] = (short)saber_level.texture_count++;
				/* (a sky's alpha is its shape: a cloud's soft edge. Filling it
				in would draw the card solid. A star field's black, and a
				planet's clear card, are stored with no alpha that cuts them) */
				if (sky && strstr(level.materials[material], "ring") != NULL)
					saber_texture_ring_contrast(texture);
				/* (a glow drawn on a card with no alpha, a moon's aura or the
				sea's haze: its black is the sky behind it, so its brightness
				is made its alpha. A mountain's card is cut by its own) */
				/* (a cloud's card: its white and grey kept, the sky's blue
				painted behind them, and its alpha's hard edge, made clear) */
				if (sky && strncmp(level.materials[material], "cloud", 5) == 0)
				{
					/* (a sheet of cloud with no alpha, clouds_br_02, every
					texel solid: its brightness first, so its grey thins and
					the sheet's edge is not a hard line across the sky) */
					if (saber_texture_alpha_opaque(texture))
						saber_texture_glow_alpha(texture);
					saber_texture_cloud_alpha(texture);
				}
				else if (sky && strncmp(level.materials[material], "stars", 5) != 0 &&
					!strstr(level.materials[material], "mountain") && saber_texture_alpha_opaque(texture))
				{
					saber_texture_glow_alpha(texture);
				}
				/* (a planet's and a moon's own alpha is kept: in Anniversary's
				sky they are faint through the day's haze, the planet's night
				side the sky's blue) */
				else if (sky && (strstr(level.materials[material], "planet") != NULL ||
					strncmp(level.materials[material], "basis_sky", 9) == 0))
				{
				}
				/* (the stars faint in the day: their brightness their alpha,
				a third of it, blended over the day's blue) */
				else if (sky && strncmp(level.materials[material], "stars", 5) == 0)
				{
					saber_texture_glow_alpha(texture);
					saber_texture_scale_alpha(texture, 1, 3);
				}
				else if (!sky && !saber_texture_cutout(texture, material_foliage(level.materials[material])) &&
					!material_card(level.materials[material]))
				{
					saber_texture_make_solid(texture);
				}
				/* (the cloud layers stack: a dozen of them over one another,
				each drawn whole, covered the sky. Each is drawn at half its
				alpha, as the package's atmosphere thins them) */
				if (sky && strncmp(level.materials[material], "cloud", 5) == 0)
					saber_texture_scale_alpha(texture, 1, 2);
				/* (the ring pale in the haze, as Anniversary's) */
				if (sky && strstr(level.materials[material], "ring") != NULL)
					saber_texture_scale_alpha(texture, 2, 3);
				/* (described again: a glow's DXT1 was made a DXT5) */
				bitmap_describe(texture, saber_level.bitmap_tag, &saber_level.bitmaps[slots[material]]);
			}
		}
		if (!made->gradient && slots[material] < 0)
		{
			part->vertex_count = 0;
			continue;
		}
		made->texture = made->gradient ? gradient_slot : slots[material];
		made->uv_scale[0] = part->uv_scale[0] != 0.f ? part->uv_scale[0] : 1.f;
		made->uv_scale[1] = part->uv_scale[1] != 0.f ? part->uv_scale[1] : 1.f;
		/* (a sky mesh's coordinates were packed by its repeat: a moon's card
		holds 0..0.5, a cloud layer's thirds. Drawn by the shader's map scale) */
		if (sky && !made->gradient && part->uv_repeat > 1 && made->uv_scale[0] == 1.f && made->uv_scale[1] == 1.f)
		{
			made->uv_scale[0] = (real)part->uv_repeat;
			made->uv_scale[1] = (real)part->uv_repeat;
		}
		/* (a cloud, the stars, a planet and a moon are cut out: the card
		around the planet, and the black of the stars, are clear) */
		/* (ground, or of a cliff's or the ground's layered mesh a material laid
		as a layer, moss_02: its alpha its height, and lit as the ground) */
		/* (of the terrain's meshes alone: b30's ivy walls, cards of plants_01
		with plants_01 laid over them, drawn so were the leaves' dark green
		plates, whole) */
		made->terrain = part->terrain == 1 || (part->terrain == 2 && slots[3 * level.material_count + material] &&
			strstr(part->region, "(terrain)")) ? TRUE : FALSE;
		/* (a sky card that fades by its vertices: kept to be drawn so) */
		if (sky && !made->gradient && part->vertex_alpha)
		{
			long faded;

			made->fade_positions = malloc(sizeof(*made->fade_positions) * (size_t)part->vertex_count);
			made->fade_texcoords = malloc(sizeof(*made->fade_texcoords) * (size_t)part->vertex_count);
			made->fade_alpha = malloc((size_t)part->vertex_count);
			made->fade_triangles = malloc(sizeof(*made->fade_triangles) * 3 * (size_t)part->triangle_count);
			if (made->fade_positions && made->fade_texcoords && made->fade_alpha && made->fade_triangles)
			{
				for (faded = 0; faded < part->vertex_count; faded++)
				{
					made->fade_positions[faded][0] = part->vertices[faded].position.x;
					made->fade_positions[faded][1] = part->vertices[faded].position.y;
					made->fade_positions[faded][2] = part->vertices[faded].position.z;
					made->fade_texcoords[faded][0] = part->vertices[faded].texcoord.x * made->uv_scale[0];
					made->fade_texcoords[faded][1] = part->vertices[faded].texcoord.y * made->uv_scale[1];
					made->fade_alpha[faded] = part->vertex_alpha[faded];
				}
				memcpy(made->fade_triangles, part->triangles, sizeof(*made->fade_triangles) * 3 * (size_t)part->triangle_count);
				made->fade_triangle_count = part->triangle_count;
			}
			else
			{
				saber_level_layers_free(made);
			}
		}
		/* (by its alpha all but a hard surface's: a Forerunner panel's alpha
		is its shine, and cut by it a30's beam tower was see through, the sky
		behind its walls. A moss's, a far tree's, a liana's keep their holes) */
		made->cutout = made->gradient || made->terrain ? FALSE :
			(material_card(level.materials[material]) && !made->blend) ||
			(!sky && !level_material_hard(level.materials[material]) &&
				(!slots[3 * level.material_count + material] || !strstr(part->region, "(terrain)")) &&
				saber_texture_cutout(&saber_level.textures[made->texture],
					material_foliage(level.materials[material]))) ? TRUE : FALSE;
		/* (ground, a terrain's, is never cut out: its texture's alpha is its
		height, and cut by it, a grass triangle with nothing under it drew the
		void's black.) (and a structure's root or grass card, its foliage at least a
		twentieth clear: b30's trees are the structure's, their root cards drawn
		whole and grey. Not ground, which has layers over it: ter_grass_01's
		alpha is its height, and cut by it the ground has holes. Logged once a
		material, as the templates' are) */
		if (!made->gradient && !sky && !made->blend && material_foliage(level.materials[material]) &&
			!made->terrain && !part->layer_count && !slots[2 * level.material_count + material])
		{
			long clear = 0;
			long solid = 0;
			long texels = 0;

			saber_texture_alpha_counts(&saber_level.textures[made->texture], TRUE, &clear, &solid, &texels);
			if (!made->cutout && texels && clear * 20 >= texels)
				made->cutout = TRUE;
			slots[2 * level.material_count + material] = TRUE;
			platform_log("anniversary: the structure's %s (format %d): %ld%% clear, %ld%% solid, cut out %d",
				level.materials[material], (int)saber_level.textures[made->texture].format,
				texels ? clear * 100 / texels : 0, texels ? solid * 100 / texels : 0, (int)made->cutout);
		}
		else if (!made->gradient && !sky && !made->blend && material_foliage(level.materials[material]) &&
			!made->terrain && !part->layer_count && !made->cutout)
		{
			long clear = 0;
			long solid = 0;
			long texels = 0;

			saber_texture_alpha_counts(&saber_level.textures[made->texture], TRUE, &clear, &solid, &texels);
			if (texels && clear * 20 >= texels)
				made->cutout = TRUE;
		}
		made->lit_bsp = NONE;
		vertices_size += (unsigned long)part->vertex_count * (unsigned long)compressed_size +
			(unsigned long)part->triangle_count * 6 * sizeof(word);
		bounds.x0 = MIN(bounds.x0, made->bounds.x0);
		bounds.x1 = MAX(bounds.x1, made->bounds.x1);
		bounds.y0 = MIN(bounds.y0, made->bounds.y0);
		bounds.y1 = MAX(bounds.y1, made->bounds.y1);
		bounds.z0 = MIN(bounds.z0, made->bounds.z0);
		bounds.z1 = MAX(bounds.z1, made->bounds.z1);
		if (made->sky)
			sky_parts++;
		/* (not over a cut out part's: a layer drawn whole over a leaf's card
		would cover, or darken, what shows through around the leaf) */
		if (!made->sky && !made->cutout)
			saber_level_layers_make(pak, &level, part, slots, made);
		if (made->layer_count)
			layered_parts++;
		saber_level.part_count++;
	}
	reason = "none of its parts is drawn";
	if (!saber_level.part_count)
		goto refused;

	/* its bitmap: each texture a bitmap (the permutation of a shader
	reference, of rasterizer_model_draw) */
	saber_level.group.sequences.count = 0;
	saber_level.group.bitmaps.count = saber_level.texture_count;
	saber_level.group.bitmaps.address = saber_level.bitmaps;
	saber_level.group.bitmaps.definition = NULL;
	/* (the structure is drawn with the map's first model shader, changed
	colours and all, so its textures are found where the map's would be: they
	are the package's, and laid out as this build reads them) */
	for (index = 0; index < saber_level.texture_count; index++)
	{
		if (custom_edition_bitmap_channel_order(saber_level.bitmap_tag, index) != _custom_edition_channels_xbox)
			custom_edition_bitmap_order_set(saber_level.bitmap_tag, index, _custom_edition_channels_xbox);
	}

	/* its shaders: the map's first model shader's, its base map the bitmap */
	{
		struct tag_iterator iterator;
		long shader_tag;
		boolean found = FALSE;

		tag_iterator_new(&iterator, SHADER_DEFINITION_TAG);
		while (!found && (shader_tag = tag_iterator_next(&iterator)) != NONE)
		{
			struct shader const *shader = shader_definition_get(shader_tag);

			if (shader && shader->base.type == SHADER_TYPE_MODEL)
			{
				struct saber_shader const *played;

				saber_level.shaders[0] = *(struct shader_model_definition const *)shader;
				/* (its own definition, if a model drawn as a template changed it) */
				for (played = saber_models_globals.shaders; played; played = played->next)
				{
					if (played->tag_index == shader_tag)
						saber_level.shaders[0] = played->map_shader;
				}
				found = TRUE;
			}
		}
		reason = "the map has no model shader to draw it with";
		if (!found)
			goto refused;
	}
	{
		struct shader_model_properties *model = &saber_level.shaders[0].model;

		model->map_u_scale = 1.f;
		model->map_v_scale = 1.f;
		model->base_map.index = saber_level.bitmap_tag;
		model->multipurpose_map.index = NONE;
		model->detail_map.index = NONE;
		model->reflection_cube_map.index = NONE;
		model->change_color_source = 0;
		model->detail_mask = 0;
		model->self_illumination_color_source = 0;
		/* (the map's shader was lit by the cube map's reflections, which the
	package's textures carry themselves) */
		csmemset(&model->perpendicular_tint_color, 0, sizeof(model->perpendicular_tint_color));
		model->perpendicular_brightness = 0.f;
		model->parallel_brightness = 0.f;
		csmemset(&model->parallel_tint_color, 0, sizeof(model->parallel_tint_color));
		csmemset(&model->self_illumination_animation_color_lower_bound, 0,
			sizeof(model->self_illumination_animation_color_lower_bound));
		csmemset(&model->self_illumination_animation_color_upper_bound, 0,
			sizeof(model->self_illumination_animation_color_upper_bound));
		csmemset(&model->texture_animation, 0, sizeof(model->texture_animation));
		SET_FLAG(model->flags, SHADER_MODEL_ALPHA_BLENDED_DECAL_BIT, FALSE);
		SET_FLAG(model->flags, SHADER_MODEL_TWO_SIDED_BIT, FALSE);
		SET_FLAG(model->flags, SHADER_MODEL_NOT_ALPHA_TESTED_BIT, TRUE);
		saber_level.shaders[1] = saber_level.shaders[0];
		SET_FLAG(saber_level.shaders[1].model.flags, SHADER_MODEL_TWO_SIDED_BIT, TRUE);
		SET_FLAG(saber_level.shaders[1].model.flags, SHADER_MODEL_NOT_ALPHA_TESTED_BIT, FALSE);
	}

	/* its buffers: each part's of a block of its own, its uncompressed
	vertices freed once they are compressed */
	(void)vertices_size;
	{
		long made_index = 0;

		for (index = 0; index < level.part_count; index++)
		{
			struct saber_mesh_part *part = &level.parts[index];
			struct saber_level_part *made;
			long strip_count;

			if (part->vertex_count <= 0)
				continue;
			made = &saber_level.parts[made_index];
			reason = "out of memory";
			made->memory = malloc((size_t)part->vertex_count * (size_t)compressed_size +
				(size_t)part->triangle_count * 6 * sizeof(word) + 4);
			made->memory_contiguous = FALSE;
			if (!made->memory)
			{
				saber_level.part_count = made_index;
				goto refused;
			}
			vertices = (struct model_vertex_compressed *)made->memory;
			strips = (word *)((char *)made->memory + (unsigned long)part->vertex_count * (unsigned long)compressed_size);
			rasterizer_geometry_compress_vertices(_rasterizer_vertex_type_model_uncompressed, part->vertex_count, vertices,
				part->vertex_count * compressed_size, part->vertices, part->vertex_count * uncompressed_size);
			strip_count = part_strip(part, NULL, strips);
			reason = "the renderer cannot make its buffers";
			if (!rasterizer_vertex_buffer_new(&made->vertex_buffer, _rasterizer_vertex_type_model_compressed,
					part->vertex_count, vertices, part->vertex_count * compressed_size) ||
				!rasterizer_triangle_buffer_new(&made->triangle_buffer, _triangle_buffer_type_precompiled_strip,
					strip_count - 2, strips))
			{
				saber_level.part_count = made_index + 1;
				goto refused;
			}
			triangles += part->triangle_count;
			free(part->vertices);
			part->vertices = NULL;
			free(made->memory);
			made->memory = NULL;
			made_index++;
		}
		drawn = made_index;
	}
	saber_level_textures_fit();
	saber_level.drawn = TRUE;
	platform_log("anniversary: the structure drawn as %s: %ld parts, %ld of them the sky, %ld with layers blended over "
		"them, %ld triangles, %ld textures; its bounds %.1f %.1f %.1f to %.1f %.1f %.1f world units",
		saber_pak_entry_name(pak, entry), drawn, sky_parts, layered_parts, triangles, saber_level.texture_count, bounds.x0,
		bounds.y0, bounds.z0, bounds.x1, bounds.y1, bounds.z1);
	saber_template_dispose(&level);
	free(slots);

	return TRUE;

refused:
	platform_log("anniversary: the structure is drawn as the map's: %s", reason);
	saber_template_dispose(&level);
	if (slots)
		free(slots);
	saber_level_free(TRUE);

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
	if (!map_name || !*map_name || (!saber_toggle_models_wanted() && !config_boolean("display.remastered_bsp")))
	{
		return;
	}
	/* (ui.map, the main menu, stays the map's own graphics unless
	display.remastered_ui is on) */
	if (!_stricmp(map_name, "ui") && !config_boolean("display.remastered_ui"))
	{
		platform_log("anniversary: the main menu stays the map's (display.remastered_ui is off)");
		return;
	}
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
	if (saber_toggle_models_wanted())
	{
		struct tag_iterator iterator;
		char const *name = strrchr(map_name, '\\');
		long tag_index;
		long drawn = 0;
		long models = 0;

		name = name ? name + 1 : map_name;
		classic_models_keep(pak, name);
		tag_iterator_new(&iterator, MODELS_GROUP_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			models++;
			if (model_play(pak, tag_index, name))
				drawn++;
		}
		platform_log("anniversary: %ld of the map's %ld models drawn as templates", drawn, models);
	}
	/* (and its structure, display.remastered_bsp; Back switches the models
	alone, saber_toggle.c) */
	if (config_boolean("display.remastered_bsp"))
		saber_level_play(pak);
	/* (the package's copy of each shader taken once everything that draws with
	it has had its say: a shader two models share is changed by both, so the
	copy is what the last of them left it as, saber_toggle.c. Taken after the
	level, which takes a shader for itself) */
	{
		struct saber_shader *plugged;

		for (plugged = saber_models_globals.shaders; plugged; plugged = plugged->next)
			plugged->played = *(struct shader_model_definition *)shader_definition_get(plugged->tag_index);
	}
	saber_pak_close(pak);
	if (saber_models_globals.stream)
		saber_pak_close(saber_models_globals.stream);
	saber_models_globals.stream = NULL;
	saber_models_globals.stream_tried = FALSE;
	{
		long index;

		for (index = 0; index < saber_models_globals.ipak_count; index++)
			saber_pak_close(saber_models_globals.ipaks[index]);
	}
	saber_models_globals.ipak_count = 0;
	saber_models_globals.ipaks_tried = FALSE;
}

/* (Back, pressed: saber_toggle.c) the map's models drawn as the package's, or
as the map's own, by writing the tags: each model's geometries, regions and
shaders the model made for it, or the map's. Everything that draws a model
reads the tag, so none of them asks which is drawn. */
long saber_models_anniversary_set(
	int shown)
{
	struct saber_model *made;
	long models = 0;

	for (made = saber_models_globals.models; made; made = made->next)
	{
		struct model *definition = model_definition_get(made->tag_index);

		if (shown)
			*definition = made->model;
		else
		{
			*definition = made->map_model;
			/* (the map's own texture scale, which the template's overrides) */
			definition->base_map_scale.i = made->map_model.base_map_scale.i != 0.f ?
				made->map_model.base_map_scale.i : 1.f;
			definition->base_map_scale.j = made->map_model.base_map_scale.j != 0.f ?
				made->map_model.base_map_scale.j : 1.f;
		}
		models++;
	}
	/* (and their shaders: the map's own, with the map's models; as the
	package's models draw them, with the templates) */
	{
		struct saber_shader *shader;

		/* (the package's copy while the templates are drawn, the map's own
		while its models are: `map_shader` is what the shader drew with
		before a template took it, `played` what the templates drew with) */
		for (shader = saber_models_globals.shaders; shader; shader = shader->next)
			*(struct shader_model_definition *)shader_definition_get(shader->tag_index) = shown ? shader->played : shader->map_shader;
	}
	/* (and the bitmaps they draw with: the package's textures, or the map's
	own) */
	{
		struct saber_bitmap *bitmap;

		for (bitmap = saber_models_globals.bitmaps; bitmap; bitmap = bitmap->next)
		{
			struct bitmap_group *group = bitmap_group_get(bitmap->tag_index);

			/* (written every time, as the models and shaders are: a flag that
			says which is in place cannot be trusted to be in step with it) */
			bitmap->shown = shown ? TRUE : FALSE;
			if (shown)
			{
				*group = bitmap->map_group;
				group->sequences.count = 0;
				group->bitmaps.count = bitmap->count;
				group->bitmaps.address = bitmap->bitmaps;
				group->bitmaps.definition = NULL;
			}
			else
				*group = bitmap->classic_group;
		}
	}
	/* (and the textures of the map's own, not the package's that a texture in
	the cache still holds: they are read again, as the map's bitmaps are, and
their channels listed again as the map's) */
	texture_cache_flush();
	custom_edition_reordered_bitmaps_find_again();

	return models;
}

long saber_models_geometry_count(
	void)
{
	struct saber_model const *made;
	long geometries = 0;

	for (made = saber_models_globals.models; made; made = made->next)
		geometries += model_definition_get(made->tag_index)->geometries.count;

	return geometries;
}

void saber_models_tags_unloaded(
	int give_back)
{
	saber_level_free(give_back ? TRUE : FALSE);
	saber_models_globals.ranked_count = 0;
	saber_jaw_count = 0;
	if (saber_models_globals.kept_tags)
		free(saber_models_globals.kept_tags);
	saber_models_globals.kept_tags = NULL;
	saber_models_globals.kept_count = 0;
	saber_models_globals.scroll_shader_count = 0;
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

/* the rank an object is drawn as, by its colour (its first change colour,
of its actor variant's): blue minor, red major, dark spec ops, gold a
zealot's; the least, else */
static short object_rank(
	long object_index)
{
	struct object_datum *object = object_index == NONE ? NULL : object_try_and_get(object_index);
	real_rgb_color const *color;
	real high;
	real low;

	if (!object)
		return _saber_rank_minor;
	color = &object->object.base_change_colors[0];
	high = MAX(color->red, MAX(color->green, color->blue));
	low = MIN(color->red, MIN(color->green, color->blue));
	/* (dark is spec ops only when it is grey too: a major's red is dark,
	0.25 0.08 0.09 in b30, and his) */
	if (high < 0.08f)
		return _saber_rank_spec_ops;
	if (high - low < 0.25f * high)
		return high < 0.5f ? _saber_rank_spec_ops : _saber_rank_minor;
	if (color->blue >= high)
		return _saber_rank_minor;
	if (color->red >= high)
	{
		if (color->green < 0.35f * color->red)
			return _saber_rank_major;
		/* (a Grunt minor's orange is gold too: a model of no zealots draws
		it the least rank's) */
		return _saber_rank_commander;
	}

	return _saber_rank_minor;
}

void saber_models_jaw_pose(
	long model_tag,
	long object_index,
	struct real_matrix4x3 *relative_node_matrices,
	short *node_count)
{
	long at;
	struct saber_jaw const *jaw = NULL;
	real aperture = 0.f;
	real angle;
	real_matrix4x3 turn;
	real_point3d moved;

	for (at = 0; at < saber_jaw_count; at++)
	{
		if (saber_jaws[at].model_tag == model_tag)
		{
			jaw = &saber_jaws[at];
			break;
		}
	}
	if (!jaw || jaw->jaw_node != *node_count || jaw->head_node < 0 || jaw->head_node >= *node_count ||
		jaw->jaw_node >= MAXIMUM_NODES_PER_MODEL)
	{
		return;
	}
	if (object_index != NONE)
	{
		struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(object_index, _object_mask_unit);

		if (unit)
			aperture = unit->unit.mouth_aperture;
	}
	if (!(aperture > 0.f))
		aperture = 0.f;
	if (aperture > 1.f)
		aperture = 1.f;
	angle = aperture * SABER_JAW_OPEN_ANGLE;
	/* (turned about the hinge: p -> pivot + R (p - pivot)) */
	matrix4x3_rotation_from_axis_and_angle(&turn, &jaw->axis, (real)sin(angle), (real)cos(angle));
	matrix4x3_transform_point(&turn, &jaw->pivot, &moved);
	turn.position.x = jaw->pivot.x - moved.x;
	turn.position.y = jaw->pivot.y - moved.y;
	turn.position.z = jaw->pivot.z - moved.z;
	matrix4x3_multiply(&relative_node_matrices[jaw->head_node], &turn, &relative_node_matrices[jaw->jaw_node]);
	*node_count = (short)(jaw->jaw_node + 1);
}

int saber_models_part_solid(
	unsigned long part_flags)
{
	return (part_flags & SABER_PART_SOLID_FLAG) != 0;
}

int saber_models_part_region_hidden(
	unsigned long part_flags,
	void const *model_,
	char const *region_permutation_indices,
	short region_permutation_count)
{
	struct model const *model = (struct model const *)model_;
	long region_index;
	long permutation;
	boolean damaged;

	if (!model || !region_permutation_indices)
		return FALSE;
	/* (one variant of a group: drawn at the permutations of its mask) */
	if (part_flags & SABER_PART_VARIANT_FLAG)
	{
		unsigned long mask = (part_flags >> SABER_PART_VARIANT_SHIFT) & SABER_PART_VARIANT_MASK;

		region_index = (long)((part_flags >> SABER_PART_REGION_SHIFT) & SABER_PART_REGION_MASK);
		if (region_index >= model->regions.count || region_index >= region_permutation_count)
			return FALSE;
		permutation = region_permutation_indices[region_index];
		if (permutation < 0 || permutation >= 8)
			return FALSE;
		return (mask & (1UL << permutation)) == 0;
	}
	if (!(part_flags & SABER_PART_REGION_FLAG))
		return FALSE;
	region_index = (long)((part_flags >> SABER_PART_REGION_SHIFT) & SABER_PART_REGION_MASK);
	if (region_index >= model->regions.count || region_index >= region_permutation_count)
		return FALSE;
	permutation = region_permutation_indices[region_index];
	{
		struct model_region const *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);

		if (permutation < 0 || permutation >= region->permutations.count)
			return FALSE;
		/* (the region at its ~damaged (or ~destroyed): the damaged copy, else
		the whole one) */
		damaged = TAG_BLOCK_GET_ELEMENT(&region->permutations, permutation,
			struct model_region_permutation)->name[0] == '~';
	}

	return damaged != ((part_flags & SABER_PART_DAMAGED_FLAG) != 0);
}

int saber_models_part_hidden(
	unsigned long part_flags,
	long object_index)
{
	struct object_datum *object;
	boolean destroyed;

	if (!(part_flags & (SABER_PART_WRECK_FLAG | SABER_PART_WHOLE_FLAG)))
		return FALSE;
	/* (only a vehicle's: anything else is drawn whole) */
	object = object_index != NONE ?
		(struct object_datum *)object_try_and_get_and_verify_type(object_index, _object_mask_vehicle) : NULL;
	destroyed = object && TEST_FLAG(object->object.damage_flags, _object_dead_bit);

	return (part_flags & SABER_PART_WRECK_FLAG) ? !destroyed : destroyed;
}

int saber_models_part_cutout(
	long shader_tag,
	short permutation)
{
	struct shader const *shader;
	struct saber_bitmap const *played;

	if (shader_tag == NONE || permutation < 0)
		return FALSE;
	/* (the map's own models draw every part as they always did: no package
	cutout, nor blend, is theirs, saber_toggle.c) */
	if (saber_toggle_classic_models())
		return FALSE;
	shader = shader_definition_get(shader_tag);
	if (!shader || shader->base.type != SHADER_TYPE_MODEL)
		return FALSE;
	played = bitmap_played_get(((struct shader_model_definition const *)shader)->model.base_map.index);
	if (!played || permutation >= played->count || permutation >= MAXIMUM_SABER_BITMAP_TEXTURES)
		return FALSE;
	/* (a decal sheet's marks are cut out whichever model played it: a
	marine's UNSC patch drew its dark card) */
	if (strstr(played->names[permutation], "decal") != NULL)
		return TRUE;

	return played->cutouts[permutation] ? TRUE : FALSE;
}

int saber_models_part_blend(
	long shader_tag,
	short permutation)
{
	struct shader const *shader;
	struct saber_bitmap const *played;

	if (shader_tag == NONE || permutation < 0)
		return FALSE;
	/* (the map's own models draw every part as they always did: no package
	cutout, nor blend, is theirs, saber_toggle.c) */
	if (saber_toggle_classic_models())
		return FALSE;
	shader = shader_definition_get(shader_tag);
	if (!shader || shader->base.type != SHADER_TYPE_MODEL)
		return FALSE;
	played = bitmap_played_get(((struct shader_model_definition const *)shader)->model.base_map.index);
	if (!played || permutation >= played->count || permutation >= MAXIMUM_SABER_BITMAP_TEXTURES)
		return FALSE;

	return played->blends[permutation] ? TRUE : FALSE;
}

short saber_models_geometry_index(
	struct model const *model,
	long object_index,
	short geometry_index)
{
	struct saber_model const *made;

	if (!saber_models_globals.ranked_count || geometry_index == NONE)
		return geometry_index;
	for (made = saber_models_globals.models; made; made = made->next)
	{
		short rank;
		long rank_index;
		long chosen = NONE;

		/* (the map's own model's shaders are its own, and its geometries the
		templates' copies do not change) */
		if (made->rank_count <= 1 || model->geometries.address != made->geometries ||
			geometry_index < 0 || geometry_index >= made->geometry_count)
		{
			continue;
		}
		/* (a rank it has none of drawn as the nearest it has: stealth as
		spec ops, either as the least, any other as the least) */
		rank = object_rank(object_index);
		while (chosen == NONE)
		{
			for (rank_index = 0; rank_index < made->rank_count && made->ranks[rank_index] != rank; rank_index++)
			{
			}
			if (rank_index < made->rank_count)
				chosen = rank_index;
			else if (rank == _saber_rank_spec_ops)
				rank = _saber_rank_stealth;
			else if (rank != _saber_rank_minor)
				rank = _saber_rank_minor;
			else
				chosen = 0;
		}

		/* (the body's geometries, then each head permutation's, a rank's
		levels together) */
		return (short)((geometry_index / (made->level_count * made->rank_count)) *
			(made->level_count * made->rank_count) +
			chosen * made->level_count + geometry_index % made->level_count);
	}

	return geometry_index;
}

/* the shader permutation an object is drawn with: its actor variant's
forced one (an Elite major's 1, a commander's 2: the map's bitmaps of the
rank), but none while the template is drawn, whose shaders' permutations
are its own textures' places, the same for every rank */
/* the map's lights an Anniversary model has none of: hidden on an object
while its model is drawn as the template (a Banshee's wingtip lights,
glowing in the air off the remastered wings' tips) */
static char const *const saber_hidden_lights[] = {
	"vehicles\\banshee\\banshee wing",
};

int saber_models_light_hidden(
	long light_tag,
	long model_tag)
{
	struct saber_model const *made;
	char const *name;
	long index;

	if (light_tag == NONE || model_tag == NONE)
		return FALSE;
	name = tag_get_name(light_tag);
	if (!name)
		return FALSE;
	for (index = 0; index < NUMBEROF(saber_hidden_lights); index++)
	{
		if (!strcmp(name, saber_hidden_lights[index]))
			break;
	}
	if (index == NUMBEROF(saber_hidden_lights))
		return FALSE;
	for (made = saber_models_globals.models; made; made = made->next)
	{
		if (made->tag_index == model_tag)
			return model_definition_get(model_tag)->geometries.address == made->geometries;
	}

	return FALSE;
}

short saber_models_shader_permutation(
	long model_tag,
	short forced)
{
	struct saber_model const *made;

	if (!forced || model_tag == NONE)
		return forced;
	for (made = saber_models_globals.models; made; made = made->next)
	{
		/* (of a model of ranks alone, an Elite's, whose ranks are its own
		geometries: a Jackal's forced permutation is its shield's colour, of
		the map's own shield, which is drawn) */
		if (made->tag_index == model_tag)
			return made->rank_count > 1 && model_definition_get(model_tag)->geometries.address == made->geometries ?
				0 : forced;
	}

	return forced;
}

real_rgb_color const *saber_models_change_colors(
	long model_tag,
	long object_index,
	real_rgb_color const *colors)
{
	static real_rgb_color changed[MAXIMUM_CHANGE_COLORS_PER_MODEL];
	struct saber_model const *made;

	if (!colors || model_tag == NONE)
		return colors;
	for (made = saber_models_globals.models; made; made = made->next)
	{
		long index;

		if (made->tag_index != model_tag)
			continue;
		/* (while the template is drawn, and for the least rank alone: a major
		is red as its colour says) */
		if (!made->band_weight || model_definition_get(model_tag)->geometries.address != made->geometries ||
			object_rank(object_index) != _saber_rank_minor)
		{
			return colors;
		}
		for (index = 0; index < MAXIMUM_CHANGE_COLORS_PER_MODEL; index++)
			changed[index] = colors[index];
		changed[0] = made->band_color;
		return changed;
	}

	return colors;
}

int saber_models_level_drawn(
	void)
{
	return saber_level.drawn && !saber_toggle_classic_level();
}

int saber_models_level_sky_drawn(
	void)
{
	long index;

	if (!saber_level.drawn || saber_toggle_classic_level())
		return FALSE;
	for (index = 0; index < saber_level.part_count; index++)
	{
		if (saber_level.parts[index].sky)
			return TRUE;
	}

	return FALSE;
}

/* the order a sky part is drawn in, lowest first: the daytime sky, then
its sorting_transparent_group (a part with none after the dome) */
static long saber_level_sky_order(
	struct saber_level_part const *part)
{
	if (part->gradient)
		return -2;
	if (part->sort_group != NONE)
		return part->sort_group;

	return part->dome ? 0 : 30;
}

/* A sky card that fades by its vertices drawn: its texture times its
vertices' alpha, blended over the sky, unlit (the sky is drawn in white
light), with no depth; at `origin` + its points times `scale`, as the sky's
are placed about the camera */
static void saber_level_sky_fade_draw(
	struct saber_level_part const *part,
	real_point3d const *origin,
	real scale)
{
	struct pixel_shader_definition shader;
	long corner;

	csmemset(&shader, 0, sizeof(shader));
	shader.texture_modes = 1;
	shader.combiner_count = 1;
	/* (r0 the texture's colour times the vertex's, r0.a its alpha times the
	vertex's; out r0) */
	shader.rgb_inputs[0] = 0x08040000;
	shader.rgb_outputs[0] = 0xc00;
	shader.alpha_inputs[0] = 0x18140000;
	shader.alpha_outputs[0] = 0xc00;
	shader.final_combiner_inputs_abcd = 0xc;
	shader.final_combiner_inputs_efg = 0x1c00;
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_NONE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHABLENDENABLE, TRUE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHATESTENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZWRITEENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZBIAS, 0);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	rasterizer_set_framebuffer_blend_function(0);
	rasterizer_set_vertex_shader_permutation(1, 10, 0);
	rasterizer_set_pixel_shader(&shader);
	if (rasterizer_set_texture_bitmap_data(0, &saber_level.bitmaps[part->texture]))
	{
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLELIST);
		for (corner = 0; corner < 3 * part->fade_triangle_count; corner++)
		{
			unsigned short vertex = part->fade_triangles[corner];

			IDirect3DDevice8_SetVertexData4ub(global_d3d_device, 9, 255, 255, 255, part->fade_alpha[vertex]);
			IDirect3DDevice8_SetVertexData2f(global_d3d_device, 4, part->fade_texcoords[vertex][0],
				part->fade_texcoords[vertex][1]);
			IDirect3DDevice8_SetVertexData4f(global_d3d_device, 0,
				origin->x + part->fade_positions[vertex][0] * scale,
				origin->y + part->fade_positions[vertex][1] * scale,
				origin->z + part->fade_positions[vertex][2] * scale, 1.f);
		}
		IDirect3DDevice8_End(global_d3d_device);
	}
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZENABLE, TRUE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZWRITEENABLE, TRUE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHABLENDENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALL);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_CCW);
}

/* the package's sky, in the map sky's place: full bright, with no fog and
no depth, so the structure drawn afterwards covers it. The sky is moved so
its dome is centred on the camera, then fitted inside the far plane: left
where the level puts it, the dome's rim crosses the view and its shell
covers the planet. The dome is drawn first, the clouds over it, and a
planet or a moon last */
void saber_models_level_sky_render(
	void)
{
	static real_rgb_color const colors[4] = { { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f } };
	static real const values[8] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	struct rasterizer_model_begin_parameters parameters;
	struct render_lighting lighting;
	real_matrix4x3 node;
	long order[MAXIMUM_SABER_SKY_PARTS];
	long sky_count;
	long index;
	long placed;
	long anchor_index;
	real_point3d anchor;
	real reach;
	real far_distance;
	real scale;

	if (!saber_level.drawn || saber_toggle_classic_level())
		return;
	sky_count = 0;
	anchor_index = NONE;
	for (index = 0; index < saber_level.part_count && sky_count < MAXIMUM_SABER_SKY_PARTS; index++)
	{
		if (!saber_level.parts[index].sky)
			continue;
		order[sky_count++] = index;
		if (anchor_index == NONE ||
			(saber_level.parts[index].dome && !saber_level.parts[anchor_index].dome) ||
			(saber_level.parts[index].dome == saber_level.parts[anchor_index].dome &&
				saber_level.parts[index].radius > saber_level.parts[anchor_index].radius))
		{
			anchor_index = index;
		}
	}
	if (!sky_count || anchor_index == NONE)
		return;
	/* (seen from the dome's middle at the horizon: from its box's middle,
	half way up, the horizon stood a third of the way up the sky and the
	mountains and the ring's feet sank under the ground) */
	anchor = saber_level.has_sky_anchor ? saber_level.sky_anchor : saber_level.parts[anchor_index].centroid;
	reach = 0.f;
	for (index = 0; index < sky_count; index++)
	{
		struct saber_level_part const *part = &saber_level.parts[order[index]];
		long corner;

		for (corner = 0; corner < 8; corner++)
		{
			real_vector3d offset;
			real distance;

			offset.i = ((corner & 1) ? part->bounds.x1 : part->bounds.x0) - anchor.x;
			offset.j = ((corner & 2) ? part->bounds.y1 : part->bounds.y0) - anchor.y;
			offset.k = ((corner & 4) ? part->bounds.z1 : part->bounds.z0) - anchor.z;
			distance = magnitude3d(&offset);
			if (distance > reach)
				reach = distance;
		}
	}
	far_distance = rasterizer_globals.far_clip_distance * 0.85f;
	if (far_distance < 16.f)
		far_distance = 16.f;
	scale = reach > far_distance ? far_distance / reach : 1.f;
	/* the daytime sky, then the objects in their sorting_transparent_group
	order (the stars, the moon, the ring, the clouds, the planet): with no
	depth a later part covers an earlier. Farther first within a group */
	for (placed = 1; placed < sky_count; placed++)
	{
		long item = order[placed];
		long slot = placed;
		long item_layer;
		real item_distance;
		real_vector3d offset;

		offset.i = saber_level.parts[item].centroid.x - anchor.x;
		offset.j = saber_level.parts[item].centroid.y - anchor.y;
		offset.k = saber_level.parts[item].centroid.z - anchor.z;
		item_distance = magnitude3d(&offset);
		item_layer = saber_level_sky_order(&saber_level.parts[item]);
		while (slot > 0)
		{
			long earlier = order[slot - 1];
			long earlier_layer;
			real earlier_distance;
			boolean move;

			offset.i = saber_level.parts[earlier].centroid.x - anchor.x;
			offset.j = saber_level.parts[earlier].centroid.y - anchor.y;
			offset.k = saber_level.parts[earlier].centroid.z - anchor.z;
			earlier_distance = magnitude3d(&offset);
			earlier_layer = saber_level_sky_order(&saber_level.parts[earlier]);
			move = item_layer < earlier_layer ||
				(item_layer == earlier_layer && earlier_distance < item_distance);
			if (!move)
				break;
			order[slot] = earlier;
			slot--;
		}
		order[slot] = item;
	}
	csmemset(&lighting, 0, sizeof(lighting));
	lighting.ambient_color = *global_real_rgb_white;
	csmemset(&node, 0, sizeof(node));
	node.forward.i = 1.f;
	node.left.j = 1.f;
	node.up.k = 1.f;
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.unique_identifier = NONE;
	parameters.skinning.node_matrices = &node;
	parameters.skinning.node_matrix_count = 1;
	parameters.animation.colors = colors;
	parameters.animation.values = values;
	parameters.base_map_scale.i = 1.f;
	parameters.base_map_scale.j = 1.f;
	parameters.lighting = lighting;
	parameters.geometry_flags = FLAG(SABER_GEOMETRY_NO_FOG_BIT) | FLAG(SABER_GEOMETRY_NO_DEPTH_BIT);

	rasterizer_models_begin(TRUE);
	for (placed = 0; placed < sky_count; placed++)
	{
		struct saber_level_part *part = &saber_level.parts[order[placed]];
		struct shader_model_definition *shader = &part->shader;
		real_rectangle3d scaled;

		/* camera + (point - anchor) * scale */
		scaled.x0 = render.camera.position.x + (part->bounds.x0 - anchor.x) * scale;
		scaled.x1 = render.camera.position.x + (part->bounds.x1 - anchor.x) * scale;
		scaled.y0 = render.camera.position.y + (part->bounds.y0 - anchor.y) * scale;
		scaled.y1 = render.camera.position.y + (part->bounds.y1 - anchor.y) * scale;
		scaled.z0 = render.camera.position.z + (part->bounds.z0 - anchor.z) * scale;
		scaled.z1 = render.camera.position.z + (part->bounds.z1 - anchor.z) * scale;
		if (!render_frustum_cube_visible(&render.frustum, &scaled, FALSE))
			continue;
		node.scale = scale;
		node.position.x = render.camera.position.x - anchor.x * scale;
		node.position.y = render.camera.position.y - anchor.y * scale;
		node.position.z = render.camera.position.z - anchor.z * scale;
		parameters.centroid.x = render.camera.position.x + (part->centroid.x - anchor.x) * scale;
		parameters.centroid.y = render.camera.position.y + (part->centroid.y - anchor.y) * scale;
		parameters.centroid.z = render.camera.position.z + (part->centroid.z - anchor.z) * scale;
		parameters.radius = part->radius * scale;
		*shader = saber_level.shaders[part->cutout ? 1 : 0];
		shader->model.map_u_scale = part->uv_scale[0];
		shader->model.map_v_scale = part->uv_scale[1];
		SET_FLAG(shader->model.flags, SHADER_MODEL_TWO_SIDED_BIT, TRUE);
		/* (its alpha blended, so the card fades. The test would cut it) */
		if (part->blend)
			SET_FLAG(shader->model.flags, SHADER_MODEL_ALPHA_BLENDED_DECAL_BIT, TRUE);
		if (part->fade_triangle_count > 0)
		{
			rasterizer_models_end();
			saber_level_sky_fade_draw(part, &node.position, scale);
			rasterizer_models_begin(TRUE);
			continue;
		}
		rasterizer_model_begin(&parameters, FALSE);
		rasterizer_model_draw((struct shader *)shader, part->texture,
			&part->triangle_buffer, NONE, part->triangle_buffer.count, &part->vertex_buffer, NONE);
		rasterizer_model_end();
	}
	rasterizer_models_end();
}

/* The light a level part's layers are drawn in at each vertex: of the
part's lighting (the structure's about it, as its base is drawn in), its
ambient and each distant light as the vertex faces it, of 255. */
/* A level part's light, sampled above its middle and above four points
between its middle and its corners, the brightest kept (above its middle
alone may be under a cliff or a rock); its brightness, or -1 with none */
static real saber_level_part_sample_light(
	struct saber_level_part const *part,
	struct render_lighting *lighting)
{
	real_point3d above;
	struct render_lighting sampled;
	real best = -1.f;
	long sample;

	csmemset(lighting, 0, sizeof(*lighting));
	for (sample = 0; sample < 5; sample++)
	{
		real brightness;
		long light;

		above = part->centroid;
		if (sample > 0)
		{
			above.x = (part->centroid.x + ((sample & 1) ? part->bounds.x0 : part->bounds.x1)) / 2.f;
			above.y = (part->centroid.y + ((sample & 2) ? part->bounds.y0 : part->bounds.y1)) / 2.f;
		}
		above.z = part->bounds.z1 + 0.1f;
		csmemset(&sampled, 0, sizeof(sampled));
		if (!lights_distant_lighting_at_point(0, &above, &sampled))
			continue;
		brightness = sampled.ambient_color.red + sampled.ambient_color.green + sampled.ambient_color.blue;
		for (light = 0; light < sampled.distant_light_count && light < MAXIMUM_RENDERED_DISTANT_LIGHTS; light++)
		{
			brightness += sampled.distant_lights[light].color.red + sampled.distant_lights[light].color.green +
				sampled.distant_lights[light].color.blue;
		}
		if (brightness > best)
		{
			best = brightness;
			*lighting = sampled;
		}
	}

	return best;
}

/* The ground's light in the structure `bsp_index`: of all the terrain's
parts' samples, the one most samples are nearest. A shade in the classic
structure's light (where its rocks and cliffs stood) is not the remastered
ground's, and the sunlit light most of the ground is in is drawn over it
all, as one */
static void saber_level_ground_light(
	short bsp_index)
{
	struct render_lighting best_lighting;
	real best = -1.f;
	long index;
	long counted = 0;
	real *brightnesses;
	struct render_lighting *lightings;

	saber_level.ground_lit = FALSE;
	saber_level.ground_lit_bsp = bsp_index;
	brightnesses = malloc(sizeof(*brightnesses) * ((size_t)saber_level.part_count + 1));
	lightings = malloc(sizeof(*lightings) * ((size_t)saber_level.part_count + 1));
	if (!brightnesses || !lightings)
	{
		if (brightnesses)
			free(brightnesses);
		if (lightings)
			free(lightings);
		return;
	}
	for (index = 0; index < saber_level.part_count; index++)
	{
		struct saber_level_part const *part = &saber_level.parts[index];
		real brightness;

		if (!part->terrain || part->sky)
			continue;
		brightness = saber_level_part_sample_light(part, &lightings[counted]);
		if (brightness < 0.f)
			continue;
		brightnesses[counted++] = brightness;
	}
	/* (the brightness three quarters of the samples are under: the sunlit
	ground's, not a lone bright spot's) */
	if (counted > 0)
	{
		long rank;
		long below;

		for (rank = 0; rank < counted; rank++)
		{
			below = 0;
			for (index = 0; index < counted && below <= (counted * 3) / 4; index++)
			{
				if (brightnesses[index] < brightnesses[rank])
					below++;
			}
			if (below <= (counted * 3) / 4 && brightnesses[rank] > best)
			{
				best = brightnesses[rank];
				best_lighting = lightings[rank];
			}
		}
	}
	if (best >= 0.f)
	{
		saber_level.ground_lighting = best_lighting;
		saber_level.ground_lighting.point_light_count = 0;
		saber_level.ground_lit = TRUE;
	}
	free(brightnesses);
	free(lightings);
}

static void saber_level_layers_light(
	struct saber_level_part *part)
{
	long vertex;

	for (vertex = 0; vertex < part->layer_vertex_count; vertex++)
	{
		real const *normal = part->layer_normals[vertex];
		real color[3];
		long light;
		long channel;

		color[0] = part->lighting.ambient_color.red;
		color[1] = part->lighting.ambient_color.green;
		color[2] = part->lighting.ambient_color.blue;
		for (light = 0; light < part->lighting.distant_light_count && light < MAXIMUM_RENDERED_DISTANT_LIGHTS;
			light++)
		{
			struct render_distant_light const *distant = &part->lighting.distant_lights[light];
			real facing = -(normal[0] * distant->direction.i + normal[1] * distant->direction.j +
				normal[2] * distant->direction.k);

			if (facing <= 0.f)
				continue;
			color[0] += distant->color.red * facing;
			color[1] += distant->color.green * facing;
			color[2] += distant->color.blue * facing;
		}
		/* (in the map's sun and sky, as a model is, darkened where the light
		Saber's exporter baked into the vertex is in shade: a cliff's shadow
		under its overhang, a crevice. Its sunlit sand's whole, the shaded
		cliff's about two thirds) */
		if (part->layer_baked)
		{
			unsigned char const *baked = part->layer_baked[vertex];
			long brightest = MAX(baked[0], MAX(baked[1], baked[2]));
			real shade = 0.3f + 0.7f * (real)brightest / 255.f;

			color[0] *= shade;
			color[1] *= shade;
			color[2] *= shade;
		}
		for (channel = 0; channel < 3; channel++)
		{
			real value = color[channel] * 255.f;

			part->layer_lit[vertex][channel] = (unsigned char)(value <= 0.f ? 0.f : value >= 255.f ? 255.f : value);
		}
	}
	part->layer_lit_bsp = part->lit_bsp;
	part->layer_lit_version++;
}

/* a point light as its definition gives its cone (rasterizer_xbox.c's) */
struct point_light_geometry_parameters
{
	real radius;
	real radius_modifier_lower_bound;
	real radius_modifier_upper_bound;
	real falloff_angle;
	real cutoff_angle;
	real lens_flare_radius;
	real runtime_cosine_falloff_angle;
	real runtime_cosine_cutoff_angle;
	real specular_radius_multiplier;
	real runtime_sine_cutoff_angle;
	long unused[2];
};

struct point_light_definition
{
	unsigned long flags;
	struct point_light_geometry_parameters geometry;
};

/* whether the frame's point light `light` (a flashlight, a plasma bolt's
glow) reaches the box `bounds` */
static boolean saber_level_light_reaches(
	struct rasterizer_light_submit_parameters const *light,
	real_rectangle3d const *bounds)
{
	real dx = light->position.x < bounds->x0 ? bounds->x0 - light->position.x :
		light->position.x > bounds->x1 ? light->position.x - bounds->x1 : 0.f;
	real dy = light->position.y < bounds->y0 ? bounds->y0 - light->position.y :
		light->position.y > bounds->y1 ? light->position.y - bounds->y1 : 0.f;
	real dz = light->position.z < bounds->z0 ? bounds->z0 - light->position.z :
		light->position.z > bounds->z1 ? light->position.z - bounds->z1 : 0.f;

	return light->radius > 0.f && dx * dx + dy * dy + dz * dz < light->radius * light->radius;
}

/* whether a point light's cone (a flashlight's) can reach the sphere about
a level part: the angle from its forward to the part's middle within its
cutoff and the angle the part spans there. A light with no cone reaches
all about it */
static boolean saber_level_light_cone_reaches(
	struct rasterizer_light_submit_parameters const *light,
	real_point3d const *centroid,
	real radius)
{
	real to[3];
	real distance;
	real along;
	real cutoff;
	real spread;

	if (!light->definition || light->definition->geometry.runtime_cosine_falloff_angle == -1.0f)
		return TRUE;
	to[0] = centroid->x - light->position.x;
	to[1] = centroid->y - light->position.y;
	to[2] = centroid->z - light->position.z;
	distance = square_root(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
	if (distance <= radius)
		return TRUE;
	along = (to[0] * light->forward.i + to[1] * light->forward.j + to[2] * light->forward.k) / distance;
	cutoff = light->definition->geometry.runtime_cosine_cutoff_angle;
	/* (the part's sine of its half angle there, a little more than the
	cosine it lowers the cutoff by) */
	spread = radius / distance;

	return along >= cutoff - spread;
}

/* the frame's point lights that reach a level part, as a model's lighting
names them (their rasterizer indices) */
static void saber_level_point_lights(
	struct saber_level_part const *part,
	struct render_lighting *lighting)
{
	long index;

	lighting->point_light_count = 0;
	for (index = 0; index < rasterizer_lights.light_count && lighting->point_light_count < MAXIMUM_RENDERED_POINT_LIGHTS;
		index++)
	{
		if (saber_level_light_reaches(&rasterizer_lights.lights[index], &part->bounds) &&
			saber_level_light_cone_reaches(&rasterizer_lights.lights[index], &part->centroid, part->radius))
		{
			lighting->point_light_indices[lighting->point_light_count++] = index;
		}
	}
}

/* a level part's layers' light this frame: their light (saber_level_layers_light)
and the frame's point lights' that reach it, a flashlight's cone, as a
model's vertex shader lights it (falling off to its radius, and from its
cone's falloff to its cutoff angle); NULL when none reaches it. Only the
vertices a light reaches are lit again: of a big cliff's thousands, the
flashlight's cone holds a few, and each one lit cost the frame */
static unsigned char (*saber_level_layers_lit_dynamic(
	struct saber_level_part const *part))[3]
{
	static unsigned char (*lit)[3] = NULL;
	static long lit_size = 0;
	long reaching[MAXIMUM_RENDERED_POINT_LIGHTS];
	long reaching_count = 0;
	long index;
	long vertex;

	for (index = 0; index < rasterizer_lights.light_count && reaching_count < MAXIMUM_RENDERED_POINT_LIGHTS; index++)
	{
		struct rasterizer_light_submit_parameters const *light = &rasterizer_lights.lights[index];

		if (saber_level_light_reaches(light, &part->bounds) &&
			saber_level_light_cone_reaches(light, &part->centroid, part->radius))
		{
			reaching[reaching_count++] = index;
		}
	}
	if (!reaching_count)
		return NULL;
	if (lit_size < part->layer_vertex_count)
	{
		unsigned char (*grown)[3] = realloc(lit, sizeof(*lit) * (size_t)part->layer_vertex_count);

		if (!grown)
			return NULL;
		lit = grown;
		lit_size = part->layer_vertex_count;
	}
	memcpy(lit, part->layer_lit, sizeof(*lit) * (size_t)part->layer_vertex_count);
	for (index = 0; index < reaching_count; index++)
	{
		struct rasterizer_light_submit_parameters const *light = &rasterizer_lights.lights[reaching[index]];
		real radius_squared = light->radius * light->radius;
		real inverse_radius_squared = 1.f / radius_squared;
		boolean coned = light->definition && light->definition->geometry.runtime_cosine_falloff_angle != -1.0f &&
			light->definition->geometry.runtime_cosine_falloff_angle !=
				light->definition->geometry.runtime_cosine_cutoff_angle;
		real cutoff = coned ? light->definition->geometry.runtime_cosine_cutoff_angle : -1.f;
		real cone_scale = coned ? 1.f / (light->definition->geometry.runtime_cosine_falloff_angle - cutoff) : 0.f;
		real red = light->color.red * 255.f;
		real green = light->color.green * 255.f;
		real blue = light->color.blue * 255.f;

		for (vertex = 0; vertex < part->layer_vertex_count; vertex++)
		{
			real const *at = part->layer_positions[vertex];
			real const *normal = part->layer_normals[vertex];
			real to[3];
			real distance_squared;
			real falloff;
			real facing;
			real inverse_distance;
			real strength;
			long channel;
			real added[3];

			/* (out of its radius, behind the surface, out of its cone: each
			tested before the square root) */
			to[0] = light->position.x - at[0];
			to[1] = light->position.y - at[1];
			to[2] = light->position.z - at[2];
			distance_squared = to[0] * to[0] + to[1] * to[1] + to[2] * to[2];
			if (distance_squared >= radius_squared || distance_squared <= 0.00000001f)
				continue;
			facing = normal[0] * to[0] + normal[1] * to[1] + normal[2] * to[2];
			if (facing <= 0.f)
				continue;
			strength = 1.f;
			if (coned)
			{
				real along = -(to[0] * light->forward.i + to[1] * light->forward.j + to[2] * light->forward.k);

				if (along <= 0.f || (cutoff > 0.f && along * along < cutoff * cutoff * distance_squared))
					continue;
				inverse_distance = 1.f / square_root(distance_squared);
				strength = (along * inverse_distance - cutoff) * cone_scale;
				if (strength <= 0.f)
					continue;
				if (strength > 1.f)
					strength = 1.f;
			}
			else
			{
				inverse_distance = 1.f / square_root(distance_squared);
			}
			falloff = 1.f - distance_squared * inverse_radius_squared;
			strength *= falloff * facing * inverse_distance;
			added[0] = red * strength;
			added[1] = green * strength;
			added[2] = blue * strength;
			for (channel = 0; channel < 3; channel++)
			{
				real value = (real)lit[vertex][channel] + added[channel];

				lit[vertex][channel] = (unsigned char)(value >= 255.f ? 255.f : value);
			}
		}
	}

	return lit;
}

/* room for `floats` floats, the vertices of a part's layer or fog as they
are sent to the card; NULL, none */
static float *saber_level_static_scratch(
	long floats)
{
	static float *scratch = NULL;
	static long scratch_size = 0;

	if (floats > scratch_size)
	{
		float *grown = realloc(scratch, sizeof(*scratch) * (size_t)floats);

		if (!grown)
			return NULL;
		scratch = grown;
		scratch_size = floats;
	}

	return scratch;
}

/* The level's covered parts' layers in batches, each the layers of one
place in their parts' layers (the first, the second...), one texture and
one blend, in one buffer on the card, and each frame the visible parts'
ranges of each drawn in one call; and their fog in one more. Drawn a part
and a layer at a time, b30 was some five thousand draws a frame, each its
state set again */
struct saber_level_batch
{
	short slot;
	short texture;
	boolean multiply;
	void *vertices;
	unsigned long vertex_count;
	unsigned long index_count;
	/* (this frame's ranges) */
	unsigned long *firsts;
	unsigned long *counts;
	long range_count;
	long range_capacity;
};

static struct
{
	boolean made;
	boolean failed;
	struct saber_level_batch *batches;
	long batch_count;
	struct saber_level_batch fog;
} saber_level_batches;

static boolean saber_level_batch_range(
	struct saber_level_batch *batch,
	unsigned long first,
	unsigned long count)
{
	if (batch->range_count == batch->range_capacity)
	{
		long capacity = batch->range_capacity ? 2 * batch->range_capacity : 64;
		unsigned long *firsts = realloc(batch->firsts, sizeof(*firsts) * (size_t)capacity);
		unsigned long *counts;

		if (!firsts)
			return FALSE;
		batch->firsts = firsts;
		counts = realloc(batch->counts, sizeof(*counts) * (size_t)capacity);
		if (!counts)
			return FALSE;
		batch->counts = counts;
		batch->range_capacity = capacity;
	}
	batch->firsts[batch->range_count] = first;
	batch->counts[batch->range_count] = count;
	batch->range_count++;

	return TRUE;
}

static void saber_level_batch_dispose(
	struct saber_level_batch *batch)
{
	if (batch->vertices)
		d3d8_gl_static_vertices_free(batch->vertices);
	if (batch->firsts)
		free(batch->firsts);
	if (batch->counts)
		free(batch->counts);
	csmemset(batch, 0, sizeof(*batch));
}

static void saber_level_batches_free(
	void)
{
	long index;

	for (index = 0; index < saber_level_batches.batch_count; index++)
		saber_level_batch_dispose(&saber_level_batches.batches[index]);
	if (saber_level_batches.batches)
		free(saber_level_batches.batches);
	saber_level_batch_dispose(&saber_level_batches.fog);
	csmemset(&saber_level_batches, 0, sizeof(saber_level_batches));
}

/* whether a part's layers are batched: its first covers it */
static boolean saber_level_part_batched(
	struct saber_level_part const *part)
{
	return part->layer_count > 0 && part->layer_cover == 0 && !part->sky && part->layer_positions &&
		part->layer_vertex_count > 0;
}

/* a part's layer's vertices as the card is sent them (12 floats each: its
position, texture coordinates and colour, its light (white, multiplying)
and its weight inverted) */
static void saber_level_layer_pack(
	struct saber_level_part const *part,
	long layer,
	unsigned char (*lit)[3],
	float *packed)
{
	unsigned char const *weights = part->layer_weights[layer];
	boolean multiply = part->layer_multiply[layer];
	long vertex;

	for (vertex = 0; vertex < part->layer_vertex_count; vertex++)
	{
		float *out = packed + vertex * 12;

		out[0] = part->layer_positions[vertex][0];
		out[1] = part->layer_positions[vertex][1];
		out[2] = part->layer_positions[vertex][2];
		out[3] = 1.f;
		out[4] = part->layer_texcoords[layer][vertex][0];
		out[5] = part->layer_texcoords[layer][vertex][1];
		out[6] = 0.f;
		out[7] = 1.f;
		out[8] = multiply || !lit ? 1.f : (float)lit[vertex][0] / 255.f;
		out[9] = multiply || !lit ? 1.f : (float)lit[vertex][1] / 255.f;
		out[10] = multiply || !lit ? 1.f : (float)lit[vertex][2] / 255.f;
		out[11] = (float)(255 - weights[vertex]) / 255.f;
	}
}

/* makes the batches, the first time the layers are drawn; FALSE, none */
static boolean saber_level_batches_make(
	void)
{
	long index;
	long batch_index;
	unsigned long fog_vertices = 0;
	unsigned long fog_indices = 0;

	if (saber_level_batches.made)
		return !saber_level_batches.failed;
	saber_level_batches.made = TRUE;
	saber_level_batches.failed = TRUE;
	/* (each part's layers counted into their batches) */
	for (index = 0; index < saber_level.part_count; index++)
	{
		struct saber_level_part *part = &saber_level.parts[index];
		long layer;

		if (!saber_level_part_batched(part))
			continue;
		for (layer = 0; layer < part->layer_count; layer++)
		{
			struct saber_level_batch *batch = NULL;

			for (batch_index = 0; batch_index < saber_level_batches.batch_count; batch_index++)
			{
				batch = &saber_level_batches.batches[batch_index];
				if (batch->slot == layer && batch->texture == part->layer_textures[layer] &&
					batch->multiply == part->layer_multiply[layer])
				{
					break;
				}
			}
			if (batch_index == saber_level_batches.batch_count)
			{
				struct saber_level_batch *grown = realloc(saber_level_batches.batches,
					sizeof(*grown) * (size_t)(saber_level_batches.batch_count + 1));

				if (!grown)
					return FALSE;
				saber_level_batches.batches = grown;
				batch = &grown[saber_level_batches.batch_count++];
				csmemset(batch, 0, sizeof(*batch));
				batch->slot = (short)layer;
				batch->texture = part->layer_textures[layer];
				batch->multiply = part->layer_multiply[layer];
			}
			part->batch[layer] = batch_index;
			part->batch_vertex[layer] = batch->vertex_count;
			part->batch_index[layer] = batch->index_count;
			batch->vertex_count += (unsigned long)part->layer_vertex_count;
			batch->index_count += 3 * (unsigned long)part->layer_triangle_counts[layer];
			part->layer_vertices_version[layer] = -2;
		}
		part->fog_batch_vertex = fog_vertices;
		part->fog_batch_index = fog_indices;
		fog_vertices += (unsigned long)part->layer_vertex_count;
		fog_indices += 3 * (unsigned long)part->layer_triangle_counts[0];
	}
	/* (each batch's vertices and indices sent, one at a time) */
	for (batch_index = 0; batch_index < saber_level_batches.batch_count; batch_index++)
	{
		struct saber_level_batch *batch = &saber_level_batches.batches[batch_index];
		float *packed = malloc(sizeof(*packed) * 12 * (size_t)batch->vertex_count);
		unsigned long *indices = malloc(sizeof(*indices) * (size_t)batch->index_count);

		if (packed && indices)
		{
			for (index = 0; index < saber_level.part_count; index++)
			{
				struct saber_level_part *part = &saber_level.parts[index];
				long layer;
				long corner;

				if (!saber_level_part_batched(part))
					continue;
				for (layer = 0; layer < part->layer_count; layer++)
				{
					if (part->batch[layer] != batch_index)
						continue;
					saber_level_layer_pack(part, layer, NULL, packed + 12 * part->batch_vertex[layer]);
					for (corner = 0; corner < 3 * part->layer_triangle_counts[layer]; corner++)
					{
						indices[part->batch_index[layer] + corner] =
							part->batch_vertex[layer] + part->layer_triangles[layer][corner];
					}
				}
			}
			batch->vertices = d3d8_gl_static_vertices_new32(FLAG(0) | FLAG(4) | FLAG(9), packed, batch->vertex_count,
				indices, batch->index_count);
		}
		if (packed)
			free(packed);
		if (indices)
			free(indices);
		if (!batch->vertices)
		{
			saber_level_batches_free();
			saber_level_batches.made = TRUE;
			saber_level_batches.failed = TRUE;
			return FALSE;
		}
	}
	/* (and the fog's: positions, and the fog's colour and density, sent as
	the camera moves) */
	if (fog_vertices > 0)
	{
		float *packed = calloc((size_t)fog_vertices * 8, sizeof(*packed));
		unsigned long *indices = malloc(sizeof(*indices) * (size_t)fog_indices);

		if (packed && indices)
		{
			for (index = 0; index < saber_level.part_count; index++)
			{
				struct saber_level_part *part = &saber_level.parts[index];
				long vertex;
				long corner;

				if (!saber_level_part_batched(part))
					continue;
				for (vertex = 0; vertex < part->layer_vertex_count; vertex++)
				{
					float *out = packed + 8 * (part->fog_batch_vertex + (unsigned long)vertex);

					out[0] = part->layer_positions[vertex][0];
					out[1] = part->layer_positions[vertex][1];
					out[2] = part->layer_positions[vertex][2];
					out[3] = 1.f;
				}
				for (corner = 0; corner < 3 * part->layer_triangle_counts[0]; corner++)
					indices[part->fog_batch_index + corner] = part->fog_batch_vertex + part->layer_triangles[0][corner];
				part->fog_batched = FALSE;
			}
			saber_level_batches.fog.vertices = d3d8_gl_static_vertices_new32(FLAG(0) | FLAG(9), packed, fog_vertices,
				indices, fog_indices);
			saber_level_batches.fog.vertex_count = fog_vertices;
			saber_level_batches.fog.index_count = fog_indices;
		}
		if (packed)
			free(packed);
		if (indices)
			free(indices);
	}
	saber_level_batches.failed = FALSE;
	platform_log("anniversary: the structure's layers in %ld batches (%s fog)", saber_level_batches.batch_count,
		saber_level_batches.fog.vertices ? "and their" : "without");

	return TRUE;
}

/* a part's layers this frame into their batches, their light sent again
where it changed (lit, this frame's) */
static void saber_level_layers_batch(
	struct saber_level_part *part,
	unsigned char (*lit)[3])
{
	long version = lit == part->layer_lit ? part->layer_lit_version : -1;
	long layer;

	for (layer = 0; layer < part->layer_count; layer++)
	{
		struct saber_level_batch *batch = &saber_level_batches.batches[part->batch[layer]];

		if (part->layer_vertices_version[layer] != version || version == -1)
		{
			float *packed = saber_level_static_scratch(part->layer_vertex_count * 12);

			if (packed)
			{
				saber_level_layer_pack(part, layer, lit, packed);
				d3d8_gl_static_vertices_update_range(batch->vertices, packed, part->batch_vertex[layer],
					(unsigned long)part->layer_vertex_count);
				part->layer_vertices_version[layer] = version;
			}
		}
		saber_level_batch_range(batch, part->batch_index[layer], 3 * (unsigned long)part->layer_triangle_counts[layer]);
	}
}

/* a part's fog this frame into its batch, sent again as the camera moves */
static void saber_level_fog_batch(
	struct saber_level_part *part)
{
	struct render_fog const *fog = &render.fog;
	real span = fog->atmospheric_maximum_distance - fog->atmospheric_minimum_distance;
	real dx;
	real dy;
	real dz;
	real mx = render.camera.position.x - part->fog_camera.x;
	real my = render.camera.position.y - part->fog_camera.y;
	real mz = render.camera.position.z - part->fog_camera.z;

	if (!saber_level_batches.fog.vertices || fog->atmospheric_maximum_density <= 0.f ||
		fog->atmospheric_maximum_distance <= 0.f || span <= 0.f)
	{
		return;
	}
	dx = render.camera.position.x < part->bounds.x0 ? part->bounds.x0 - render.camera.position.x :
		render.camera.position.x > part->bounds.x1 ? render.camera.position.x - part->bounds.x1 : 0.f;
	dy = render.camera.position.y < part->bounds.y0 ? part->bounds.y0 - render.camera.position.y :
		render.camera.position.y > part->bounds.y1 ? render.camera.position.y - part->bounds.y1 : 0.f;
	dz = render.camera.position.z < part->bounds.z0 ? part->bounds.z0 - render.camera.position.z :
		render.camera.position.z > part->bounds.z1 ? render.camera.position.z - part->bounds.z1 : 0.f;
	if (dx * dx + dy * dy + dz * dz <= fog->atmospheric_minimum_distance * fog->atmospheric_minimum_distance)
		return;
	if (!part->fog_batched || mx * mx + my * my + mz * mz > 0.01f)
	{
		float *packed = saber_level_static_scratch(part->layer_vertex_count * 8);
		float red = PIN(fog->atmospheric_color.red, 0.f, 1.f);
		float green = PIN(fog->atmospheric_color.green, 0.f, 1.f);
		float blue = PIN(fog->atmospheric_color.blue, 0.f, 1.f);
		long vertex;

		if (!packed)
			return;
		for (vertex = 0; vertex < part->layer_vertex_count; vertex++)
		{
			real const *at = part->layer_positions[vertex];
			real ox = at[0] - render.camera.position.x;
			real oy = at[1] - render.camera.position.y;
			real oz = at[2] - render.camera.position.z;
			real density = (square_root(ox * ox + oy * oy + oz * oz) - fog->atmospheric_minimum_distance) / span;
			float *out = packed + vertex * 8;

			density = PIN(density, 0.f, 1.f) * fog->atmospheric_maximum_density;
			out[0] = at[0];
			out[1] = at[1];
			out[2] = at[2];
			out[3] = 1.f;
			out[4] = red;
			out[5] = green;
			out[6] = blue;
			out[7] = PIN(density, 0.f, 1.f);
		}
		d3d8_gl_static_vertices_update_range(saber_level_batches.fog.vertices, packed, part->fog_batch_vertex,
			(unsigned long)part->layer_vertex_count);
		part->fog_batched = TRUE;
		part->fog_camera = render.camera.position;
	}
	saber_level_batch_range(&saber_level_batches.fog, part->fog_batch_index,
		3 * (unsigned long)part->layer_triangle_counts[0]);
}

/* the layer shader: a multiplying layer's or a blended one's (see
saber_level_layers_render) */
static void saber_level_layer_shader_set(
	struct pixel_shader_definition *shader,
	boolean multiply);

/* Draws a level part's layer of its vertices kept on the card, made the
first time and sent again as its light changes (lit, this frame's: its
own, or its own and the frame's point lights'): its position, texture
coordinates and colour (its light, or white for a multiplying layer; its
alpha its weight inverted), as the immediate-mode draw gives them. FALSE,
not drawn: the caller draws it in immediate mode */
static boolean saber_level_layer_static_draw(
	struct saber_level_part *part,
	long layer,
	unsigned char (*lit)[3])
{
	unsigned long const used = FLAG(0) | FLAG(4) | FLAG(9);
	long version = lit == part->layer_lit ? part->layer_lit_version : -1;

	if (part->layer_vertices_failed)
		return FALSE;
	if (!part->layer_vertices[layer] || version == -1 || part->layer_vertices_version[layer] != version)
	{
		float *packed = saber_level_static_scratch(part->layer_vertex_count * 12);
		unsigned char const *weights = part->layer_weights[layer];
		boolean multiply = part->layer_multiply[layer];
		long vertex;

		if (!packed)
			return FALSE;
		for (vertex = 0; vertex < part->layer_vertex_count; vertex++)
		{
			float *out = packed + vertex * 12;

			out[0] = part->layer_positions[vertex][0];
			out[1] = part->layer_positions[vertex][1];
			out[2] = part->layer_positions[vertex][2];
			out[3] = 1.f;
			out[4] = part->layer_texcoords[layer][vertex][0];
			out[5] = part->layer_texcoords[layer][vertex][1];
			out[6] = 0.f;
			out[7] = 1.f;
			out[8] = multiply ? 1.f : (float)lit[vertex][0] / 255.f;
			out[9] = multiply ? 1.f : (float)lit[vertex][1] / 255.f;
			out[10] = multiply ? 1.f : (float)lit[vertex][2] / 255.f;
			out[11] = (float)(255 - weights[vertex]) / 255.f;
		}
		if (!part->layer_vertices[layer])
		{
			part->layer_vertices[layer] = d3d8_gl_static_vertices_new(used, packed,
				(unsigned long)part->layer_vertex_count, part->layer_triangles[layer],
				3 * (unsigned long)part->layer_triangle_counts[layer]);
			if (!part->layer_vertices[layer])
			{
				part->layer_vertices_failed = TRUE;
				return FALSE;
			}
		}
		else
		{
			d3d8_gl_static_vertices_update(part->layer_vertices[layer], packed);
		}
		part->layer_vertices_version[layer] = version;
	}

	return d3d8_gl_static_vertices_draw(part->layer_vertices[layer], used) ? TRUE : FALSE;
}

/* The sky's fog over a level part's layers, which cover its base: the
fog's colour blended over each vertex by its depth in the fog, as the
map's atmospheric fog is (none to its start, its maximum density at its
opaque distance), of the triangles of the layer that covers it */
static void saber_level_layers_fog(
	struct saber_level_part const *part,
	struct pixel_shader_definition *shader)
{
	struct render_fog const *fog = &render.fog;
	unsigned short const *triangles = part->layer_triangles[part->layer_cover];
	real span = fog->atmospheric_maximum_distance - fog->atmospheric_minimum_distance;
	unsigned char red = (unsigned char)PIN(fog->atmospheric_color.red * 255.f, 0.f, 255.f);
	unsigned char green = (unsigned char)PIN(fog->atmospheric_color.green * 255.f, 0.f, 255.f);
	unsigned char blue = (unsigned char)PIN(fog->atmospheric_color.blue * 255.f, 0.f, 255.f);
	real dx;
	real dy;
	real dz;
	long corner;

	if (fog->atmospheric_maximum_density <= 0.f || fog->atmospheric_maximum_distance <= 0.f || span <= 0.f ||
		!triangles || part->layer_triangle_counts[part->layer_cover] <= 0)
	{
		return;
	}
	/* (none where the part is nearer than the fog's start) */
	dx = render.camera.position.x < part->bounds.x0 ? part->bounds.x0 - render.camera.position.x :
		render.camera.position.x > part->bounds.x1 ? render.camera.position.x - part->bounds.x1 : 0.f;
	dy = render.camera.position.y < part->bounds.y0 ? part->bounds.y0 - render.camera.position.y :
		render.camera.position.y > part->bounds.y1 ? render.camera.position.y - part->bounds.y1 : 0.f;
	dz = render.camera.position.z < part->bounds.z0 ? part->bounds.z0 - render.camera.position.z :
		render.camera.position.z > part->bounds.z1 ? render.camera.position.z - part->bounds.z1 : 0.f;
	if (dx * dx + dy * dy + dz * dz <= fog->atmospheric_minimum_distance * fog->atmospheric_minimum_distance)
		return;
	csmemset(shader, 0, sizeof(*shader));
	shader->texture_modes = 0;
	shader->combiner_count = 1;
	/* (the vertex's colour, the fog's, and its alpha, the fog's density) */
	shader->rgb_inputs[0] = 0x04200000;
	shader->alpha_inputs[0] = 0x14200000;
	shader->rgb_outputs[0] = 0xc00;
	shader->alpha_outputs[0] = 0xc00;
	shader->final_combiner_inputs_abcd = 0xc;
	shader->final_combiner_inputs_efg = 0x1c00;
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	rasterizer_set_framebuffer_blend_function(0);
	rasterizer_set_pixel_shader(shader);
	/* (kept on the card, made again as the camera moves a tenth of a unit:
	the fog changes little in that) */
	{
		struct saber_level_part *changed = (struct saber_level_part *)part;
		real mx = render.camera.position.x - part->fog_camera.x;
		real my = render.camera.position.y - part->fog_camera.y;
		real mz = render.camera.position.z - part->fog_camera.z;
		boolean moved = !part->fog_vertices || mx * mx + my * my + mz * mz > 0.01f;
		float *packed = NULL;
		long vertex;

		if (moved && !part->layer_vertices_failed)
			packed = saber_level_static_scratch(part->layer_vertex_count * 8);
		if (packed)
		{
			for (vertex = 0; vertex < part->layer_vertex_count; vertex++)
			{
				real const *at = part->layer_positions[vertex];
				real ox = at[0] - render.camera.position.x;
				real oy = at[1] - render.camera.position.y;
				real oz = at[2] - render.camera.position.z;
				real density = (square_root(ox * ox + oy * oy + oz * oz) - fog->atmospheric_minimum_distance) / span;
				float *out = packed + vertex * 8;

				density = PIN(density, 0.f, 1.f) * fog->atmospheric_maximum_density;
				out[0] = at[0];
				out[1] = at[1];
				out[2] = at[2];
				out[3] = 1.f;
				out[4] = (float)red / 255.f;
				out[5] = (float)green / 255.f;
				out[6] = (float)blue / 255.f;
				out[7] = PIN(density, 0.f, 1.f);
			}
			if (!part->fog_vertices)
			{
				changed->fog_vertices = d3d8_gl_static_vertices_new(FLAG(0) | FLAG(9), packed,
					(unsigned long)part->layer_vertex_count, triangles,
					3 * (unsigned long)part->layer_triangle_counts[part->layer_cover]);
				if (!changed->fog_vertices)
					changed->layer_vertices_failed = TRUE;
			}
			else
			{
				d3d8_gl_static_vertices_update(part->fog_vertices, packed);
			}
			changed->fog_camera = render.camera.position;
		}
		if (part->fog_vertices && d3d8_gl_static_vertices_draw(part->fog_vertices, FLAG(0) | FLAG(9)))
			return;
	}
	IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLELIST);
	for (corner = 0; corner < 3 * part->layer_triangle_counts[part->layer_cover]; corner++)
	{
		unsigned short vertex = triangles[corner];
		real const *at = part->layer_positions[vertex];
		real ox = at[0] - render.camera.position.x;
		real oy = at[1] - render.camera.position.y;
		real oz = at[2] - render.camera.position.z;
		real distance = square_root(ox * ox + oy * oy + oz * oz);
		real density = (distance - fog->atmospheric_minimum_distance) / span;

		density = PIN(density, 0.f, 1.f) * fog->atmospheric_maximum_density;
		IDirect3DDevice8_SetVertexData4ub(global_d3d_device, 9, red, green, blue,
			(unsigned char)PIN(density * 255.f, 0.f, 255.f));
		IDirect3DDevice8_SetVertexData2f(global_d3d_device, 4, 0.f, 0.f);
		IDirect3DDevice8_SetVertexData4f(global_d3d_device, 0, at[0], at[1], at[2], 1.f);
	}
	IDirect3DDevice8_End(global_d3d_device);
}

static void saber_level_layer_shader_set(
	struct pixel_shader_definition *shader,
	boolean multiply)
{
	csmemset(shader, 0, sizeof(*shader));
	shader->texture_modes = 1;
	shader->combiner_count = 2;
	shader->rgb_outputs[0] = 0xc00;
	shader->rgb_outputs[1] = 0xc00;
	shader->alpha_outputs[1] = 0xc00;
	shader->final_combiner_inputs_abcd = 0xc;
	shader->final_combiner_inputs_efg = 0x1c00;
	if (multiply)
	{
		/* (as a decal's multiply: white where it weighs nothing,
		the texture where it weighs fully, the framebuffer times
		it) */
		shader->rgb_inputs[0] = 0x28240820;
		shader->rgb_inputs[1] = 0x340c1420;
		shader->alpha_inputs[1] = 0x341c1420;
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_COLORWRITEENABLE,
			D3DCOLORWRITEENABLE_ALL);
		/* (1: the shader tags' multiply) */
		rasterizer_set_framebuffer_blend_function(1);
	}
	else
	{
		/* (as a decal's alpha blend: the colour the texture's times
		the vertex's, the alpha the texture's times the vertex's
		inverted) */
		shader->rgb_inputs[0] = 0x08040000;
		shader->rgb_inputs[1] = 0x200c0000;
		/* (its cover by its weight w and the texture's height h,
		a height blend: clamp(4w + 2h - 2.5). Nothing where it weighs
		nothing, whole where it weighs fully, and between them the
		texture's high places first, so the edge between two layers
		follows the rock and sand rather than the triangles. Of
		(1 - v0.a) w, t0.a h and c0.a a half: first 2w + h - 1,
		of w + h/2 biased and doubled; then twice that, biased) */
		shader->constant_0[0] = 0x80000000;
		shader->alpha_inputs[0] = 0x34201811;
		shader->alpha_outputs[0] = 0x18c00;
		/* (and no more: clamp(2w + h - 1), half as steep. Doubled
		again, a layer was whole a third of the way into a triangle
		and nothing short of it, and the base where no layer weighs
		stood out of the layers as a flat slab, edged nearly as the
		triangles are) */
		shader->alpha_inputs[1] = 0x1c200000;
		shader->alpha_outputs[1] = 0xc00;
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_COLORWRITEENABLE,
			D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
		/* (0: the shader tags' alpha blend, as hud_draw.c names it) */
		rasterizer_set_framebuffer_blend_function(0);
	}
	rasterizer_set_pixel_shader(shader);
}

/* Draws the level's terrains' layers over their bases (sand over a
cliff), each texture lit as the vertex is and blended by the vertex's
weight, as Halo draws a decal: its vertex shader of decals, alpha
blended, the depth tested and not written. */
static void saber_level_layers_render(
	void)
{
	struct pixel_shader_definition shader;
	long index;
	boolean begun = FALSE;
	/* (the kind of layer the state is set for: NONE, none yet) */
	long multiplying = NONE;
	/* (logged the first frame: what of the layers is drawn) */
	static boolean logged = FALSE;
	long parts_layered = 0;
	long parts_unlit = 0;
	long layers_drawn = 0;
	long layers_untextured = 0;
	long triangles_drawn = 0;
	/* (whether the layer drawn writes the depth: a covering layer's does) */
	boolean depth_written = FALSE;
	/* (whether the covered parts' layers are drawn in batches) */
	boolean batched = FALSE;

	for (index = 0; index < saber_level.part_count; index++)
	{
		struct saber_level_part *part = &saber_level.parts[index];
		unsigned char (*frame_lit)[3];
		boolean covered;
		long layer;

		if (part->layer_count && !part->sky)
		{
			parts_layered++;
			if (part->lit_bsp == NONE)
				parts_unlit++;
		}
		if (!part->layer_count || part->sky || part->lit_bsp == NONE ||
			!render_frustum_cube_visible(&render.frustum, &part->bounds, FALSE))
		{
			continue;
		}
		if (!begun)
		{
			begun = TRUE;
			IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_NONE);
			IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHABLENDENABLE, TRUE);
			IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHATESTENABLE, FALSE);
			IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZENABLE, TRUE);
			IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZWRITEENABLE, FALSE);
			IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZBIAS, rasterizer_debug_options.zbias);
			rasterizer_set_vertex_shader_permutation(1, 10, 0);
			batched = saber_level_batches_make();
		}
		if (part->layer_lit_bsp != part->lit_bsp)
			saber_level_layers_light(part);
		/* (and the frame's point lights', the flashlight's, where they reach) */
		frame_lit = saber_level_layers_lit_dynamic(part);
		if (!frame_lit)
			frame_lit = part->layer_lit;
		covered = part->layer_cover == 0;
		if (covered && batched && saber_level_part_batched(part))
		{
			saber_level_layers_batch(part, frame_lit);
			saber_level_fog_batch(part);
			continue;
		}
		for (layer = 0; layer < part->layer_count; layer++)
		{
			unsigned char const *weights = part->layer_weights[layer];
			unsigned short const *triangles = part->layer_triangles[layer];
			boolean multiply = part->layer_multiply[layer];
			long corner;

			if (multiplying != (long)multiply)
			{
				multiplying = (long)multiply;
				saber_level_layer_shader_set(&shader, multiply);
			}
			if (!rasterizer_set_texture_bitmap_data(0, &saber_level.bitmaps[part->layer_textures[layer]]))
			{
				layers_untextured++;
				continue;
			}
			layers_drawn++;
			triangles_drawn += part->layer_triangle_counts[layer];
			IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
			/* (a part its first layer covers whole, its base not drawn: that
			layer writes the depth, and the rest are drawn at it, all of the
			same vertices by the same shader; kept on the card) */
			if (covered)
			{
				boolean writes = layer == part->layer_cover;

				if (writes != depth_written)
				{
					depth_written = writes;
					IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZWRITEENABLE, writes);
				}
				if (saber_level_layer_static_draw(part, layer, frame_lit))
					continue;
			}
			IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLELIST);
			for (corner = 0; corner < 3 * part->layer_triangle_counts[layer]; corner++)
			{
				unsigned short vertex = triangles[corner];
				unsigned char const *lit = frame_lit[vertex];

				/* (a multiplying layer unlit: it scales the lit colour under it) */
				if (multiply)
				{
					IDirect3DDevice8_SetVertexData4ub(global_d3d_device, 9, 255, 255, 255,
						(unsigned char)(255 - weights[vertex]));
				}
				else
				{
					IDirect3DDevice8_SetVertexData4ub(global_d3d_device, 9, lit[0], lit[1], lit[2],
						(unsigned char)(255 - weights[vertex]));
				}
				IDirect3DDevice8_SetVertexData2f(global_d3d_device, 4, part->layer_texcoords[layer][vertex][0],
					part->layer_texcoords[layer][vertex][1]);
				if (covered)
				{
					IDirect3DDevice8_SetVertexData4f(global_d3d_device, 0, part->layer_positions[vertex][0],
						part->layer_positions[vertex][1], part->layer_positions[vertex][2], 1.f);
					continue;
				}
				/* (drawn a two-hundredth of the way toward the camera: the base
				is drawn of compressed vertices by the model shader, the layer of
				these by another, and the depth they come to differs by more than
				the bias. Each layer failed the test against its own base and none
				was seen: the ground's triangles stood out one texture each) */
				IDirect3DDevice8_SetVertexData4f(global_d3d_device, 0,
					part->layer_positions[vertex][0] + (render.camera.position.x - part->layer_positions[vertex][0]) * 0.005f,
					part->layer_positions[vertex][1] + (render.camera.position.y - part->layer_positions[vertex][1]) * 0.005f,
					part->layer_positions[vertex][2] + (render.camera.position.z - part->layer_positions[vertex][2]) * 0.005f,
					1.f);
			}
			IDirect3DDevice8_End(global_d3d_device);
		}
		/* (and the sky's fog over it: its base, drawn by the model shader, is
		fogged, but its layers cover it whole and are not. The ground stood
		dark and clear in the distance among the map's trees and rocks, pale
		in the fog) */
		if (covered)
		{
			if (depth_written)
			{
				depth_written = FALSE;
				IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZWRITEENABLE, FALSE);
			}
			saber_level_layers_fog(part, &shader);
			multiplying = NONE;
		}
	}
	/* (the batches: each place's layers, the first's writing the depth, then
	their fog) */
	if (batched)
	{
		unsigned long const used = FLAG(0) | FLAG(4) | FLAG(9);
		long slot;
		long batch_index;

		for (slot = 0; slot < SABER_MAXIMUM_LAYERS; slot++)
		{
			for (batch_index = 0; batch_index < saber_level_batches.batch_count; batch_index++)
			{
				struct saber_level_batch *batch = &saber_level_batches.batches[batch_index];
				long range;

				if (batch->slot != slot || !batch->range_count)
					continue;
				if (multiplying != (long)batch->multiply)
				{
					multiplying = (long)batch->multiply;
					saber_level_layer_shader_set(&shader, batch->multiply);
				}
				if (!rasterizer_set_texture_bitmap_data(0, &saber_level.bitmaps[batch->texture]))
				{
					layers_untextured += batch->range_count;
					batch->range_count = 0;
					continue;
				}
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
				if ((slot == 0) != depth_written)
				{
					depth_written = slot == 0;
					IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZWRITEENABLE, depth_written);
				}
				d3d8_gl_static_vertices_draw_ranges(batch->vertices, used, batch->firsts, batch->counts,
					(unsigned long)batch->range_count);
				layers_drawn += batch->range_count;
				for (range = 0; range < batch->range_count; range++)
					triangles_drawn += (long)(batch->counts[range] / 3);
				batch->range_count = 0;
			}
		}
		if (depth_written)
		{
			depth_written = FALSE;
			IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZWRITEENABLE, FALSE);
		}
		if (saber_level_batches.fog.range_count)
		{
			csmemset(&shader, 0, sizeof(shader));
			shader.texture_modes = 0;
			shader.combiner_count = 1;
			/* (the vertex's colour, the fog's, and its alpha, the fog's density) */
			shader.rgb_inputs[0] = 0x04200000;
			shader.alpha_inputs[0] = 0x14200000;
			shader.rgb_outputs[0] = 0xc00;
			shader.alpha_outputs[0] = 0xc00;
			shader.final_combiner_inputs_abcd = 0xc;
			shader.final_combiner_inputs_efg = 0x1c00;
			IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_COLORWRITEENABLE,
				D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
			rasterizer_set_framebuffer_blend_function(0);
			rasterizer_set_pixel_shader(&shader);
			d3d8_gl_static_vertices_draw_ranges(saber_level_batches.fog.vertices, FLAG(0) | FLAG(9),
				saber_level_batches.fog.firsts, saber_level_batches.fog.counts,
				(unsigned long)saber_level_batches.fog.range_count);
			saber_level_batches.fog.range_count = 0;
			multiplying = NONE;
		}
	}
	if (!logged && parts_layered)
	{
		logged = TRUE;
		platform_log("anniversary: the structure's layers: %ld parts with layers (%ld unlit), %ld layers drawn of %ld "
			"triangles, %ld whose texture could not be set", parts_layered, parts_unlit, layers_drawn, triangles_drawn,
			layers_untextured);
	}
	if (begun)
	{
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZWRITEENABLE, TRUE);
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHABLENDENABLE, FALSE);
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZBIAS, 0);
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALL);
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_CCW);
	}
}

void saber_models_level_render(
	void)
{
	static real_rgb_color const colors[4] = { { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f } };
	static real const values[8] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	struct rasterizer_model_begin_parameters parameters;
	real_matrix4x3 node;
	short bsp_index = global_structure_bsp_index_get();
	long index;

	if (!saber_level.drawn || saber_toggle_classic_level())
		return;
	csmemset(&node, 0, sizeof(node));
	node.scale = 1.f;
	node.forward.i = 1.f;
	node.left.j = 1.f;
	node.up.k = 1.f;
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.unique_identifier = NONE;
	parameters.skinning.node_matrices = &node;
	parameters.skinning.node_matrix_count = 1;
	parameters.animation.colors = colors;
	parameters.animation.values = values;
	parameters.base_map_scale.i = 1.f;
	parameters.base_map_scale.j = 1.f;
	if (saber_level.ground_lit_bsp != bsp_index)
		saber_level_ground_light(bsp_index);
	for (index = 0; index < saber_level.part_count; index++)
	{
		struct saber_level_part *part = &saber_level.parts[index];
		struct shader_model_definition *shader = &part->shader;

		/* (the sky is drawn in the map sky's place) */
		if (part->sky || !render_frustum_cube_visible(&render.frustum, &part->bounds, FALSE))
			continue;
		/* (its light, of the structure about its middle, sampled as it is first
		drawn in a structure) */
		if (part->lit_bsp != bsp_index)
		{
			real best = -1.f;

			/* (ground, a terrain's part, in the ground's light: each of a
			chunk's layers is a part of its own, and each lit by its own place
			drew neighbouring triangles light and dark, in patches with their
			edges) */
			if (part->terrain && saber_level.ground_lit)
			{
				part->lighting = saber_level.ground_lighting;
				best = 0.f;
			}
			else
			{
				best = saber_level_part_sample_light(part, &part->lighting);
			}
			if (best < 0.f)
			{
				csmemset(&part->lighting, 0, sizeof(part->lighting));
				part->lighting.ambient_color.red = 0.5f;
				part->lighting.ambient_color.green = 0.5f;
				part->lighting.ambient_color.blue = 0.5f;
				part->lighting.distant_light_count = 1;
				part->lighting.distant_lights[0].color.red = 0.6f;
				part->lighting.distant_lights[0].color.green = 0.6f;
				part->lighting.distant_lights[0].color.blue = 0.55f;
				part->lighting.distant_lights[0].direction.k = -1.f;
				part->lighting.shadow_vector.k = -1.f;
			}
			part->lighting.point_light_count = 0;
			part->lit_bsp = bsp_index;
		}
		/* (not a base its own material laid whole over it covers: never seen,
		that layer drawn in its place, and the depth its layers are drawn
		against: saber_level_layers_render) */
		if (part->layer_count && part->layer_cover == 0)
			continue;
		parameters.lighting = part->lighting;
		/* (and the frame's point lights that reach it, the flashlight's) */
		saber_level_point_lights(part, &parameters.lighting);
		parameters.centroid = part->centroid;
		parameters.radius = part->radius;
		*shader = saber_level.shaders[part->cutout ? 1 : 0];
		shader->model.map_u_scale = part->uv_scale[0];
		shader->model.map_v_scale = part->uv_scale[1];
		rasterizer_model_begin(&parameters, FALSE);
		rasterizer_model_draw((struct shader *)shader, part->texture,
			&part->triangle_buffer, NONE, part->triangle_buffer.count, &part->vertex_buffer, NONE);
		rasterizer_model_end();
	}
	saber_level_layers_render();
}

int saber_bitmaps_contain(
	struct bitmap_data const *bitmap)
{
	struct saber_bitmap const *played;

	if (saber_level.bitmaps && bitmap >= saber_level.bitmaps && bitmap < saber_level.bitmaps + saber_level.texture_count)
		return TRUE;
	for (played = saber_models_globals.bitmaps; played; played = played->next)
	{
		/* (only while the group holds the package's textures: the map's own
		array can lie where the package's did) */
		if (bitmap_group_get(played->tag_index)->bitmaps.address == played->bitmaps &&
			bitmap >= played->bitmaps + played->classic_count && bitmap < played->bitmaps + played->count)
			return TRUE;
	}

	return FALSE;
}

struct bitmap_data const *saber_bitmaps_original(
	struct bitmap_data const *bitmap)
{
	struct saber_bitmap const *played;

	for (played = saber_models_globals.bitmaps; played; played = played->next)
	{
		if (bitmap_group_get(played->tag_index)->bitmaps.address == played->bitmaps &&
			bitmap >= played->bitmaps && bitmap < played->bitmaps + played->classic_count &&
			bitmap - played->bitmaps < played->map_group.bitmaps.count)
		{
			return TAG_BLOCK_GET_ELEMENT(&played->map_group.bitmaps, bitmap - played->bitmaps, struct bitmap_data);
		}
	}

	return NULL;
}

int saber_bitmaps_read(
	struct bitmap_data *bitmap,
	void *buffer)
{
	struct saber_bitmap const *played;

	if (saber_level.bitmaps && bitmap >= saber_level.bitmaps && bitmap < saber_level.bitmaps + saber_level.texture_count)
	{
		struct saber_texture const *texture = &saber_level.textures[bitmap - saber_level.bitmaps];

		if (!texture->pixels || texture->pixels_size < bitmap->pixels_size)
			return FALSE;
		memcpy(buffer, texture->pixels, (size_t)bitmap->pixels_size);
		halo_custom_edition_texels_channels(buffer, _custom_edition_channels_xbox);
		rasterizer_xbox_bitmap_rebuild_hardware_format(bitmap);
		return TRUE;
	}
	for (played = saber_models_globals.bitmaps; played; played = played->next)
	{
		/* (only while the group holds the package's textures: the map's own
		array can lie where the package's did, and then the map's own bitmap
		would be given the package's, and not its own) */
		if (bitmap_group_get(played->tag_index)->bitmaps.address == played->bitmaps &&
			bitmap >= played->bitmaps + played->classic_count && bitmap < played->bitmaps + played->count)
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
