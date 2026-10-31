/*
SHADOWRUN_CABLE.C

The Shadowrun prototype's cable, as the executable keeps it
(shadowrun_cable.h): the grapple, the glider, and the heal of the undead,
which are one tag of the map's (t2ed, its srud's tech2).

Ported from the prototype's executable (cachesym.xbe, build 01.12.07.0132).
bipeds.c FUN_002541d0 holds the cable in the biped's state (0x10B8): fired,
attached to a unit or to the world, reeling, released, or gliding; the
collision that fires it is units.c FUN_00284EA0's, and the effects it plays
are the tag's (0x1C0 to 0x260), made on the unit's "head" marker by
FUN_001C3720 and FUN_001C3830. The executable's tech ids are FUN_0027D4E0's:
5 is the cable's, 6 the glider's.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "cache/cache_files.h" /* port: tag_get_group_tag (the tags it is given) */
#include "objects/objects.h"
#include "objects/object_types.h"
#include "physics/collisions.h"
#include "effects/effects.h"
#include "effects/effect_definitions.h" /* port: EFFECT_DEFINITION_TAG */
#include "game/game.h"
#include "network_distributed.h" /* port: MAXIMUM_TRACKED_OBJECTS */
#include "units/bipeds.h"
#include "units/units.h"
#include "shadowrun.h"
#include "shadowrun_definitions.h"
#include "shadowrun_units.h"
#include "shadowrun_tech.h"
#include "shadowrun_cable.h"

/* ---------- constants */

/* FUN_00284EC0's: the point the collision starts at, ahead of the unit (0.1
world units), and the speed it reaches along the unit's forward (2.0): 300
units, 30 m, whatever the unit's pitch (FUN_00284EA0's 0x40000000) */
#define SHADOWRUN_CABLE_ORIGIN_OFFSET 0.1f
#define SHADOWRUN_CABLE_SPEED 2.0f

/* FUN_002541D0's: how close to a unit the bolt comes to attach to it, how
close the cable's end comes to the unit to be attached, how far the unit may
be from the bolt before the cable snaps, how long the release takes (ticks)
and the least the unit is moved for a snap to count (0.09 units) */
#define SHADOWRUN_CABLE_ATTACH_DISTANCE 0.1225f
#define SHADOWRUN_CABLE_SNAP_DISTANCE 0.09f
#define SHADOWRUN_CABLE_RELEASE_TICKS 15

/* FUN_0025B7D0's: a unit whose weapon is out waits this many ticks before
its cable may fire (0x1088's 5) */
#define SHADOWRUN_CABLE_FIRE_WAIT_TICKS 5

/* port: how much of the tag's reel speed is used: half of it, as the tag's
10 units a second (attach) and 8 (world) pull too fast to steer at (the
executable's own, unchanged) */
#define SHADOWRUN_CABLE_REEL_SCALE 0.5f

/* ---------- globals */

/* (each unit's, by its absolute index: a unit is a biped, whose datum this
build's has no room for the executable's fields) */

/* the cable's state (bipeds.c FUN_002541D0's 0x10B8, in the order its
numbers are) */
static short shadowrun_cable_states[MAXIMUM_TRACKED_OBJECTS];

/* the cable's own: the bolt it fired (0x10E8), the unit it attached to
(0x10EC) and its effect (0x750), and the ticks it was fired at (0x5F0) and
has been attached (0x432) */
static long shadowrun_cable_projectiles[MAXIMUM_TRACKED_OBJECTS];
static long shadowrun_cable_attached_units[MAXIMUM_TRACKED_OBJECTS];
static long shadowrun_cable_effects[MAXIMUM_TRACKED_OBJECTS];
static long shadowrun_cable_fired_times[MAXIMUM_TRACKED_OBJECTS];
static long shadowrun_cable_attached_ticks[MAXIMUM_TRACKED_OBJECTS];
/* the unit's wait before the cable may fire again (0x108C) */
static long shadowrun_cable_fire_waits[MAXIMUM_TRACKED_OBJECTS];

/* where the bolt attached (the tag's 0x428 in the executable), and the unit's
position as the cable last left it (0x424): the collision this tick starts
from where the last one ended */
static real_point3d shadowrun_cable_points[MAXIMUM_TRACKED_OBJECTS];

/* a unit's slot in the tables above: its object index is a datum's
(identifier<<16 | slot), and the identifier is not 0 for one reused */
static long shadowrun_cable_slot(
	long unit_index)
{
	return DATUM_INDEX_TO_ABSOLUTE_INDEX(unit_index);
}

/* (a unit whose slot the tables do not hold: nothing is kept for it. A
unit's index is a datum's, and a large identifier makes it negative as a
signed long: only its slot is looked at, which is the low half) */
static boolean shadowrun_cable_slot_valid(
	long unit_index)
{
	return (unsigned long)shadowrun_cable_slot(unit_index) < (unsigned long)MAXIMUM_TRACKED_OBJECTS;
}

/* ---------- private prototypes */

static struct shadowrun_tech2_definition *shadowrun_cable_definition(
	long unit_index);
static long shadowrun_cable_collision(
	real_point3d const *origin,
	real_vector3d const *direction,
	real distance,
	long unit_index,
	real_point3d *point);
static long shadowrun_cable_effect(
	long definition_index,
	long unit_index,
	long owner_object_index);
static void shadowrun_cable_remove_effect(
	long unit_index);
static void shadowrun_cable_stop(
	long unit_index);
static boolean shadowrun_cable_move(
	long unit_index,
	real speed,
	boolean keeps_velocity);

/* ---------- private code */

/* the map's t2ed the unit's srud names (units.c FUN_0025E860's), or NULL */
static struct shadowrun_tech2_definition *shadowrun_cable_definition(
	long unit_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_player(unit_index);

	return definition && definition->tech2.index != NONE ? tag_get(SHADOWRUN_TECH2_DEFINITION_TAG, definition->tech2.index) : NULL;
}

/* FUN_00284EC0: the first thing along `direction` from `origin`, its distance
put in `distance`: NONE if it hits nothing */
static long shadowrun_cable_collision(
	real_point3d const *origin,
	real_vector3d const *direction,
	real distance,
	long unit_index,
	real_point3d *point)
{
	real_vector3d vector;
	struct collision_result collision;

	scale_vector3d(direction, distance, &vector);
	if (collision_test_vector(_collision_test_for_projectiles_flags, origin, &vector, unit_index,
							  &collision))
	{
		if (point)
		{
			*point = collision.point;
		}
		return collision.type == _collision_result_object ? collision.object_index : NONE;
	}

	return NONE;
}

/* the effect of a tag's, made on the unit's head marker (FUN_001C3720's,
FUN_001C3830's), as the unit's own */
static long shadowrun_cable_effect(
	long definition_index,
	long unit_index,
	long owner_object_index)
{
	struct object_marker marker;
	real_point3d point;
	real_vector3d forward;

	if (definition_index == NONE)
	{
		return NONE;
	}
	/* port: only an effect the map loaded (a t2ed's reference that did not
	resolve is no tag index at all) */
	if (tag_get_group_tag(definition_index) != EFFECT_DEFINITION_TAG)
	{
		error(_error_silent, "shadowrun cable: %08lx is not an effect tag", (unsigned long)definition_index);
		return NONE;
	}
	if (object_get_marker_by_name(unit_index, "head", &marker, 1) > 0)
	{
		point = marker.matrix.position;
		forward = marker.matrix.forward;
	}
	else
	{
		object_get_origin(unit_index, &point);
		object_get_orientation(unit_index, &forward, NULL);
	}

	return effect_new_from_object(definition_index, owner_object_index, unit_index, NONE, 0.f, 0.f,
								  NULL, NULL);
}

/* the cable's effect taken away, if it has one (FUN_001C40F0's) */
static void shadowrun_cable_remove_effect(
	long unit_index)
{
	long *effect_index = &shadowrun_cable_effects[shadowrun_cable_slot(unit_index)];

	if (*effect_index != NONE)
	{
		effect_delete(*effect_index);
		*effect_index = NONE;
	}

	return;
}

/* FUN_0025B650: the cable let go: the bolt's and the attached unit's, their
flags cleared, and the state back to none (the executable scales what it
holds by the tag's, which the map's is 1.0: the same) */
static void shadowrun_cable_stop(
	long unit_index)
{
	long attached_unit_index = shadowrun_cable_attached_units[shadowrun_cable_slot(unit_index)];
	long projectile_index = shadowrun_cable_projectiles[shadowrun_cable_slot(unit_index)];

	if (attached_unit_index != NONE)
	{
		struct shadowrun_unit *attached = shadowrun_unit_get(attached_unit_index);

		if (attached)
		{
			attached->cable_held = FALSE;
		}
	}
	if (projectile_index != NONE)
	{
		struct object_datum *projectile = object_try_and_get_and_verify_type(projectile_index,
																			 _object_mask_projectile);

		if (projectile)
		{
			SET_FLAG(projectile->object.flags, _object_connected_to_map_bit, FALSE);
		}
	}
	shadowrun_cable_states[shadowrun_cable_slot(unit_index)] = _shadowrun_cable_state_none;
	shadowrun_cable_attached_units[shadowrun_cable_slot(unit_index)] = NONE;
	shadowrun_cable_projectiles[shadowrun_cable_slot(unit_index)] = NONE;
	shadowrun_cable_attached_ticks[shadowrun_cable_slot(unit_index)] = 0;
	shadowrun_cable_fired_times[shadowrun_cable_slot(unit_index)] = game_time_get();

	return;
}



/* FUN_002541D0's step, for a unit attached to the world: its velocity is
set along the cable toward the point the bolt holds (the executable's
local_b8/b4/b0, its 0x428 point less its 0x3, times the tag's speed over
the ticks). It is set every tick, as the executable sets it, so the unit
travels at the tag's speed until the cable lets go */
static boolean shadowrun_cable_move(
	long unit_index,
	real speed,
	boolean keeps_velocity)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	long slot = shadowrun_cable_slot(unit_index);
	real_vector3d direction;
	real length;

	if (!unit)
	{
		return FALSE;
	}
	vector_from_points3d(&unit->object.position, &shadowrun_cable_points[slot], &direction);
	length = normalize3d(&direction);
	if (length <= 0.f)
	{
		return TRUE;
	}
	/* (to a unit: the cable stops the unit's own movement; to the world it
	keeps its own, as the executable's 0x100 flag leaves it) */
	if (!keeps_velocity)
	{
		unit->unit.throttle = *global_zero_vector3d;
	}
	/* (the cable's velocity is the unit's, as the executable sets it:
	not added to one it had, which would carry a fall into the pull; at
	half the tag's speed, this build's) */
	scale_vector3d(&direction, speed * SHADOWRUN_CABLE_REEL_SCALE / TICKS_PER_SECOND,
		&unit->object.translational_velocity);

	return TRUE;
}

/* ---------- public code */

void shadowrun_cable_initialize_for_new_map(
	void)
{
	long index;

	for (index = 0; index < MAXIMUM_TRACKED_OBJECTS; index++)
	{
		shadowrun_cable_states[index] = _shadowrun_cable_state_none;
		shadowrun_cable_projectiles[index] = NONE;
		shadowrun_cable_attached_units[index] = NONE;
		shadowrun_cable_effects[index] = NONE;
		shadowrun_cable_fired_times[index] = 0;
		shadowrun_cable_attached_ticks[index] = 0;
		shadowrun_cable_fire_waits[index] = 0;
	}

	return;
}

void shadowrun_cable_new(
	long unit_index)
{
	if (shadowrun_cable_slot_valid(unit_index))
	{
		shadowrun_cable_states[shadowrun_cable_slot(unit_index)] = _shadowrun_cable_state_none;
		shadowrun_cable_projectiles[shadowrun_cable_slot(unit_index)] = NONE;
		shadowrun_cable_attached_units[shadowrun_cable_slot(unit_index)] = NONE;
		shadowrun_cable_effects[shadowrun_cable_slot(unit_index)] = NONE;
		shadowrun_cable_fired_times[shadowrun_cable_slot(unit_index)] = 0;
		shadowrun_cable_attached_ticks[shadowrun_cable_slot(unit_index)] = 0;
		shadowrun_cable_fire_waits[shadowrun_cable_slot(unit_index)] = 0;
	}

	return;
}

void shadowrun_cable_delete(
	long unit_index)
{
	if (shadowrun_cable_slot_valid(unit_index))
	{
		shadowrun_cable_remove_effect(unit_index);
		shadowrun_cable_states[shadowrun_cable_slot(unit_index)] = _shadowrun_cable_state_none;
		shadowrun_cable_projectiles[shadowrun_cable_slot(unit_index)] = NONE;
		shadowrun_cable_attached_units[shadowrun_cable_slot(unit_index)] = NONE;
		shadowrun_cable_fire_waits[shadowrun_cable_slot(unit_index)] = 0;
	}

	return;
}

/* FUN_0027D4E0's case 5 and FUN_0027D760's case 4: the cable's tech taken
up or let go */
void shadowrun_cable_begin(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (!shadowrun_unit || shadowrun_unit->cable_tech || !shadowrun_cable_slot_valid(unit_index))
	{
		return;
	}
	shadowrun_unit->cable_tech = TRUE;
	shadowrun_cable_states[shadowrun_cable_slot(unit_index)] = _shadowrun_cable_state_none;

	return;
}

void shadowrun_cable_end(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (!shadowrun_unit || !shadowrun_unit->cable_tech || !shadowrun_cable_slot_valid(unit_index))
	{
		return;
	}
	shadowrun_cable_remove_effect(unit_index);
	shadowrun_cable_stop(unit_index);
	shadowrun_unit->cable_tech = FALSE;

	return;
}

/* FUN_0025B5E0: the cable released by itself (the map's, as it takes the tech
away): the bolt and the effect go, and the unit's fall is not held */
void shadowrun_cable_force_release(
	long unit_index)
{
	if (!shadowrun_cable_slot_valid(unit_index))
	{
		return;
	}
	shadowrun_cable_remove_effect(unit_index);
	shadowrun_cable_stop(unit_index);

	return;
}

/* port: whether a cable holds the unit now, to the world or to a unit: it is
not hurt by falling while one does (bipeds.c's falling damage) */
boolean shadowrun_cable_holds(
	long unit_index)
{
	if (!shadowrun_cable_slot_valid(unit_index))
	{
		return FALSE;
	}
	return shadowrun_cable_states[shadowrun_cable_slot(unit_index)] == _shadowrun_cable_state_attached_to_unit ||
		shadowrun_cable_states[shadowrun_cable_slot(unit_index)] == _shadowrun_cable_state_attached_to_world;
}

/* whether the unit is reeling in, or held by a cable: it is seen from behind
(shadowrun_camera.c) */
boolean shadowrun_cable_reeling(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	return shadowrun_unit && (shadowrun_unit->cable_held ||
		(shadowrun_cable_slot_valid(unit_index) &&
			shadowrun_cable_states[shadowrun_cable_slot(unit_index)] == _shadowrun_cable_state_reeling));
}

/* FUN_0025B7D0: the cable fires: along the unit's forward, 30 m, whatever its
pitch; the first thing it hits is attached to, a unit or the world, and the
unit is reeled in over the tag's ticks. TRUE when it fired */
boolean shadowrun_cable_fire(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct shadowrun_tech2_definition *definition = shadowrun_cable_definition(unit_index);
	struct unit_datum *unit = unit_try_and_get(unit_index);
	real_point3d origin;
	real_point3d point;
	real_vector3d forward;
	long attached_unit_index = NONE;
	long projectile_index;

	/* port: each refusal says why (the unit's index, and what stopped it):
a left bumper that does nothing otherwise says nothing */
	if (!shadowrun_unit || !unit)
	{
		error(_error_silent, "shadowrun cable: unit %08lx is not on this map (no unit)", (unsigned long)unit_index);
		return FALSE;
	}
	if (!shadowrun_cable_slot_valid(unit_index))
	{
		error(_error_silent, "shadowrun cable: unit %08lx is past the cable's tables (%d)",
			(unsigned long)unit_index, MAXIMUM_TRACKED_OBJECTS);
		return FALSE;
	}
	if (!definition)
	{
		error(_error_silent, "shadowrun cable: unit %08lx's srud names no tech2 (the cable's tag)",
			(unsigned long)unit_index);
		return FALSE;
	}
	if (!shadowrun_unit->cable_tech)
	{
		error(_error_silent, "shadowrun cable: unit %08lx has not taken the cable's tech (tech %d)",
			(unsigned long)unit_index, _shadowrun_tech_grapple_id);
		return FALSE;
	}
	/* port: the button again lets the cable go, as a toggle: the executable's
	state 2 let it go after 15 ticks, this build's lasts until it is pressed
	again (or the unit arrives) */
	if (shadowrun_cable_states[shadowrun_cable_slot(unit_index)] != _shadowrun_cable_state_none)
	{
		if (shadowrun_cable_reeling(unit_index) || shadowrun_cable_holds(unit_index))
		{
			if (definition->player_release_effect.index != NONE)
			{
				shadowrun_cable_remove_effect(unit_index);
				shadowrun_cable_effects[shadowrun_cable_slot(unit_index)] = shadowrun_cable_effect(
					definition->player_release_effect.index, unit_index, unit_index);
			}
			shadowrun_cable_stop(unit_index);
			return TRUE;
		}
		error(_error_silent, "shadowrun cable: unit %08lx's cable is still out (state %d)",
			(unsigned long)unit_index, shadowrun_cable_states[shadowrun_cable_slot(unit_index)]);
		return FALSE;
	}
	/* (FUN_0025B7D0's wait: 0x108C, which a unit whose weapon is out sets) */
	if (shadowrun_cable_fire_waits[shadowrun_cable_slot(unit_index)] > 0)
	{
		error(_error_silent, "shadowrun cable: unit %08lx waits %ld ticks before the cable",
			(unsigned long)unit_index, shadowrun_cable_fire_waits[shadowrun_cable_slot(unit_index)]);
		return FALSE;
	}
	/* (the unit's forward, not its aim: the executable fires level) */
	/* port: along where the unit aims (the camera, in first person), so a
	grapple goes where the player looks, up a wall or down a shaft, and not
	only ahead. The executable fired along the unit's forward (its 0x0.1,0,0
	collision); this is a departure, as the hologram's cast is */
	origin = unit->object.position;
	unit_get_aiming_vector(unit_index, &forward);
	point_from_line3d(&origin, &forward, SHADOWRUN_CABLE_ORIGIN_OFFSET, &origin);
	/* (the tag's fire distance is how far it reaches: 15 m in missionlet,
	which at a metre a tick is its 30 m) */
	{
		/* port: what the t2ed holds, said once: the cable's fields, as read */
		static boolean said = FALSE;

		if (!said)
		{
			said = TRUE;
			error(_error_silent, "shadowrun cable: t2ed bolt %08lx, fire %g m, hit %g, attach %g, snap %g, %ld ticks",
				(unsigned long)definition->projectile.index, (double)definition->fire_distance,
				(double)definition->hit_distance, (double)definition->attach_distance,
				(double)definition->snap_distance, definition->reel_ticks);
		}
	}
	attached_unit_index = shadowrun_cable_collision(&origin, &forward,
													definition->fire_distance, unit_index, &point);
	/* (where it holds: the cable reels toward it from here) */
	shadowrun_cable_points[shadowrun_cable_slot(unit_index)] = point;
	if (attached_unit_index != NONE)
	{
		struct object_datum *hit = object_try_and_get(attached_unit_index);

		/* (it attaches to the thing it hit, so the reel starts there) */
		if (hit)
		{
			shadowrun_cable_points[shadowrun_cable_slot(unit_index)] = hit->object.position;
		}
	}
	/* (the bolt is the map's projectile, if it has one; the cable works
	without it) */
	projectile_index = shadowrun_grapple_fire(unit_index, definition->projectile.index);
	shadowrun_cable_projectiles[shadowrun_cable_slot(unit_index)] = projectile_index;
	shadowrun_cable_fired_times[shadowrun_cable_slot(unit_index)] = game_time_get();
	shadowrun_cable_attached_ticks[shadowrun_cable_slot(unit_index)] = 0;
	/* (FUN_0025B7D0's release: the fire effect, and the player's travel) */
	shadowrun_cable_remove_effect(unit_index);
	shadowrun_cable_effects[shadowrun_cable_slot(unit_index)] = shadowrun_cable_effect(
		definition->player_attach_effect.index, unit_index, unit_index);
	if (attached_unit_index != NONE &&
		object_try_and_get_and_verify_type(attached_unit_index, _object_mask_unit))
	{
		struct shadowrun_unit *attached = shadowrun_unit_get(attached_unit_index);

		shadowrun_cable_states[shadowrun_cable_slot(unit_index)] = _shadowrun_cable_state_attached_to_unit;
		shadowrun_cable_attached_units[shadowrun_cable_slot(unit_index)] = attached_unit_index;
		if (attached)
		{
			attached->cable_held = TRUE;
		}
	}
	else
	{
		shadowrun_cable_states[shadowrun_cable_slot(unit_index)] = _shadowrun_cable_state_attached_to_world;
	}
	shadowrun_cable_fire_waits[shadowrun_cable_slot(unit_index)] = SHADOWRUN_CABLE_FIRE_WAIT_TICKS;

	return TRUE;
}

/* FUN_002541D0: each tick of a unit that has the cable: it fires it, holds
it, reels itself in, lets it go, or glides. TRUE when the unit is held */
boolean shadowrun_cable_update(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct shadowrun_tech2_definition *definition;
	long state;
	boolean held = FALSE;

	if (!shadowrun_unit || !unit || !shadowrun_cable_slot_valid(unit_index))
	{
		return FALSE;
	}
	if (shadowrun_cable_fire_waits[shadowrun_cable_slot(unit_index)] > 0)
	{
		shadowrun_cable_fire_waits[shadowrun_cable_slot(unit_index)]--;
	}
	/* (a bolt that is gone ends the cable's hold of the world) */
	if (shadowrun_cable_projectiles[shadowrun_cable_slot(unit_index)] != NONE &&
		!object_try_and_get_and_verify_type(shadowrun_cable_projectiles[shadowrun_cable_slot(unit_index)],
											_object_mask_projectile))
	{
		shadowrun_cable_projectiles[shadowrun_cable_slot(unit_index)] = NONE;
	}
	if (!shadowrun_unit->cable_tech)
	{
		return FALSE;
	}
	definition = shadowrun_cable_definition(unit_index);
	if (!definition)
	{
		return FALSE;
	}
	state = shadowrun_cable_states[shadowrun_cable_slot(unit_index)];
	switch (state)
	{
	case _shadowrun_cable_state_none:
		break;

	case _shadowrun_cable_state_attached_to_unit:
	{
		struct object_datum *attached = object_try_and_get_and_verify_type(
			shadowrun_cable_attached_units[shadowrun_cable_slot(unit_index)], _object_mask_unit);

		if (!attached)
		{
			shadowrun_cable_stop(unit_index);
			break;
		}
		shadowrun_cable_attached_ticks[shadowrun_cable_slot(unit_index)]++;
		/* (to a unit: the cable stops the unit's own movement) */
		if (!shadowrun_cable_move(unit_index, definition->attach_distance, FALSE))
		{
			shadowrun_cable_stop(unit_index);
			break;
		}
		/* port: the reel ends when the button is pressed again, and not on
		arrival: a cable that ends there cannot be held, and a player who
		presses it onto a wall stays on it (the executable's state 2 let go
		once it arrived, or after 15 ticks) */
		held = TRUE;
	}
	break;

	case _shadowrun_cable_state_attached_to_world:
	{
		shadowrun_cable_attached_ticks[shadowrun_cable_slot(unit_index)]++;
		/* (to the world: the unit is reeled toward where the bolt hit, and
		keeps its own movement. The bolt itself is not needed: one that hit a
		wall detonates and is deleted as the cable holds, and the point it
		hit is what the reel follows) */
		if (!shadowrun_cable_move(unit_index, definition->hit_distance, TRUE))
		{
			/* (the cable snapped: the release effect, and a snap for a
			unit that was moved) */
			if (definition->cable_snap_effect.index != NONE)
			{
				shadowrun_cable_remove_effect(unit_index);
				shadowrun_cable_effects[shadowrun_cable_slot(unit_index)] = shadowrun_cable_effect(
					definition->cable_snap_effect.index, unit_index, unit_index);
			}
			shadowrun_cable_stop(unit_index);
			break;
		}
		/* port: the reel ends when the button is pressed again, and not on
		arrival (see the unit's case above) */
		held = TRUE;
	}
	break;

	case _shadowrun_cable_state_reeling:
		/* (the reel is the unit's own, as the executable's: the release
		effect at its end) */
		if (definition->player_release_effect.index != NONE)
		{
			shadowrun_cable_remove_effect(unit_index);
			shadowrun_cable_effects[shadowrun_cable_slot(unit_index)] = shadowrun_cable_effect(
				definition->player_release_effect.index, unit_index, unit_index);
		}
		shadowrun_cable_stop(unit_index);
		break;

	case _shadowrun_cable_state_released:
		shadowrun_cable_stop(unit_index);
		break;

	case _shadowrun_cable_state_gliding:
		/* (the glider is its own: shadowrun_tech.c) */
		break;
	}

	return held;
}

