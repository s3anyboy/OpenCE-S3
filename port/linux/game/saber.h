/*
SABER.H

Halo: Combat Evolved Anniversary's remastered models drawn over a map's
(saber_models.c): its Saber3D packages (saber_paks.c, saber_lzx.c), their
templates (models) and textures.
*/

#ifndef __SABER_H
#define __SABER_H
#pragma once

/* ---------- headers */

#include <stddef.h>

/* ---------- constants */

/* the types of a package's entries the models need (the package's own
numbers) */
enum
{
	_saber_entry_textures = 6,
	_saber_entry_textures_mips64 = 7,
	_saber_entry_templates = 12,
	_saber_entry_level_geometry = 16,
};

/* the ranks a template's mesh can be of (an Elite's "metal_major", a
Grunt's "back_minor"): none (every rank's), or one of them */
enum
{
	_saber_rank_all = 0,
	_saber_rank_minor,
	_saber_rank_major,
	_saber_rank_spec_ops,
	_saber_rank_stealth,
	_saber_rank_commander,
	NUMBER_OF_SABER_RANKS
};

/* (saber_lzx_decompress's errors) */
#define SABER_LZX_ERROR_ARGUMENTS (-1)
#define SABER_LZX_ERROR_DATA (-2)
#define SABER_LZX_ERROR_SPACE (-3)

/* ---------- structures */

struct saber_lzx_state;
struct saber_pak;
struct model_vertex_uncompressed;

/* a template's mesh, made Halo's: its vertices in Halo's axes (not yet
scaled to Halo's units; their two nodes the template's bones' places, of
the most weight) and its triangles (three indices each) as Halo
draws them, a part for each of its submeshes with the material it is drawn
with (its index in the template's materials) */
struct saber_mesh_part
{
	long vertex_count;
	struct model_vertex_uncompressed *vertices;
	long triangle_count;
	unsigned short *triangles;
	short material_index;
	/* its level of detail (0 the most detailed) */
	short lod;
	/* the rank it is drawn for (_saber_rank_all: every one) */
	short rank;
	/* the head permutation it is (a marine's "head_charles_helmet_10"),
	empty when it is drawn with every permutation (the body) */
	char variant[64];
	/* whether it is a skin compound's, and then where its first vertex's
	bone stands (in Halo's axes, the template's units) */
	short compound;
	real anchor[3];
	/* how many times its texture repeats, and from where (a level mesh's
	__export_uv_compr_params). 1 and 0 until then. The repeat is not folded
	into the coordinates: the renderer keeps a coordinate in -1..1 */
	real uv_scale[2];
	real uv_offset[2];
	/* a level's sky object (its skydome, its cloud layers): drawn with
	the sky, whichever material it uses */
	short sky;
};

struct saber_template
{
	long material_count;
	char (*materials)[64];
	/* the bones its vertices move with, by name (as Halo's nodes are
	compared: lower case, spaces for underscores), which its vertices'
	nodes are the places of */
	long bone_count;
	char (*bones)[64];
	/* each bone's parent's place (NONE: none), and its bind: what takes a
	vertex (in Halo's axes, the template's units, a row vector) to the
	bone's own space, 4x4 row-major */
	long *bone_parents;
	real (*bone_binds)[16];
	/* and where it stands, in Halo's axes and the template's units */
	real (*bone_origins)[3];
	long part_count;
	struct saber_mesh_part *parts;
};

/* a texture of a package, as Halo's bitmaps keep it: its pixels (each
mipmap after the one before), its size, its mipmaps after the first, and
its format (bitmaps.c's: DXT1, DXT3 or DXT5) */
struct saber_texture
{
	short width;
	short height;
	short mipmap_count;
	short format;
	unsigned char *pixels;
	long pixels_size;
};

/* ---------- prototypes/SABER_LZX.C */

size_t saber_lzx_state_size(void);
/* (window: 1 << window_bits bytes, the caller's; window_bits 15 to 21) */
int saber_lzx_initialize(struct saber_lzx_state *state, int window_bits, unsigned char *window);
void saber_lzx_reset(struct saber_lzx_state *state);
/* one XMemCompress stream: the bytes written, or a SABER_LZX_ERROR_ */
long saber_lzx_decompress(struct saber_lzx_state *state, unsigned char const *in, size_t in_size,
	unsigned char *out, size_t out_size);

/* ---------- prototypes/SABER_PAKS.C */

/* a package (a .s3dpak): NULL, with *reason saying why, when it is not
one */
struct saber_pak *saber_pak_open(char const *path, char const **reason);
void saber_pak_close(struct saber_pak *pak);
/* whether it is the Xbox 360's (compressed; its templates' texture
coordinates big-endian), not the PC's (Halo: The Master Chief Collection's) */
int saber_pak_xbox360(struct saber_pak const *pak);
/* its entries: their count, and each one's name and type */
long saber_pak_entry_count(struct saber_pak const *pak);
char const *saber_pak_entry_name(struct saber_pak const *pak, long entry);
long saber_pak_entry_type(struct saber_pak const *pak, long entry);
/* the entry of the name and type, or -1 */
long saber_pak_find(struct saber_pak const *pak, char const *name, long type);
/* the whole entry, in memory of malloc's (free() it), or NULL */
unsigned char *saber_pak_read(struct saber_pak *pak, long entry, unsigned long *size);

/* the template of the package's templates entry, or FALSE with *reason
saying why not (saber_template_dispose lets go of what it made either way) */
int saber_template_read(struct saber_template *template_, unsigned char const *bytes, unsigned long size,
	int xbox360, char const **reason);
/* the level's geometry of the package's level geometry entry (its ".lg"),
read as a template of it: its every mesh, in place, including meshes drawn
of a sharingObj's buffers (0x2901) with that mesh's 0x3501 placement */
int saber_level_geometry_read(struct saber_template *template_, unsigned char const *bytes, unsigned long size,
	int xbox360, char const **reason);
void saber_template_dispose(struct saber_template *template_);

/* the texture of a package's textures entry (its TexturesMips64 or
Textures), DXT only, or FALSE (saber_texture_dispose lets go of it) */
int saber_texture_read(struct saber_texture *texture, unsigned char const *bytes, unsigned long size);
void saber_texture_dispose(struct saber_texture *texture);
/* whether a texture's alpha cuts it out (leaves, grass: many texels clear
and many solid), which is drawn alpha tested; not a texture whose alpha is
another mask, all low or all high; `foliage`, a texture named as leaves
or branches are, whose soft edges leave less of it solid) */
int saber_texture_cutout(struct saber_texture const *texture, int foliage);
/* whether a texture's alpha is a shade across the picture rather than
holes in it: at least as many texels sit between clear and solid as are
solid. Drawn tested, the shaded faces are discarded */
int saber_texture_shaded_alpha(struct saber_texture const *texture);
/* the colour-change mask of a package's texture entry (a "_mpmask", one
channel, or a "_br", whose first channel differs from its background where
its armour is: `banded`), made a texture of Halo's multipurpose map: DXT1,
the mask in its blue (which this build's model shaders take colour change
from), nothing in its other channels; FALSE if it is no mask read. A
banded mask leaves out where the texture it masks, `base` (or NULL), is
coloured already. */
int saber_texture_read_mask(struct saber_texture *texture, unsigned char const *bytes, unsigned long size,
	int banded, struct saber_texture const *base);
/* a texture of no colour change, 4 by 4 (a multipurpose map's for a
material without a mask) */
int saber_texture_blank(struct saber_texture *texture);
/* a texture's alpha made solid throughout (DXT3's and DXT5's), for one not
cut out that is drawn by a shader alpha tested for another's sake */
void saber_texture_make_solid(struct saber_texture *texture);
/* a sky texture's empty texels made clear. A star field (`stars`) is black
where the sky behind it shows, and that black has no alpha: those texels,
and the grey a block blends around each star, are punched out. A planet's
or a moon's card keeps its picture: the clear texels stay clear and the
limb is lifted so it fades instead of being cut off */
void saber_texture_sky_alpha(struct saber_texture *texture, int stars);
/* the halo ring's picture is stored a few shades off a flat blue, so the
band draws as one colour. The shades are spread so the surface shows */
void saber_texture_ring_contrast(struct saber_texture *texture);
/* multiply a texture's colour by numer/denom. The pelican's plating is
near white, so the bay's ceiling and floor clip; brought down toward the
hull's grey */
void saber_texture_darken(struct saber_texture *texture, long numer, long denom);
/* the marine eyepiece is a dark mask with a few light marks. Drawn as it
is, the lens is black. The marks, and the field under them, are made green */
void saber_texture_monicle(struct saber_texture *texture);
/* a picture of a bolt on black, with no alpha of its own (the plasma
rifle's arc). The black, and the grey a block blends around it, is punched
out so the plate does not draw */
void saber_texture_punch_black(struct saber_texture *texture);
/* the rear meters are a white plate (wpn_plasma_rifle_holo_01). Luminance
becomes blue, and the black of the plate stays black */
void saber_texture_holo(struct saber_texture *texture);
/* a window's alpha is how much of it covers what is behind it. A Pelican's
cockpit glass is plotted dark and opaque; its alpha keeps a fifth of itself,
so the bay shows through it as the map's own glass does */
void saber_texture_glass(struct saber_texture *texture);

/* ---------- prototypes/SABER_MODELS.C */

struct bitmap_data;

/* the map's tags loaded (mods.c, mods_tags_loaded): the models of its
package drawn over its own, if display.anniversary_models is on */
void saber_models_tags_loaded(char const *map_name);
/* before the tags go: the map's tags get their own definitions back
(unless give_back is FALSE: the tags are gone already), and what was made
for them is let go of */
void saber_models_tags_unloaded(int give_back);
/* (mod_bitmaps_read, mod_bitmaps_contain) whether a bitmap is one made of a
package's texture, its pixels put in buffer */
int saber_bitmaps_read(struct bitmap_data *bitmap, void *buffer);
int saber_bitmaps_contain(struct bitmap_data const *bitmap);
/* (render.c) whether the level's structure is drawn as the package's level
geometry (display.remastered_bsp), and its drawing, in place of the
structure's lightmapped surfaces. The sky is the package's too, drawn in
place of the map's sky model (the map's lens flares stay) */
int saber_models_level_drawn(void);
void saber_models_level_render(void);
int saber_models_level_sky_drawn(void);
void saber_models_level_sky_render(void);
struct model;
/* (models.c, render_model_parts) the geometry of a model drawn as a
template that the object `object_index` is drawn with: its rank's (by its
colour), of the geometry `geometry_index` Halo chose */
short saber_models_geometry_index(struct model const *model, long object_index, short geometry_index);
/* (models.c) whether the shader's base map at `permutation` is a package
texture cut out by its alpha, drawn alpha tested and from both sides */
int saber_models_part_cutout(long shader_tag, short permutation);
/* (models.c) whether that permutation is glass, drawn blended on its alpha */
int saber_models_part_blend(long shader_tag, short permutation);

#endif
