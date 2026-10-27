/*
SHADOWRUN_TECH.C

The Shadowrun prototype's tech, as far as it is ported (shadowrun_tech.h):
vision, which marks the enemy units near the caster on the HUD for 30 seconds,
and the scripts' player_change_tech, which gives a player a tech.

Ported from the prototype's executable (cachesym.xbe, build 01.12.07.0132),
whose FUN_0027d9c0 dispatches the techs and spells from the unit's power list.
The prototype's own vision is a tag's blur, which missionlet leaves empty
(t1ed's second block: docs/shadowrun_caches.md); this is its place instead,
marked with the port's navpoints.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "interface/hud_definitions.h"
#include "game/game_allegiance.h"
#include "game/players.h"
#include "interface/hud.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "scenario/scenario.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "units/units.h"
#include "shadowrun_units.h"
#include "shadowrun_tech.h"

/* ---------- constants */

/* the arrow the HUD marks vision's targets with: the red marker the game
marks its goals' enemies with ("target_red"), else the map's red default
("default_red", as missionlet names it), else the first the HUD globals have.
(A nav point is one of the HUD globals' waypoint arrows, found by name:
hud_nav_points.c reads it as its index) */
static char const *const shadowrun_vision_nav_names[] =
{
	"target_red",
	"default_red",
};

/* the game's (interface/hud.h) */
extern struct hud_globals_definition *hud_globals;
short find_nav_point(char const *name);

/* ---------- private prototypes */

static void shadowrun_gecko_end(
	long unit_index);
static void shadowrun_vision_clear(
	long unit_index);
static void shadowrun_vision_mark(
	long unit_index);

/* ---------- public code */

void shadowrun_tech_initialize(
	void)
{
	return;
}

void shadowrun_tech_initialize_for_new_map(
	void)
{
	return;
}

/* each tick: a vision's marks last its ticks, and are made afresh each tick
(a unit that leaves its range, or dies, is unmarked) */
void shadowrun_tech_update(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;

	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(iterator.index);

		if (!shadowrun_unit || shadowrun_unit->tech_ticks <= 0)
		{
			continue;
		}
		if (--shadowrun_unit->tech_ticks <= 0)
		{
			shadowrun_vision_clear(iterator.index);
			shadowrun_gecko_end(iterator.index);
		}
		else if (shadowrun_unit->tech == _shadowrun_tech_vision_id)
		{
			shadowrun_vision_mark(iterator.index);
		}
	}

	return;
}

void shadowrun_unit_set_tech(
	long unit_index,
	short tech)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit)
	{
		if (shadowrun_unit->tech != tech)
		{
			shadowrun_vision_clear(unit_index);
			shadowrun_unit->tech_ticks = 0;
		}
		shadowrun_unit->tech = tech;
	}

	return;
}

short shadowrun_unit_tech(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	return shadowrun_unit ? (short)shadowrun_unit->tech : 0;
}

/* the left bumper's: the unit's tech does what it does. TRUE when one ran */
boolean shadowrun_tech_cast(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (!shadowrun_unit || shadowrun_unit->tech == 0)
	{
		return FALSE;
	}
	switch (shadowrun_unit->tech)
	{
	case _shadowrun_tech_vision_id:
		shadowrun_vision_clear(unit_index);
		shadowrun_unit->tech_ticks = SHADOWRUN_VISION_TICKS;
		shadowrun_vision_mark(unit_index);
		return TRUE;

	case _shadowrun_tech_gecko_id:
		/* port: gecko, which the executable's tech grant makes the biped able to
		climb anything (units.c, FUN_0027d4e0: the unit's tech 4 sets its biped's
		climbs anything flag). Cast again, it holds that longer */
		{
			struct biped_datum *biped = biped_try_and_get(unit_index);

			if (!biped)
			{
				return FALSE;
			}
			/* (an actor's own flag is its variant's: this is the wearer's) */
			SET_FLAG(biped->biped.flags, _biped_climbs_anything_bit, TRUE);
			shadowrun_unit->tech_ticks = SHADOWRUN_GECKO_TICKS;
			return TRUE;
		}
	}

	return FALSE;
}

void shadowrun_unit_tech_died(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit)
	{
		shadowrun_unit->tech_ticks = 0;
		shadowrun_vision_clear(unit_index);
		shadowrun_gecko_end(unit_index);
	}

	return;
}

/* a unit deleted: its marks go (one that deleted it may have held them) */
void shadowrun_unit_tech_delete(
	long unit_index)
{
	struct object_iterator iterator;
	struct object_datum *object;

	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		struct shadowrun_unit *marker = shadowrun_unit_get(iterator.index);
		short index;

		if (!marker)
		{
			continue;
		}
		for (index = 0; index < marker->vision_target_count; index++)
		{
			if (marker->vision_targets[index] == unit_index)
			{
				marker->vision_targets[index] = NONE;
			}
		}
	}

	return;
}

/* the scripts' player_change_tech (and the console's): the players know the
tech, which their units take. FALSE if they are not on a Shadowrun map */
boolean shadowrun_players_change_tech(
	short tech)
{
	struct data_iterator iterator;
	struct player_datum *player;
	boolean changed = FALSE;

	if (!shadowrun_map_loaded() || tech < 0 || tech > _shadowrun_tech_zoom_id)
	{
		return FALSE;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		struct shadowrun_player *shadowrun_player = shadowrun_player_get(iterator.datum_index);

		if (shadowrun_player)
		{
			shadowrun_player->known_tech = tech;
			if (player->unit_index != NONE)
			{
				shadowrun_unit_set_tech(player->unit_index, tech);
			}
			changed = TRUE;
		}
	}

	return changed;
}

/* ---------- private code */

/* gecko's time up: the unit climbs as its biped's definition does again */
static void shadowrun_gecko_end(
	long unit_index)
{
	struct biped_datum *biped = biped_try_and_get(unit_index);

	if (biped)
	{
		SET_FLAG(biped->biped.flags, _biped_climbs_anything_bit, FALSE);
	}

	return;
}

/* the nav point the map marks them with: one of those arrows, else the
first the HUD globals have, else none */
static long shadowrun_vision_nav_point(
	void)
{
	long nav_index = NONE;
	short index;

	for (index = 0; index < (short)NUMBEROF(shadowrun_vision_nav_names) && nav_index == NONE; index++)
	{
		nav_index = find_nav_point(shadowrun_vision_nav_names[index]);
	}
	if (nav_index == NONE)
	{
		nav_index = hud_globals && hud_globals->waypoint.arrows.count > 0 ? 0 : NONE;
	}

	return nav_index;
}

/* the unit's marks taken off, and none kept */
static void shadowrun_vision_clear(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	short index;

	if (!shadowrun_unit)
	{
		return;
	}
	if (shadowrun_unit->vision_nav_index != NONE)
	{
		for (index = 0; index < shadowrun_unit->vision_target_count; index++)
		{
			long target = shadowrun_unit->vision_targets[index];

			if (target != NONE)
			{
				hud_unit_deactivate_nav_point_with_object(unit_index, target);
			}
		}
	}
	csmemset(shadowrun_unit->vision_targets, NONE, sizeof(shadowrun_unit->vision_targets));
	shadowrun_unit->vision_target_count = 0;
	shadowrun_unit->vision_nav_index = NONE;

	return;
}

/* the caster's mark made afresh: the enemy units within its range marked,
the nearest first (so that the farthest are left out when there are more
than the most), and the mark of each it no longer holds taken off */
static void shadowrun_vision_mark(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct object_datum *caster = object_get(unit_index);
	long seen[MAXIMUM_SHADOWRUN_VISION_TARGETS];
	real seen_distance_squared[MAXIMUM_SHADOWRUN_VISION_TARGETS];
	struct object_iterator iterator;
	struct object_datum *object;
	real range_squared = (real)SHADOWRUN_VISION_RANGE * (real)SHADOWRUN_VISION_RANGE;
	short team_index;
	short seen_count = 0;
	short index;

	if (!shadowrun_unit)
	{
		return;
	}
	if (shadowrun_unit->vision_nav_index == NONE)
	{
		long nav_index = shadowrun_vision_nav_point();
		static boolean said = FALSE;

		if (nav_index == NONE)
		{
			if (!said)
			{
				error(_error_silent, "shadowrun: the map has no navpoint for vision to mark units with");
				said = TRUE;
			}
			return;
		}
		shadowrun_unit->vision_nav_index = nav_index;
	}
	team_index = (short)caster->object.owner_team_index;

	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long target = iterator.index;
		real distance_squared;

		if (target == unit_index || !game_team_is_enemy((short)object->object.owner_team_index, team_index))
		{
			continue;
		}
		distance_squared = distance_squared3d(&object->object.position, &caster->object.position);
		if (distance_squared > range_squared)
		{
			continue;
		}
		/* (the nearest of the most) */
		{
			short place = seen_count < MAXIMUM_SHADOWRUN_VISION_TARGETS ? seen_count : MAXIMUM_SHADOWRUN_VISION_TARGETS - 1;

			if (seen_count < MAXIMUM_SHADOWRUN_VISION_TARGETS)
			{
				seen_count++;
			}
			else if (distance_squared >= seen_distance_squared[place])
			{
				continue;
			}
			while (place > 0 && seen_distance_squared[place - 1] > distance_squared)
			{
				seen[place] = seen[place - 1];
				seen_distance_squared[place] = seen_distance_squared[place - 1];
				place--;
			}
			seen[place] = target;
			seen_distance_squared[place] = distance_squared;
		}
	}
	/* those it held and no longer holds are unmarked; those it now holds,
	and did not, marked */
	for (index = 0; index < shadowrun_unit->vision_target_count; index++)
	{
		long target = shadowrun_unit->vision_targets[index];
		boolean still_seen = FALSE;
		short seen_index;

		for (seen_index = 0; seen_index < seen_count; seen_index++)
		{
			if (seen[seen_index] == target)
			{
				still_seen = TRUE;
				break;
			}
		}
		if (!still_seen && target != NONE)
		{
			hud_unit_deactivate_nav_point_with_object(unit_index, target);
		}
	}
	/* (marked again: the HUD keeps a unit's point once, so this only adds
	those that were not marked) */
	csmemset(shadowrun_unit->vision_targets, NONE, sizeof(shadowrun_unit->vision_targets));
	shadowrun_unit->vision_target_count = seen_count;
	for (index = 0; index < seen_count; index++)
	{
		shadowrun_unit->vision_targets[index] = seen[index];
		hud_unit_activate_nav_point_with_object((short)shadowrun_unit->vision_nav_index, unit_index, seen[index], 0.f);
	}

	return;
}
