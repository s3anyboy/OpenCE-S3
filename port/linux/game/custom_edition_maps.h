/*
CUSTOM_EDITION_MAPS.H

Custom Edition maps in the menus. With game.custom_edition on, the CE maps
of the custom_maps folder are listed: the multiplayer maps after the Xbox
levels in the multiplayer level list (ui_widget_event_handler_functions.c),
and MULTIPLAYER's in the map lists (port/linux/game/menu_functions.c), beside
the Shadowrun prototype's maps, which have a kind of their own. A CE map's
level name is custom_maps\<name> (custom_edition_cache.h).

The menus that show a level (ui_widget_game_data_input_functions.c) refer
to it by an index into their own tags' strings and frames. Maps that the
tags don't have get "display indices" past those, and text_group.c and
ui_widget.c ask this file for their names, descriptions and pictures.

	below 0x1000   Xbox multiplayer level (the tags' own index)
	0x3000 + n     stock campaign level n (co-op)
	0x4000 + n     CE multiplayer map
	0x6000 + n     CE campaign map (played alone or as co-op)
	0x6400 + n     Shadowrun prototype map (the map lists' SHADOWRUN)
*/

#ifndef __CUSTOM_EDITION_MAPS_H
#define __CUSTOM_EDITION_MAPS_H

/* ---------- constants */

/* the most CE multiplayer maps the level list holds, which every map list
(the menus', the Map screen's) has room for: as many as their display
indices fit before the campaigns' (custom_edition_maps.c) */
#define CUSTOM_EDITION_MAPS_MAXIMUM 8192

/* the Shadowrun prototype's maps, of the maps folder, listed with the CE
campaign maps: a level name of one is shadowrun\<name>, which the Xbox
caches' loader reads from maps\<name>.map (docs/shadowrun_caches.md) */
#define SHADOWRUN_LEVEL_NAME_PREFIX "shadowrun\\"

/* ---------- structures */

struct bitmap_data;

/* ---------- prototypes/CUSTOM_EDITION_MAPS.C */

/* the Shadowrun prototype's maps have display indices just past the Custom
Edition campaign maps', below ui_widget.c's spinner descriptions' (0x7000),
so that a list can have them only (the map lists' SHADOWRUN kind): 0x6400 + n
*/
#define FIRST_SHADOWRUN_DISPLAY_INDEX 0x6400

/* whether a level name is a Shadowrun prototype map's (shadowrun\<name>) */
boolean shadowrun_level_name(
	char const *level_name);

/* Returns the level list: the Xbox levels, then the CE multiplayer maps
sorted by name. *level_count gets the total. Scans for CE maps the first
time, and after custom_edition_maps_look_again. */
char **custom_edition_maps_level_list(
	char **xbox_levels,
	short xbox_level_count,
	short *level_count);

/* Has the next level list scan the maps folders again (a map list opening,
which shows maps added since). */
void custom_edition_maps_look_again(
	void);

/* the display index of an entry in the latest level list */
short custom_edition_maps_level_display_index(
	short level_index);

/* The display index for a level name (a network game's map name): a stock
campaign level, a CE map (custom_maps\<name>) this machine has, or NONE.
Scans for maps the first time. */
short custom_edition_maps_display_index(
	char const *level_name);

/* whether the display index is a campaign level (stock or CE), i.e. co-op */
boolean custom_edition_maps_campaign(
	short display_index);

/* the stock campaign level (0 is the first) for a display index, or NONE */
short custom_edition_maps_campaign_level(
	short display_index);

/* whether a level name is a campaign level's, stock or CE: the game played
on it is a campaign, or network co-op */
boolean custom_edition_maps_level_campaign(
	char const *level_name);

/* The CE campaign maps (campaign TRUE) or multiplayer maps, sorted by name:
how many there are, and the display index of the one at `index` (NONE past
them). Scans for maps the first time. */
short custom_edition_maps_count(
	boolean campaign);
short custom_edition_maps_display_index_of(
	boolean campaign,
	short index);

/* The Shadowrun prototype's maps, sorted by name: how many there are, and the
display index of the one at `index` (NONE past them). Scans for maps the
first time. */
short custom_edition_shadowrun_count(
	void);
short custom_edition_shadowrun_display_index_of(
	short index);

/* a Shadowrun map's level name to load, or its name for the menus, for a
display index, or NULL */
char const *custom_edition_shadowrun_level_name(
	short display_index);
wchar_t *custom_edition_shadowrun_name(
	short display_index);

/* the level name to load for a display index, or NULL */
char const *custom_edition_maps_level_name(
	short display_index);

/* whether the display index is one of Halo PC's own multiplayer maps (Ice
Fields, Death Island and so on), which are listed as VANILLA */
boolean custom_edition_maps_stock(
	short display_index);

/* name and description for a display index, or NULL (text_group.c) */
wchar_t *custom_edition_maps_name(
	short display_index);
wchar_t *custom_edition_maps_description(
	short display_index);

/* Called by ui_widget.c when a bitmap widget draws frame *frame_index of a
bitmap tag. If the tag is the level pictures and the frame is a display
index of ours, returns the picture to draw over the whole widget, and sets
*frame_index to the unknown level's frame (shown when NULL is returned).
For anything else it returns NULL and leaves the frame alone. */
struct bitmap_data *custom_edition_maps_picture(
	long bitmap_tag_index,
	short *frame_index);

#endif
