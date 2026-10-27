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
the rest). Reflexes is 0: the unit's own is _shadowrun_tech_none until
it has one */
	_shadowrun_tech_reflexes_id = 0,
	_shadowrun_tech_antimagic_id = 7,
	_shadowrun_tech_vision_id = 1,
	_shadowrun_tech_smartlink_id = 2,
	_shadowrun_tech_gecko_id = 4,
	_shadowrun_tech_glider_id = 5,
	_shadowrun_tech_grapple_id = 6,
	_shadowrun_tech_zoom_id = 11,
	_shadowrun_tech_hologram_id = 12,
	_shadowrun_tech_hearing_id = 13,
	_shadowrun_tech_heal_id = 14,
	/* (MAXIMUM_SHADOWRUN_TECHS, of shadowrun_units.h, is their count) */

	/* no tech (reflexes is 0, so 0 cannot be that) */
	_shadowrun_tech_none = -1,

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
	/* and the wait before the next may be made (ticks) */
	SHADOWRUN_HOLOGRAM_COOLDOWN_TICKS = 30 * 10,

	/* the holograms standing at once (all the units') */
	MAXIMUM_SHADOWRUN_HOLOGRAMS = 8,

	/* grapple: how far before the caster its aim starts, how far it reaches
	(world units: a metre is ten: 30 m), and how long the pull lasts (ticks) */
	SHADOWRUN_GRAPPLE_ORIGIN_OFFSET = 1,
	SHADOWRUN_GRAPPLE_RANGE = 300,
	SHADOWRUN_GRAPPLE_PULL_TICKS = 15,
	/* and the wait before the next (ticks: 5 s) */
	SHADOWRUN_GRAPPLE_COOLDOWN_TICKS = 5 * 30,
};

/* port: the map's grapple projectile (its name without the group's extension) */
#define SHADOWRUN_GRAPPLE_PROJECTILE "shadowrunstuff\\grapple\\grapple"

/* port: reflexes: how long it lasts (10 s), and what it does meanwhile: the
unit moves and reloads faster, and jumps a little higher */
#define SHADOWRUN_REFLEXES_TICKS (30 * 10)
#define SHADOWRUN_REFLEXES_MOVEMENT_SCALE 1.6f
#define SHADOWRUN_REFLEXES_RELOAD_SCALE 0.6f
#define SHADOWRUN_REFLEXES_JUMP_SCALE 1.3f

/* port: the map's anti-magic tech tag (t3ed), as its tags name it */
#define SHADOWRUN_ANTIMAGIC_TECH_NAME "shadowrunstuff\\tech3"

/* port: anti-magic: the zones standing at once, and the most zones one
player's grenades keep (units.c, FUN_0026FCA0: a global list) */
#define MAXIMUM_SHADOWRUN_ANTIMAGIC_ZONES 8
/* port: the units a zone keeps, to know which left it (and so lost it) */
#define MAXIMUM_SHADOWRUN_ANTIMAGIC_INSIDE 32

/* port: the glider: the scenery it wears on its back, how high its jump goes
(1 for neither), how long it glides (ticks) and how its fall slows while it
does (a step of its velocity a tick toward this) */
#define SHADOWRUN_GLIDER_SCENERY "shadowrunstuff\\glider\\glider"
/* (the scenery group's: this file's headers declare no SCENERY_DEFINITION_TAG) */
#define SHADOWRUN_SCENERY_TAG 'scen'
#define SHADOWRUN_GLIDER_JUMP_SCALE 1.6f
#define SHADOWRUN_GLIDER_TICKS (30 * 3)
#define SHADOWRUN_GLIDER_FALL_SPEED 1.f
/* how long it is worn (ticks: 15 min, until another tech takes its place) */
#define SHADOWRUN_GLIDER_WEAR_TICKS (30 * 60 * 15)

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
/* port: anti-magic, which the anti-magic grenade makes where it lands
(units.c, FUN_0026EA00, FUN_0026FCA0): a shrinking zone; whether the unit
is inside one (its spells fail there, and it is hurt by the zone) */
void shadowrun_antimagic_zone(
	long owner_object_index,
	real_point3d const *position);
boolean shadowrun_antimagic_at(
	long unit_index);
/* whether the unit is an enemy of the zone's (the zones are the thrower's)
and inside it: its spells fail (shadowrun_magic.c) */
boolean shadowrun_spells_blocked(
	long unit_index);
void shadowrun_antimagic_update(
	void);
void shadowrun_antimagic_initialize_for_new_map(
	void);

/* whether the actor is a hologram's: its attacks do no damage (ai.c) */
boolean shadowrun_hologram_active(
	long actor_index);

/* port: the map's grapple projectile (shadowrunstuff\grapple\grapple), or
NONE; grapple fires it (shadowrun_tech.c), and where it lands the caster is
pulled to it (shadowrun_magic.c) */
long shadowrun_grapple_projectile(
	void);
long shadowrun_grapple_fire(
	long unit_index,
	long projectile_definition_index);
/* whether the projectile is a grapple's */
boolean shadowrun_grapple_projectile_is(
	long projectile_index);
/* port: whether the unit wears the glider (shadowrun_tech.c), how much its
jump is scaled (bipeds.c), and its glide, each tick of a unit */
boolean shadowrun_glider_worn(
	long unit_index);
real shadowrun_glider_jump_scale(
	long unit_index);
void shadowrun_glider_update(
	long unit_index);

/* port: whether the unit's reflexes run (shadowrun_tech.c): its jump is
scaled (bipeds.c), its movement (shadowrun_races.c) and its reload
(weapons.c) */
boolean shadowrun_reflexes_active(
	long unit_index);
real shadowrun_reflexes_jump_scale(
	long unit_index);
real shadowrun_reflexes_movement_scale(
	long unit_index);
real shadowrun_reflexes_reload_scale(
	long unit_index);

/* its caster, pulled to `position` over the pull's ticks, which this moves
it through */
void shadowrun_grapple_pull(
	long projectile_index,
	real_point3d const *position);
void shadowrun_grapple_update(
	long unit_index);

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
/* port: the techs a player has bought, and the one it has taken up:
player_change_tech, the buy menu and the d-pad's left (shadowrun_tech.c) */
struct shadowrun_player;
boolean shadowrun_player_has_tech(
	struct shadowrun_player *shadowrun_player,
	short tech);
boolean shadowrun_player_select_tech(
	struct shadowrun_player *shadowrun_player,
	long unit_index,
	short tech);
short shadowrun_player_next_tech(
	struct shadowrun_player *shadowrun_player);
boolean shadowrun_player_buy_tech(
	struct shadowrun_player *shadowrun_player,
	long unit_index,
	short tech);
boolean shadowrun_player_remove_tech(
	struct shadowrun_player *shadowrun_player,
	long unit_index,
	short tech);
/* the local players, each to the next of its techs (player_control.c) */
void shadowrun_players_switch_tech(
	void);

/* the tech one unit knows, as the console's shadowrun_tech <id> gives it to
the first local player */
boolean shadowrun_local_player_change_tech(
	short tech);

#endif
