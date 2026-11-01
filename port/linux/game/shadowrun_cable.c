/*
SHADOWRUN_CABLE.C

The Shadowrun prototype's cable, the grapple, as the executable keeps it
(shadowrun_cable.h): one tag of the map's (t2ed, its srud's tech2).

Ported from the prototype's executable (cachesym.xbe, build 01.12.07.0132):
bipeds.c FUN_0025B7D0 fires it, FUN_002541D0 (the biped's physics) runs
its state (the biped's 0x10B8), FUN_00251840 draws it each tick,
FUN_0025B650 lets it go, and biped_limp_noodle.c's biped update counts its
waits. Fired, its end travels from the unit's "body" marker the way it aims
at the tag's speed; where it hits the world the unit is reeled in to it
and hangs there until it stands on something, and where it hits a biped
that biped is pulled to the unit instead. The hook (the tag's scenery)
sits on the end, and the line between it and the unit is the tag's
projectile, made again each tick at the end and moving to the unit in that
tick, which its contrail draws.
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
#include "game/game_engine.h"
#include "items/projectile_definitions.h"
#include "network_distributed.h" /* port: MAXIMUM_TRACKED_OBJECTS */
#include "units/bipeds.h"
#include "units/units.h"
#include "shadowrun.h"
#include "shadowrun_definitions.h"
#include "shadowrun_units.h"
#include "shadowrun_tech.h"
#include "shadowrun_cable.h"

/* ---------- constants */

/* FUN_002541D0's: how close (squared) the unit comes to the end to hang
there, how many ticks it may be stuck before it hangs anyway, how far off
(squared) a collision may be before the cable is cut, and how far apart a
pulled corpse and the unit come before it is let go; the hanging unit's
look for ground below it (0.2 units up and down) */
#define SHADOWRUN_CABLE_ARRIVE_DISTANCE_SQUARED 0.1225f
#define SHADOWRUN_CABLE_STUCK_TICKS 15
#define SHADOWRUN_CABLE_CUT_DISTANCE_SQUARED 0.09f
#define SHADOWRUN_CABLE_CORPSE_DISTANCE_SQUARED 1.f
#define SHADOWRUN_CABLE_GROUND_DISTANCE 0.2f

/* the material a cable's end bounces off (FUN_002541D0's 0x1F), and none
(what it hits has no material: it is cut) */
#define SHADOWRUN_CABLE_BOUNCE_MATERIAL 0x1F

/* FUN_0025B7D0's wait before the cable fires again (0x1088's 5) */
#define SHADOWRUN_CABLE_FIRE_WAIT_TICKS 5

/* the biped update's: a unit that moved less than this (squared) in a tick
is stuck (0x1090) */
#define SHADOWRUN_CABLE_STUCK_DISTANCE_SQUARED 2.5e-05f

/* ---------- globals */

/* (each unit's, by its absolute index: a biped's datum in this build has no
room for the executable's fields) */
struct shadowrun_cable
{
	/* 0x10B8 */
	long state;
	/* the end: where it is (0x10A0) and the way it travels (0x10AC) */
	real_point3d end;
	real_vector3d direction;
	/* the biped it pulls, or the one pulling it (0x10CC) */
	long attached_unit_index;
	/* the ticks it has pulled (0x10C8) */
	long attached_ticks;
	/* the hook (0x78C), the line (0x790) and the effect it plays while it
	travels and reels (0x750) */
	long hook_index;
	long line_index;
	long effect_index;
	/* the waits: before it fires again (0x1088), and after it was out
	(0x108C: pressed during it, the cable lets go) */
	long fire_wait_ticks;
	long cooldown_ticks;
	/* the ticks the unit has not moved (0x1090), and where it was (0x10BC) */
	long stuck_ticks;
	real_point3d last_position;
};

static struct shadowrun_cable shadowrun_cables[MAXIMUM_TRACKED_OBJECTS];

/* ---------- private code */

/* a unit's cable: its object index is a datum's (identifier<<16 | slot), and
the table is by slot. NULL past the table */
static struct shadowrun_cable *shadowrun_cable_get(
	long unit_index)
{
	unsigned long slot = (unsigned long)DATUM_INDEX_TO_ABSOLUTE_INDEX(unit_index);

	return unit_index != NONE && slot < (unsigned long)MAXIMUM_TRACKED_OBJECTS ? &shadowrun_cables[slot] : NULL;
}

static void shadowrun_cable_clear(
	struct shadowrun_cable *cable)
{
	csmemset(cable, 0, sizeof(*cable));
	cable->state = _shadowrun_cable_state_none;
	cable->attached_unit_index = NONE;
	cable->hook_index = NONE;
	cable->line_index = NONE;
	cable->effect_index = NONE;

	return;
}

/* the map's t2ed the unit's srud names (units.c FUN_0025E860's), or NULL */
static struct shadowrun_tech2_definition *shadowrun_cable_definition(
	long unit_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_player(unit_index);

	return definition && definition->tech2.index != NONE ? tag_get(SHADOWRUN_TECH2_DEFINITION_TAG, definition->tech2.index) : NULL;
}

/* the unit's "body" marker (FUN_00251820: units.c FUN_00278530), or its
origin */
static void shadowrun_cable_origin(
	long unit_index,
	real_point3d *point)
{
	struct object_marker marker;

	if (object_get_marker_by_name(unit_index, "body", &marker, 1) > 0)
	{
		*point = marker.matrix.position;
	}
	else
	{
		object_get_origin(unit_index, point);
	}

	return;
}

/* port: whether a t2ed's reference names a tag of the group (one that did
not resolve is no tag index at all) */
static boolean shadowrun_cable_tag(
	long definition_index,
	unsigned long group_tag)
{
	return definition_index != NONE && tag_get_group_tag(definition_index) == group_tag;
}

/* FUN_000D72E0: an effect of the tag's, on the object */
static void shadowrun_cable_effect(
	struct tag_reference const *reference,
	long object_index)
{
	/* port: on an object that is still there (the hook may have gone with
	the structure: a switch of the map's BSPs deletes what it holds) */
	if (object_index != NONE && object_try_and_get_and_verify_type(object_index, NONE) &&
		shadowrun_cable_tag(reference->index, EFFECT_DEFINITION_TAG))
	{
		effect_new_from_object(reference->index, object_index, object_index, NONE, 0.f, 0.f, NULL, NULL);
	}

	return;
}

/* FUN_000D7C30: the effect it plays while it travels and reels, stopped */
static void shadowrun_cable_stop_effect(
	struct shadowrun_cable *cable)
{
	if (cable->effect_index != NONE)
	{
		effect_stop(cable->effect_index, TRUE);
		cable->effect_index = NONE;
	}

	return;
}

/* FUN_000D7190: a looping effect of the tag's on the unit, kept as the
cable's */
static void shadowrun_cable_loop_effect(
	struct shadowrun_cable *cable,
	struct tag_reference const *reference,
	long unit_index)
{
	shadowrun_cable_stop_effect(cable);
	if (shadowrun_cable_tag(reference->index, EFFECT_DEFINITION_TAG))
	{
		cable->effect_index = effect_new_looping(reference->index, unit_index, NONE, NONE, NONE);
	}

	return;
}

/* an object the cable made, deleted */
static void shadowrun_cable_delete_object(
	long *object_index)
{
	if (*object_index != NONE)
	{
		if (object_try_and_get_and_verify_type(*object_index, NONE))
		{
			object_delete(*object_index);
		}
		*object_index = NONE;
	}

	return;
}

/* the unit's velocity scaled (a let-go cable's) */
static void shadowrun_cable_scale_velocity(
	long unit_index,
	real scale)
{
	struct object_datum *object = object_try_and_get_and_verify_type(unit_index, _object_mask_biped);

	if (object)
	{
		scale_vector3d(&object->object.translational_velocity, scale, &object->object.translational_velocity);
	}

	return;
}

/* FUN_0025B650: the cable let go: the release effect (the reel's; the
hanging unit's own), its velocity kept by the scale where it was moved by
the cable, and the biped it pulled let go too */
static void shadowrun_cable_stop(
	long unit_index,
	real scale)
{
	struct shadowrun_cable *cable = shadowrun_cable_get(unit_index);
	struct shadowrun_tech2_definition *definition = shadowrun_cable_definition(unit_index);

	if (!cable)
	{
		return;
	}
	if (definition)
	{
		if (cable->state == _shadowrun_cable_state_fired || cable->state == _shadowrun_cable_state_reeling ||
			cable->state == _shadowrun_cable_state_pulling)
		{
			shadowrun_cable_effect(&definition->cable_release_effect, unit_index);
		}
		if (cable->state == _shadowrun_cable_state_hanging)
		{
			shadowrun_cable_effect(&definition->player_release_effect, unit_index);
		}
	}
	if (cable->state == _shadowrun_cable_state_reeling || cable->state == _shadowrun_cable_state_hanging ||
		cable->state == _shadowrun_cable_state_pulling)
	{
		shadowrun_cable_scale_velocity(unit_index, scale);
	}
	if (cable->attached_unit_index != NONE)
	{
		struct shadowrun_cable *attached = shadowrun_cable_get(cable->attached_unit_index);

		if (attached && object_try_and_get_and_verify_type(cable->attached_unit_index, _object_mask_biped))
		{
			shadowrun_cable_scale_velocity(cable->attached_unit_index, scale);
			attached->state = _shadowrun_cable_state_none;
			attached->attached_unit_index = NONE;
		}
		cable->attached_unit_index = NONE;
	}
	cable->state = _shadowrun_cable_state_none;

	return;
}

/* FUN_002541D0's fired cable: its end travels at the tag's speed; the
first thing it hits ends its travel (it bounces off ice, is cut by what has
no material) and holds it: the world reels the unit in, a biped is pulled.
It is let go past its reach, and cut where something comes between it and
the unit */
static void shadowrun_cable_travel(
	long unit_index,
	struct shadowrun_cable *cable,
	struct shadowrun_tech2_definition *definition)
{
	struct collision_result collision;
	real_point3d origin;
	real_point3d end;

	cable->stuck_ticks = 0;
	shadowrun_cable_origin(unit_index, &origin);
	point_from_line3d(&cable->end, &cable->direction, definition->hit_distance / TICKS_PER_SECOND, &end);
	if (!collision_test_line(_collision_test_for_projectiles_flags, &cable->end, &end, unit_index, &collision))
	{
		cable->end = end;
	}
	else
	{
		shadowrun_cable_stop_effect(cable);
		cable->state = _shadowrun_cable_state_none;
		cable->end = collision.point;
		if (collision.material_type == SHADOWRUN_CABLE_BOUNCE_MATERIAL)
		{
			shadowrun_cable_effect(&definition->hit_bounce_effect_2, unit_index);
			shadowrun_cable_effect(&definition->hit_bounce_effect, cable->hook_index);
		}
		else if (collision.material_type == NONE)
		{
			shadowrun_cable_effect(&definition->cable_release_effect, unit_index);
		}
		else
		{
			long hit_index = collision.type == _collision_result_object ? collision.object_index : NONE;
			struct object_datum *hit = hit_index != NONE ? object_try_and_get_and_verify_type(hit_index, NONE) : NULL;

			shadowrun_cable_loop_effect(cable, &definition->player_travel_effect, unit_index);
			cable->state = _shadowrun_cable_state_reeling;
			/* (a biped it hits, or rides, is pulled to the unit instead) */
			if (hit && hit->object.parent_object_index != NONE)
			{
				hit_index = hit->object.parent_object_index;
				hit = object_try_and_get_and_verify_type(hit_index, NONE);
			}
			if (hit && hit->object.type == _object_type_biped)
			{
				struct shadowrun_cable *pulled = shadowrun_cable_get(hit_index);

				if (pulled)
				{
					struct object_datum *unit = object_get(unit_index);

					cable->state = _shadowrun_cable_state_pulling;
					cable->attached_unit_index = hit_index;
					cable->attached_ticks = 0;
					pulled->state = _shadowrun_cable_state_pulled;
					pulled->attached_unit_index = unit_index;
					pulled->attached_ticks = 0;
					vector_from_points3d(&hit->object.position, &unit->object.position, &pulled->direction);
					normalize3d(&pulled->direction);
					negate_vector3d(&pulled->direction, &cable->direction);
					shadowrun_cable_effect(&definition->undead_heal_effect, hit_index);
				}
			}
			shadowrun_cable_effect(&definition->hit_success_effect_2, unit_index);
			shadowrun_cable_effect(&definition->hit_success_effect, cable->hook_index);
		}
	}
	/* (past its reach it is let go; something between it and the unit cuts
	it) */
	if (distance_squared3d(&cable->end, &origin) > definition->fire_distance * definition->fire_distance)
	{
		shadowrun_cable_stop(unit_index, 1.f);
	}
	if (collision_test_line(_collision_test_for_projectiles_flags, &origin, &cable->end, unit_index, &collision) &&
		distance_squared3d(&cable->end, &collision.point) > SHADOWRUN_CABLE_CUT_DISTANCE_SQUARED)
	{
		shadowrun_cable_stop(unit_index, 1.f);
		shadowrun_cable_effect(&definition->cable_snap_effect, unit_index);
	}

	return;
}

/* FUN_002541D0's pull of a biped: its end follows the biped's, which is
pulled to the unit until the tag's ticks have passed (a corpse until it is
close), or something comes between them */
static void shadowrun_cable_pull(
	long unit_index,
	struct shadowrun_cable *cable,
	struct shadowrun_tech2_definition *definition)
{
	struct object_datum *unit = object_get(unit_index);
	struct object_datum *pulled = cable->attached_unit_index != NONE ?
		object_try_and_get_and_verify_type(cable->attached_unit_index, _object_mask_biped) : NULL;
	struct shadowrun_cable *pulled_cable = pulled ? shadowrun_cable_get(cable->attached_unit_index) : NULL;
	struct collision_result collision;
	real_point3d origin;
	real_point3d pulled_origin;

	cable->attached_ticks++;
	if (!pulled || !pulled_cable)
	{
		shadowrun_cable_stop(unit_index, definition->snap_distance);
		return;
	}
	vector_from_points3d(&pulled->object.position, &unit->object.position, &pulled_cable->direction);
	normalize3d(&pulled_cable->direction);
	negate_vector3d(&pulled_cable->direction, &cable->direction);
	/* (its end on the biped's body, the cable cut where something comes
	between them) */
	shadowrun_cable_origin(unit_index, &origin);
	shadowrun_cable_origin(cable->attached_unit_index, &pulled_origin);
	cable->end = pulled_origin;
	if (!TEST_FLAG(pulled->object.damage_flags, _object_dead_bit) ?
		cable->attached_ticks >= definition->undead_heal_points :
		distance_squared3d(&unit->object.position, &pulled->object.position) < SHADOWRUN_CABLE_CORPSE_DISTANCE_SQUARED)
	{
		shadowrun_cable_stop(unit_index, definition->snap_distance);
	}
	/* port: the pulled biped itself is not what comes between them (the
	executable's line met it too, short of its body marker) */
	else if (collision_test_line(_collision_test_for_projectiles_flags, &origin, &pulled_origin, unit_index, &collision) &&
		!(collision.type == _collision_result_object &&
			object_get_ultimate_parent(collision.object_index) == object_get_ultimate_parent(cable->attached_unit_index)) &&
		distance_squared3d(&pulled_origin, &collision.point) > SHADOWRUN_CABLE_CUT_DISTANCE_SQUARED)
	{
		shadowrun_cable_stop(unit_index, definition->snap_distance);
	}

	return;
}

/* ---------- public code */

void shadowrun_cable_initialize_for_new_map(
	void)
{
	long index;

	for (index = 0; index < MAXIMUM_TRACKED_OBJECTS; index++)
	{
		shadowrun_cable_clear(&shadowrun_cables[index]);
	}

	return;
}

void shadowrun_cable_new(
	long unit_index)
{
	struct shadowrun_cable *cable = shadowrun_cable_get(unit_index);

	if (cable)
	{
		shadowrun_cable_clear(cable);
		object_get_origin(unit_index, &cable->last_position);
	}

	return;
}

void shadowrun_cable_delete(
	long unit_index)
{
	struct shadowrun_cable *cable = shadowrun_cable_get(unit_index);

	if (cable)
	{
		shadowrun_cable_stop(unit_index, 1.f);
		shadowrun_cable_stop_effect(cable);
		shadowrun_cable_delete_object(&cable->hook_index);
		shadowrun_cable_delete_object(&cable->line_index);
		shadowrun_cable_clear(cable);
	}

	return;
}

/* FUN_0027D4E0's case 5 (FUN_0025B5E0): the cable's tech taken up */
void shadowrun_cable_begin(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct shadowrun_cable *cable = shadowrun_cable_get(unit_index);

	if (shadowrun_unit && cable && !shadowrun_unit->cable_tech)
	{
		shadowrun_unit->cable_tech = TRUE;
		cable->state = _shadowrun_cable_state_none;
	}

	return;
}

/* FUN_0027D760's case 4 (FUN_0025B610): let go */
void shadowrun_cable_end(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit && shadowrun_unit->cable_tech && shadowrun_cable_get(unit_index))
	{
		shadowrun_cable_stop(unit_index, 0.f);
		shadowrun_unit->cable_tech = FALSE;
	}

	return;
}

/* the cable let go by the map, as it takes the tech away */
void shadowrun_cable_force_release(
	long unit_index)
{
	if (shadowrun_cable_get(unit_index))
	{
		shadowrun_cable_stop(unit_index, 1.f);
	}

	return;
}

/* port: whether a cable moves the unit (it is not hurt by falling: bipeds.c's
falling damage) */
boolean shadowrun_cable_holds(
	long unit_index)
{
	struct shadowrun_cable *cable = shadowrun_cable_get(unit_index);

	return cable && (cable->state == _shadowrun_cable_state_reeling || cable->state == _shadowrun_cable_state_hanging ||
		cable->state == _shadowrun_cable_state_pulled);
}

/* FUN_0025B550, FUN_0025B5A0, FUN_0025B510 (players.c's camera): whether the
unit's cable is out, or it is pulled, but not hanging, in which it is seen
from behind (shadowrun_camera.c) */
boolean shadowrun_cable_reeling(
	long unit_index)
{
	struct shadowrun_cable *cable = shadowrun_cable_get(unit_index);

	return cable && cable->state != _shadowrun_cable_state_none && cable->state != _shadowrun_cable_state_hanging &&
		object_try_and_get_and_verify_type(unit_index, _object_mask_biped);
}

/* FUN_0025B7D0: the cable fires, from the unit's body the way it aims, or,
pressed while it is out, lets go, keeping the tag's part of its velocity.
(The executable also put the glider away: this build's glider is a tech of
its own, which a unit has instead) */
boolean shadowrun_cable_fire(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct shadowrun_tech2_definition *definition = shadowrun_cable_definition(unit_index);
	struct shadowrun_cable *cable = shadowrun_cable_get(unit_index);
	struct object_datum *unit = object_try_and_get_and_verify_type(unit_index, _object_mask_biped);

	if (!shadowrun_unit || !cable || !unit || !definition)
	{
		error(_error_silent, "shadowrun cable: unit %08lx cannot fire (%s)", (unsigned long)unit_index,
			!definition ? "its srud names no tech2" : "not a biped of this map");
		return FALSE;
	}
	if (!shadowrun_unit->cable_tech || cable->fire_wait_ticks > 0)
	{
		return FALSE;
	}
	/* port: while it is out (travelling, reeling, hanging, pulling), the
	button lets it go, and only a press after that fires it again: the
	executable let go only within a second of its last pull, and fired
	again from where the unit hung after that */
	if (cable->state != _shadowrun_cable_state_none)
	{
		if (cable->state != _shadowrun_cable_state_pulled)
		{
			shadowrun_cable_stop(unit_index, definition->snap_distance);
		}
		return TRUE;
	}
	shadowrun_cable_effect(&definition->fire_effect, unit_index);
	shadowrun_cable_origin(unit_index, &cable->end);
	shadowrun_cable_loop_effect(cable, &definition->travel_effect, unit_index);
	cable->attached_unit_index = NONE;
	cable->fire_wait_ticks = SHADOWRUN_CABLE_FIRE_WAIT_TICKS;
	cable->cooldown_ticks = definition->reel_ticks;
	cable->state = _shadowrun_cable_state_fired;
	/* port: the way the unit aims (the executable's 0x204, the unit's
	aiming vector) */
	unit_get_aiming_vector(unit_index, &cable->direction);

	return TRUE;
}

/* FUN_002541D0's: each tick of a biped's physics, its cable: TRUE when the
cable moves it, its velocity and its movement (none) set, and it flies (its
fall held: the executable's in-flag 0x10) */
boolean shadowrun_cable_physics(
	long biped_index,
	real_vector3d *velocity,
	real_vector3d *movement_desired)
{
	struct shadowrun_cable *cable = shadowrun_cable_get(biped_index);
	struct shadowrun_tech2_definition *definition;
	struct object_datum *biped;
	boolean moved = FALSE;

	if (!cable || cable->state == _shadowrun_cable_state_none || !shadowrun_unit_get(biped_index))
	{
		return FALSE;
	}
	/* (the one pulled moves by the puller's tag) */
	definition = shadowrun_cable_definition(cable->state == _shadowrun_cable_state_pulled ?
		cable->attached_unit_index : biped_index);
	if (!definition)
	{
		definition = shadowrun_cable_definition(biped_index);
	}
	if (!definition)
	{
		cable->state = _shadowrun_cable_state_none;
		return FALSE;
	}
	biped = object_get(biped_index);
	if (cable->state == _shadowrun_cable_state_fired)
	{
		shadowrun_cable_travel(biped_index, cable, definition);
	}
	if (cable->state == _shadowrun_cable_state_reeling)
	{
		if (distance_squared3d(&cable->end, &biped->object.position) < SHADOWRUN_CABLE_ARRIVE_DISTANCE_SQUARED ||
			cable->stuck_ticks > SHADOWRUN_CABLE_STUCK_TICKS)
		{
			cable->state = _shadowrun_cable_state_hanging;
			shadowrun_cable_effect(&definition->player_attach_effect, biped_index);
		}
		else
		{
			real_vector3d direction;

			vector_from_points3d(&biped->object.position, &cable->end, &direction);
			normalize3d(&direction);
			scale_vector3d(&direction, definition->attach_distance / TICKS_PER_SECOND, velocity);
			*movement_desired = *global_zero_vector3d;
			moved = TRUE;
		}
	}
	if (cable->state == _shadowrun_cable_state_pulling)
	{
		shadowrun_cable_pull(biped_index, cable, definition);
	}
	if (cable->state == _shadowrun_cable_state_pulled)
	{
		real speed = TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ?
			definition->undead_heal_scale_2 : definition->undead_heal_scale;

		cable->attached_ticks++;
		scale_vector3d(&cable->direction, speed / TICKS_PER_SECOND, velocity);
		*movement_desired = *global_zero_vector3d;
		moved = TRUE;
		if (!TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
			cable->attached_ticks >= definition->undead_heal_points)
		{
			shadowrun_cable_stop(biped_index, definition->snap_distance);
		}
	}
	if (cable->state == _shadowrun_cable_state_hanging)
	{
		struct collision_result collision;
		real_point3d above = biped->object.position;
		real_point3d below = biped->object.position;

		*velocity = *global_zero_vector3d;
		*movement_desired = *global_zero_vector3d;
		moved = TRUE;
		above.z += SHADOWRUN_CABLE_GROUND_DISTANCE;
		below.z -= SHADOWRUN_CABLE_GROUND_DISTANCE;
		if (collision_test_line(_collision_test_for_projectiles_flags, &above, &below, biped_index, &collision))
		{
			shadowrun_cable_effect(&definition->player_release_effect, biped_index);
			cable->state = _shadowrun_cable_state_none;
			moved = FALSE;
		}
	}

	return moved;
}

/* each tick of a unit (the biped update's waits, and FUN_00251840's
drawing): the hook on the cable's end, and the line from it to the unit,
the tag's projectile made again at the end, moving to the unit's body in
the tick */
boolean shadowrun_cable_update(
	long unit_index)
{
	struct shadowrun_cable *cable = shadowrun_cable_get(unit_index);
	struct object_datum *unit = object_try_and_get_and_verify_type(unit_index, _object_mask_biped);
	struct shadowrun_tech2_definition *definition;
	real_point3d origin;
	real_vector3d up;

	if (!cable || !unit || !shadowrun_unit_get(unit_index))
	{
		return FALSE;
	}
	definition = shadowrun_cable_definition(unit_index);

	/* (the waits: the second one is held while the cable is out, and its
	end plays the tag's effect) */
	if (cable->fire_wait_ticks > 0)
	{
		cable->fire_wait_ticks--;
	}
	if (definition && (cable->state == _shadowrun_cable_state_fired ||
		cable->state == _shadowrun_cable_state_reeling || cable->state == _shadowrun_cable_state_pulling))
	{
		cable->cooldown_ticks = definition->reel_ticks;
	}
	if (cable->cooldown_ticks > 0 && --cable->cooldown_ticks < 1 && definition)
	{
		shadowrun_cable_effect(&definition->undead_heal_effect_2, unit_index);
	}
	cable->stuck_ticks = distance_squared3d(&cable->last_position, &unit->object.position) <
		SHADOWRUN_CABLE_STUCK_DISTANCE_SQUARED ? cable->stuck_ticks + 1 : 0;
	cable->last_position = unit->object.position;

	/* port: the hook and the line the game deleted itself (the line, a
	projectile, detonates; a switch of the map's BSPs deletes them) are
	forgotten, not used: their indices are another object's, or none */
	if (cable->hook_index != NONE && !object_try_and_get_and_verify_type(cable->hook_index, NONE))
	{
		cable->hook_index = NONE;
	}
	if (cable->line_index != NONE && !object_try_and_get_and_verify_type(cable->line_index, NONE))
	{
		cable->line_index = NONE;
	}
	/* (FUN_00251840) */
	if (cable->state == _shadowrun_cable_state_none || cable->state == _shadowrun_cable_state_hanging)
	{
		shadowrun_cable_stop_effect(cable);
	}
	shadowrun_cable_delete_object(&cable->line_index);
	if (!definition || cable->state == _shadowrun_cable_state_none || cable->state == _shadowrun_cable_state_pulled)
	{
		shadowrun_cable_delete_object(&cable->hook_index);
		return cable->state != _shadowrun_cable_state_none;
	}
	shadowrun_cable_origin(unit_index, &origin);
	normalize3d(&cable->direction);
	perpendicular3d(&cable->direction, &up);
	normalize3d(&up);
	if (cable->hook_index == NONE && shadowrun_cable_tag(definition->scenery.index, SHADOWRUN_SCENERY_TAG))
	{
		struct object_placement_data placement;

		object_placement_data_new(&placement, definition->scenery.index, unit_index);
		placement.position = cable->end;
		placement.forward = cable->direction;
		placement.up = up;
		cable->hook_index = object_new(&placement);
	}
	if (cable->hook_index != NONE)
	{
		object_set_position(cable->hook_index, &cable->end, &cable->direction, &up);
	}
	if ((cable->state == _shadowrun_cable_state_fired || cable->state == _shadowrun_cable_state_reeling ||
		cable->state == _shadowrun_cable_state_pulling) &&
		shadowrun_cable_tag(definition->projectile.index, PROJECTILE_DEFINITION_TAG))
	{
		struct object_placement_data placement;
		struct unit_datum *holder = unit_try_and_get(unit_index);

		object_placement_data_new(&placement, definition->projectile.index, NONE);
		placement.position = cable->end;
		placement.forward = cable->direction;
		placement.up = up;
		vector_from_points3d(&cable->end, &origin, &placement.translational_velocity);
		cable->line_index = object_new(&placement);
		/* (in its player's colour) */
		if (cable->line_index != NONE && holder && holder->unit.player_index != NONE)
		{
			struct object_datum *line = object_get(cable->line_index);
			real_rgb_color color;
			short index;

			game_engine_player_get_change_color(&color, holder->unit.player_index);
			for (index = 0; index < NUMBER_OF_OBJECT_CHANGE_COLORS; index++)
			{
				line->object.outgoing_change_colors[index] = color;
			}
		}
	}

	return TRUE;
}
