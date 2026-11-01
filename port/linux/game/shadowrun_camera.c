/*
SHADOWRUN_CAMERA.C

The Shadowrun prototype's third-person camera, as its executable
(cachesym.xbe) chooses it: a player holding a melee weapon (the katana,
razorclaws: a weapon flag of Shadowrun's) or one that must be readied is
seen from behind, as is a player in its smoke (unless it looks through a
scope), and the camera follows such a weapon's holder on its unit's third
camera track, closer than the first. Aiming is as in first person: only the
view changes.

(Its executable also follows a player in some of tech's states: tech is not
played.)
*/

/* ---------- headers */

#include "cseries.h"
#include "items/weapon_definitions.h"
#include "objects/objects.h"
#include "units/units.h"
#include "shadowrun_tech.h" /* port: port/linux/game/shadowrun_tech.c */
#include "shadowrun_cable.h" /* port: port/linux/game/shadowrun_cable.c */
#include "shadowrun_units.h"

/* ---------- constants */

enum
{
	/* (a weapon's flag of Shadowrun's, that it is a melee weapon, is
	shadowrun_definitions.h's _shadowrun_weapon_melee_bit) */

	/* the camera track a melee weapon's holder is followed on */
	SHADOWRUN_MELEE_CAMERA_TRACK = 2,
};

/* ---------- private prototypes */

/* ---------- public code */

/* 0x118C30's: whether a player's unit is seen from behind: one holding a
melee weapon, one in its smoke not looking through a scope, or one gliding
or being reeled in by a grapple (port) */
boolean shadowrun_unit_third_person(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean result = FALSE;

	if (shadowrun_unit)
	{
		struct unit_datum *unit = unit_get(unit_index);

		if (unit->object.parent_object_index == NONE)
		{
			/* 0x115B30: the player looks through a scope */
			boolean zoomed = unit->unit.current_zoom_level != NONE;

			/* port: and one gliding or reeled in by a cable
			(port/linux/game/shadowrun_tech.c, shadowrun_cable.c) */
			boolean gliding = shadowrun_glider_worn(unit_index);
			boolean reeling = shadowrun_cable_reeling(unit_index);

			result = shadowrun_unit_melee_weapon(unit_index) ||
				(shadowrun_unit->smoke_active && !zoomed) || gliding || reeling;
		}
	}

	return result;
}

/* 0x0BEF70's: the unit camera track a unit is followed on, of its count:
a melee weapon's holder's is its third, if it has one */
short shadowrun_unit_camera_track(
	long unit_index,
	short track_index,
	long track_count)
{
	if (unit_index != NONE && shadowrun_unit_get(unit_index) &&
		track_count > SHADOWRUN_MELEE_CAMERA_TRACK &&
		unit_get(unit_index)->object.parent_object_index == NONE &&
		shadowrun_unit_melee_weapon(unit_index))
	{
		track_index = SHADOWRUN_MELEE_CAMERA_TRACK;
	}

	return track_index;
}

/* ---------- private code */

/* (whether the weapon in the unit's hand is a melee weapon, or must be
readied, is shadowrun_unit_melee_weapon: shadowrun_units.c, shadowrun.h) */
