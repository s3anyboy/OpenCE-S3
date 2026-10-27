/*
SHADOWRUN_TECH.H

The Shadowrun prototype's tech, as far as it is ported (docs/shadowrun_caches.md,
"Tech"): vision, which marks the enemy units near the caster on the HUD for a
time.

The prototype's own vision is a tag-driven blur (t1ed's second block), whose
fields missionlet leaves empty; this marks the units instead, as the port's
navpoints do (FUN_0027d9c0's dispatch is what the cast follows).

The grant is the prototype's player_change_tech (the scripts' function, and the
console's shadowrun_tech).
*/

#ifndef __SHADOWRUN_TECH_H
#define __SHADOWRUN_TECH_H
#pragma once

/* ---------- headers */

#include "cseries.h"
#include "shadowrun_definitions.h"

/* ---------- constants */

enum
{
	/* the techs, as FUN_0027d4e0 numbers them (globals.hsc's tech_vision and
the rest) */
	_shadowrun_tech_vision_id = 1,
	_shadowrun_tech_smartlink_id = 2,
	_shadowrun_tech_gecko_id = 4,
	_shadowrun_tech_glider_id = 5,
	_shadowrun_tech_grapple_id = 6,
	_shadowrun_tech_zoom_id = 11,
	_shadowrun_tech_hologram_id = 12,
	_shadowrun_tech_hearing_id = 13,
	_shadowrun_tech_heal_id = 14,
	NUMBER_OF_SHADOWRUN_TECHS,

	/* vision: how far it reaches (world units: a metre is ten), and how long
	it marks them (a second is 30 ticks) */
	SHADOWRUN_VISION_RANGE = 600,
	SHADOWRUN_VISION_TICKS = 30 * 30,

	/* gecko: how long the wearer climbs anything (30 s), and the most biped
definitions it keeps the flag on at once */
	SHADOWRUN_GECKO_TICKS = 30 * 30,
	MAXIMUM_SHADOWRUN_GECKO_DEFINITIONS = 16,

	/* hologram: how far before the caster its aim starts, how far it reaches
	(world units: a metre is ten: 20 m), and how long it stays */
	SHADOWRUN_HOLOGRAM_ORIGIN_OFFSET = 1,
	SHADOWRUN_HOLOGRAM_RANGE = 200,
	SHADOWRUN_HOLOGRAM_TICKS = 30 * 30,

	/* the holograms standing at once (all the units') */
	MAXIMUM_SHADOWRUN_HOLOGRAMS = 8,
};

/* ---------- prototypes/SHADOWRUN_TECH.C */

/* a unit's tech (the slot FUN_0027d9c0 dispatches from), 0 for none */
void shadowrun_unit_set_tech(
	long unit_index,
	short tech);
short shadowrun_unit_tech(
	long unit_index);
/* port: the map's actor variant the Hologram tech makes (shadowrun_magic.c),
and a hologram of it placed at `position` on the unit's side, as a summon's
actor is. NONE if the map has none, or none could be placed */
long shadowrun_hologram_variant(
	void);
long shadowrun_hologram_place(
	long unit_index,
	real_point3d const *position);

/* the techs' place on the HUD (shadowrun_tech.c): what vision marks */
void shadowrun_tech_initialize(
	void);
void shadowrun_tech_initialize_for_new_map(
	void);
void shadowrun_tech_update(
	void);
/* the unit's tech used (the left bumper): the vision marks the enemy units
near it. TRUE when a tech was cast */
boolean shadowrun_tech_cast(
	long unit_index);
/* the unit died for good, or was deleted: its marks go */
void shadowrun_unit_tech_died(
	long unit_index);
void shadowrun_unit_tech_delete(
	long unit_index);

/* the scripts' player_change_tech, and the console's shadowrun_tech: the
players know the tech (a player's is kept for its units). FALSE if they are
not on a Shadowrun map */
boolean shadowrun_players_change_tech(
	short tech);
/* the tech one unit knows, as the console's shadowrun_tech <id> gives it to
the first local player */
boolean shadowrun_local_player_change_tech(
	short tech);

#endif
