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
#include "ai/actors.h"
#include "tag_files/tag_files.h"
#include "items/projectile_definitions.h"
#include "objects/object_definitions.h"
#include "math/real_math.h"
#include "game/game.h"
#include "interface/hud_definitions.h"
#include "game/game_allegiance.h"
#include "game/players.h"
#include "interface/hud.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "units/units.h"
#include "shadowrun.h"
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

/* ---------- globals */

/* the biped definitions gecko has made climb anything, and whether they did
before (they are the map's tags: every biped of one shares its flag) */
struct shadowrun_gecko_held_definition
{
	long definition_index;
	boolean climbed;
};

static struct shadowrun_gecko_held_definition shadowrun_gecko_held[MAXIMUM_SHADOWRUN_GECKO_DEFINITIONS];
static short shadowrun_gecko_held_count;

/* the holograms standing (a tech's actors, of the map), each with the unit
that made it and the time it goes, and how many */
struct shadowrun_hologram
{
	long actor_index;
	long owner_unit_index;
	long end_time;
	/* the actor's own orders before the port's: a hologram plays at fighting,
	and deals no damage (shadowrun_hologram_active) */
	boolean playfighting;
};

static struct shadowrun_hologram shadowrun_holograms[MAXIMUM_SHADOWRUN_HOLOGRAMS];
static short shadowrun_hologram_count;

/* ---------- private prototypes */

static void shadowrun_gecko_begin(
	long unit_index);
static long shadowrun_glider_begin(
	long unit_index);
static void shadowrun_glider_end(
	long unit_index);
static void shadowrun_gecko_end(
	long unit_index);
static void shadowrun_holograms_clear(
	long unit_index);
static void shadowrun_holograms_update(
	void);
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

	shadowrun_holograms_update();

	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(iterator.index);

		/* (a grapple's pull moves the unit whether or not its tech's time
		has run: the projectile was fired, and it is being reeled in) */
		shadowrun_grapple_update(iterator.index);
		shadowrun_glider_update(iterator.index);

		if (!shadowrun_unit || shadowrun_unit->tech_ticks <= 0)
		{
			continue;
		}
		if (--shadowrun_unit->tech_ticks <= 0)
		{
			shadowrun_vision_clear(iterator.index);
			shadowrun_gecko_end(iterator.index);
			shadowrun_holograms_clear(iterator.index);
			shadowrun_glider_end(iterator.index);
		}
		/* port: each power's own wait, as the magic's (shadowrun_units.c):
		the techs' are the waits between holograms and between grapples */
		if (shadowrun_unit->power_cooldown_ticks[_shadowrun_tech_hologram_id] > 0)
		{
			shadowrun_unit->power_cooldown_ticks[_shadowrun_tech_hologram_id]--;
		}
		if (shadowrun_unit->power_cooldown_ticks[_shadowrun_tech_grapple_id] > 0)
		{
			shadowrun_unit->power_cooldown_ticks[_shadowrun_tech_grapple_id]--;
		}
		if (shadowrun_unit->tech == _shadowrun_tech_vision_id)
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
			/* (the engine's climbing reads the biped's definition, not the unit's
			own flags: bipeds.c tests definition->biped.flags) */
			shadowrun_gecko_begin(unit_index);
			shadowrun_unit->tech_ticks = SHADOWRUN_GECKO_TICKS;
			return TRUE;
		}

	case _shadowrun_tech_reflexes_id:
		/* port: reflexes: for a while, the unit moves and reloads faster, and
		jumps a little higher (its jump, movement and reload each ask
		shadowrun_reflexes_active) */
		shadowrun_unit->tech_ticks = SHADOWRUN_REFLEXES_TICKS;
		return TRUE;

	case _shadowrun_tech_glider_id:
		/* port: glider, worn on the back: it is taken out, put on, and worn
		while it is the unit's; its jump is higher, and it glides */
		shadowrun_glider_end(unit_index);
		if (shadowrun_glider_begin(unit_index) == NONE)
		{
			return FALSE;
		}
		shadowrun_unit->tech_ticks = SHADOWRUN_GLIDER_WEAR_TICKS;
		return TRUE;

	case _shadowrun_tech_grapple_id:
		/* port: grapple, which shoots the map's grapple projectile where the
		caster aims; where it lands, the caster is pulled to it
		(shadowrun_magic.c, shadowrun_projectile_detonated) */
		{
			struct unit_datum *unit = unit_get(unit_index);
			real_vector3d vector;
			real_point3d target;
			struct collision_result collision;
			long projectile_definition_index = shadowrun_grapple_projectile();

			/* (no grapple until the wait after the last is down) */
			if (shadowrun_unit->power_cooldown_ticks[_shadowrun_tech_grapple_id] > 0 ||
				projectile_definition_index == NONE)
			{
				return FALSE;
			}
			unit_get_head_position(unit_index, &target);
			point_from_line3d(&target, &unit->unit.aiming_vector, SHADOWRUN_GRAPPLE_ORIGIN_OFFSET, &target);
			scale_vector3d(&unit->unit.aiming_vector, SHADOWRUN_GRAPPLE_RANGE, &vector);
			if (collision_test_vector(_collision_test_for_projectiles_flags, &target, &vector, unit_index, &collision))
			{
				target = collision.point;
			}
			else
			{
				add_vectors3d((real_vector3d const *)&target, &vector, (real_vector3d *)&target);
			}
			if (shadowrun_grapple_fire(unit_index, projectile_definition_index) == NONE)
			{
				return FALSE;
			}
			shadowrun_unit->power_cooldown_ticks[_shadowrun_tech_grapple_id] = SHADOWRUN_GRAPPLE_COOLDOWN_TICKS;
			return TRUE;
		}

	case _shadowrun_tech_hologram_id:
		/* port: hologram, which makes an actor of the map's hologram variant,
		where the caster aims (as summon elemental does with its elemental).
		The one it made before goes as the new one comes, and none is made
		until the wait after the last is down */
		{
			struct unit_datum *unit = unit_get(unit_index);
			real_vector3d vector;
			real_point3d target;
			struct collision_result collision;

			if (shadowrun_unit->power_cooldown_ticks[_shadowrun_tech_hologram_id] > 0)
			{
				return FALSE;
			}
			unit_get_head_position(unit_index, &target);
			point_from_line3d(&target, &unit->unit.aiming_vector, SHADOWRUN_HOLOGRAM_ORIGIN_OFFSET, &target);
			scale_vector3d(&unit->unit.aiming_vector, SHADOWRUN_HOLOGRAM_RANGE, &vector);
			if (collision_test_vector(_collision_test_for_projectiles_flags, &target, &vector, unit_index, &collision))
			{
				target = collision.point;
			}
			else
			{
				add_vectors3d((real_vector3d const *)&target, &vector, (real_vector3d *)&target);
			}
			{
				long actor_index = shadowrun_hologram_place(unit_index, &target);

				if (actor_index == NONE)
				{
					return FALSE;
				}
				/* (the one before it: one a unit has at once) */
				shadowrun_holograms_clear(unit_index);
				/* (it lasts as long as the tech does, then goes) */
				if (shadowrun_hologram_count < MAXIMUM_SHADOWRUN_HOLOGRAMS)
				{
					struct shadowrun_hologram *hologram = &shadowrun_holograms[shadowrun_hologram_count++];

					hologram->actor_index = actor_index;
					hologram->owner_unit_index = unit_index;
					hologram->end_time = game_time_get() + SHADOWRUN_HOLOGRAM_TICKS;
				}
			}
			shadowrun_unit->tech_ticks = SHADOWRUN_HOLOGRAM_TICKS;
			/* the wait after it, as a spell's own */
			shadowrun_unit->power_cooldown_ticks[_shadowrun_tech_hologram_id] = SHADOWRUN_HOLOGRAM_COOLDOWN_TICKS;
			return TRUE;
		}
	}

	return FALSE;
}

/* ---------- reflexes ---------- */

/* whether its time runs: the unit's tech is reflexes, and its ticks are not
up (the tech's own time, as vision's and gecko's) */
boolean shadowrun_reflexes_active(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	return shadowrun_unit && shadowrun_unit->tech == _shadowrun_tech_reflexes_id &&
		shadowrun_unit->tech_ticks > 0;
}

real shadowrun_reflexes_jump_scale(
	long unit_index)
{
	return shadowrun_reflexes_active(unit_index) ? SHADOWRUN_REFLEXES_JUMP_SCALE : 1.f;
}

real shadowrun_reflexes_movement_scale(
	long unit_index)
{
	return shadowrun_reflexes_active(unit_index) ? SHADOWRUN_REFLEXES_MOVEMENT_SCALE : 1.f;
}

real shadowrun_reflexes_reload_scale(
	long unit_index)
{
	return shadowrun_reflexes_active(unit_index) ? SHADOWRUN_REFLEXES_RELOAD_SCALE : 1.f;
}

/* ---------- the glider ---------- */

/* the map's glider scenery, looked for once a map (its 'scen': the map also
has a 'mode' of the name) */
static long shadowrun_glider_scenery_index;

static long shadowrun_glider_scenery(
	void)
{
	if (shadowrun_glider_scenery_index == NONE)
	{
		shadowrun_glider_scenery_index = tag_loaded(SHADOWRUN_SCENERY_TAG, SHADOWRUN_GLIDER_SCENERY);
		if (shadowrun_glider_scenery_index == NONE)
		{
			error(_error_silent, "shadowrun: the map has no %s", SHADOWRUN_GLIDER_SCENERY);
		}
	}

	return shadowrun_glider_scenery_index;
}

/* whether the unit wears it: an object on its back (the unit's own, attached
rather than in the world: shadowrun_glider_begin) */
boolean shadowrun_glider_worn(
	long unit_index)
{
	struct object_iterator iterator;
	struct object_datum *object;
	boolean worn = FALSE;

	object_iterator_new(&iterator, _object_mask_scenery, 0);
	while (!worn && (object = object_iterator_next(&iterator)) != NULL)
	{
		worn = object->object.parent_object_index == unit_index;
	}

	return worn;
}

/* how much the unit's jump is scaled: the glider's, or 1 (none) */
real shadowrun_glider_jump_scale(
	long unit_index)
{
	return shadowrun_glider_worn(unit_index) ? SHADOWRUN_GLIDER_JUMP_SCALE : 1.f;
}

/* the glider put on the unit's back: a scenery of the map's, attached at its
back marker (a biped has none: at its body's, behind it) */
static long shadowrun_glider_begin(
	long unit_index)
{
	long definition_index = shadowrun_glider_scenery();
	struct object_placement_data placement;
	long scenery_index;

	if (definition_index == NONE)
	{
		return NONE;
	}
	object_placement_data_new(&placement, definition_index, unit_index);
	object_get_origin(unit_index, &placement.position);
	placement.forward = object_get(unit_index)->object.forward;
	placement.up = object_get(unit_index)->object.up;
	scenery_index = object_new(&placement);
	if (scenery_index != NONE)
	{
		if (object_get_marker_by_name(unit_index, "back", NULL, 0) > 0)
		{
			object_attach_to_marker(unit_index, "back", scenery_index, NULL);
		}
		else
		{
			object_attach_to_marker(unit_index, "body", scenery_index, NULL);
		}
	}

	return scenery_index;
}

/* it taken off, and deleted */
static void shadowrun_glider_end(
	long unit_index)
{
	struct object_iterator iterator;
	struct object_datum *object;

	object_iterator_new(&iterator, _object_mask_scenery, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		if (object->object.parent_object_index == unit_index)
		{
			object_delete(iterator.index);
		}
	}

	return;
}

/* while it is worn and the unit is in the air, it glides: its fall slows
toward a gentle one, and it moves a little further forward (a short glide) */
void shadowrun_glider_update(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct biped_datum *biped = biped_try_and_get(unit_index);

	if (!shadowrun_unit || !biped || !shadowrun_glider_worn(unit_index))
	{
		return;
	}
	if (TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
	{
		real downward = dot_product3d(&biped->object.translational_velocity, &biped->object.up);

		if (downward < -SHADOWRUN_GLIDER_FALL_SPEED)
		{
			real_vector3d correction;

			/* (the fall taken back toward the glider's speed, a tenth a tick) */
			scale_vector3d(&biped->object.up, -SHADOWRUN_GLIDER_FALL_SPEED, &correction);
			add_vectors3d(&biped->object.translational_velocity, &correction, &correction);
			scale_vector3d(&correction, 0.35f, &correction);
			add_vectors3d(&biped->object.translational_velocity, &correction, &biped->object.translational_velocity);
			biped->biped.airborne_ticks = MIN(biped->biped.airborne_ticks, SHADOWRUN_GLIDER_TICKS);
		}
	}

	return;
}

/* ---------- grapple ---------- */

/* the map's grapple projectile, looked for once a map */
static long shadowrun_grapple_projectile_index;

long shadowrun_grapple_projectile(
	void)
{
	if (shadowrun_grapple_projectile_index == NONE)
	{
		shadowrun_grapple_projectile_index = tag_loaded(PROJECTILE_DEFINITION_TAG, SHADOWRUN_GRAPPLE_PROJECTILE);
		if (shadowrun_grapple_projectile_index == NONE)
		{
			error(_error_silent, "shadowrun: the map has no %s", SHADOWRUN_GRAPPLE_PROJECTILE);
		}
	}

	return shadowrun_grapple_projectile_index;
}

/* the projectile fired from the unit's body the way it aims */
long shadowrun_grapple_fire(
	long unit_index,
	long projectile_definition_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct object_placement_data placement;
	struct object_marker markers[1];
	real_point3d origin;
	real_vector3d forward;
	long projectile_index;

	if (!unit || projectile_definition_index == NONE)
	{
		return NONE;
	}
	if (object_get_marker_by_name(unit_index, "body", markers, NUMBEROF(markers)) > 0)
	{
		origin = markers[0].matrix.position;
	}
	else
	{
		origin = unit->object.position;
	}
	unit_get_aiming_vector(unit_index, &forward);
	object_placement_data_new(&placement, projectile_definition_index, unit_index);
	placement.position = origin;
	placement.forward = forward;
	perpendicular3d(&forward, &placement.up);
	normalize3d(&placement.up);
	placement.translational_velocity = *global_zero_vector3d;
	projectile_index = object_new(&placement);

	return projectile_index;
}

/* whether the projectile is a grapple's: one of its definition, fired from a
unit (the map's own projectiles are not) */
boolean shadowrun_grapple_projectile_is(
	long projectile_index)
{
	struct object_datum *projectile = object_try_and_get_and_verify_type(projectile_index, _object_mask_projectile);

	return projectile && shadowrun_grapple_projectile_index != NONE &&
		projectile->definition_index == shadowrun_grapple_projectile_index;
}

/* the projectile's caster pulled to where it landed, over the pull's ticks
(shadowrun_grapple_update moves it, as a teleport's moves it) */
void shadowrun_grapple_pull(
	long projectile_index,
	real_point3d const *position)
{
	struct object_datum *projectile = object_try_and_get_and_verify_type(projectile_index, _object_mask_projectile);
	long unit_index = projectile ? projectile->object.owner_object_index : NONE;
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit && unit_try_and_get(unit_index) && biped_try_and_get(unit_index))
	{
		shadowrun_unit->grapple_origin = object_get(unit_index)->object.position;
		shadowrun_unit->grapple_target = *position;
		shadowrun_unit->grapple_pull_ticks = SHADOWRUN_GRAPPLE_PULL_TICKS;
		shadowrun_unit->grapple_ticks = SHADOWRUN_GRAPPLE_PULL_TICKS;
	}

	return;
}

/* each tick of a pulled unit: it moves a part of the way to where the pull
takes it, its share of the ticks, and is there at the last */
void shadowrun_grapple_update(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct unit_datum *unit;
	real fraction;
	real_point3d from;
	real_point3d to;

	if (!shadowrun_unit || shadowrun_unit->grapple_ticks <= 0)
	{
		return;
	}
	unit = unit_get(unit_index);
	from = shadowrun_unit->grapple_origin;
	to = shadowrun_unit->grapple_target;
	fraction = (real)(shadowrun_unit->grapple_pull_ticks - shadowrun_unit->grapple_ticks + 1) /
		(real)shadowrun_unit->grapple_pull_ticks;
	if (fraction > 1.f)
	{
		fraction = 1.f;
	}
	{
		real_vector3d vector;
		real_point3d position;

		vector_from_points3d(&from, &to, &vector);
		scale_vector3d(&vector, fraction, &vector);
		add_vectors3d((real_vector3d const *)&from, &vector, (real_vector3d *)&position);
		object_set_position(unit_index, &position, NULL, NULL);
	}
	shadowrun_unit->grapple_ticks--;
	if (shadowrun_unit->grapple_ticks == 0)
	{
		/* (at the end it is out of any wall it ended in, or back where it
		started if it cannot be) */
		if (!object_force_inside_bsp(unit_index, &from))
		{
			object_set_position(unit_index, &from, NULL, NULL);
		}
	}

	return;
}

/* port: the controls a local player's unit was given this tick
(players.c), which the game's own control flags cannot carry the left
bumper's tech in: it is used here, where the unit's controls are its own */
void shadowrun_player_controls(
	long unit_index,
	unsigned long flags)
{
	if (UNIT_CONTROL_PORT_TECH_BIT < 32 && (flags & FLAG(UNIT_CONTROL_PORT_TECH_BIT)) &&
		shadowrun_map_loaded())
	{
		shadowrun_tech_cast(unit_index);
	}

	return;
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
		shadowrun_holograms_clear(unit_index);
		shadowrun_glider_end(unit_index);
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

/* port: whether the actor is a hologram's (a tech's, shadowrun_tech.c): its
attacks do no damage (ai_adjust_damage, the zero below) */
boolean shadowrun_hologram_active(
	long actor_index)
{
	short index;

	for (index = 0; index < shadowrun_hologram_count; index++)
	{
		if (shadowrun_holograms[index].actor_index == actor_index)
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* the holograms whose time is up, or whose caster is gone, go: killed (a
summon's actors are) and forgotten */
static void shadowrun_holograms_update(
	void)
{
	long now = game_time_get();
	short index;

	for (index = shadowrun_hologram_count - 1; index >= 0; index--)
	{
		struct shadowrun_hologram *hologram = &shadowrun_holograms[index];
		boolean gone = now >= hologram->end_time || !actor_try_and_get(hologram->actor_index);

		/* (a caster dead for good leaves no unit: its holograms go with it) */
		if (!gone && !object_try_and_get_and_verify_type(hologram->owner_unit_index, NONE))
		{
			gone = TRUE;
		}
		if (gone)
		{
			if (actor_try_and_get(hologram->actor_index))
			{
				actor_kill(hologram->actor_index, TRUE, FALSE);
			}
			shadowrun_holograms[index] = shadowrun_holograms[--shadowrun_hologram_count];
		}
	}

	return;
}

/* the holograms a unit made taken away, at once */
static void shadowrun_holograms_clear(
	long unit_index)
{
	short index;

	for (index = shadowrun_hologram_count - 1; index >= 0; index--)
	{
		struct shadowrun_hologram *hologram = &shadowrun_holograms[index];

		if (hologram->owner_unit_index == unit_index)
		{
			if (actor_try_and_get(hologram->actor_index))
			{
				actor_kill(hologram->actor_index, TRUE, FALSE);
			}
			shadowrun_holograms[index] = shadowrun_holograms[--shadowrun_hologram_count];
		}
	}

	return;
}

/* Gecko's flag is on the biped's definition, which every biped of that tag
shares: it is set on, and put back as it was at the end. What was set is
kept by the definition's tag, so a unit that ends while another is geckoing
does not take the flag off the other. */
static void shadowrun_gecko_begin(
	long unit_index)
{
	struct biped_datum *biped = biped_try_and_get(unit_index);
	struct biped_definition *definition;
	long definition_index;
	short index;

	if (!biped)
	{
		return;
	}
	definition_index = biped->definition_index;
	definition = biped_definition_get(definition_index);
	if (!definition)
	{
		return;
	}
	for (index = 0; index < shadowrun_gecko_held_count; index++)
	{
		if (shadowrun_gecko_held[index].definition_index == definition_index)
		{
			/* (already on for another: the first one's state is what goes back) */
			return;
		}
	}
	if (shadowrun_gecko_held_count < MAXIMUM_SHADOWRUN_GECKO_DEFINITIONS)
	{
		shadowrun_gecko_held[shadowrun_gecko_held_count].definition_index = definition_index;
		shadowrun_gecko_held[shadowrun_gecko_held_count].climbed =
			TEST_FLAG(definition->biped.flags, _biped_climbs_anything_bit) ? TRUE : FALSE;
		shadowrun_gecko_held_count++;
	}
	SET_FLAG(definition->biped.flags, _biped_climbs_anything_bit, TRUE);

	return;
}

/* Gecko's time up: the definition's flag goes back to what it was, unless
another unit of it is still geckoing */
static void shadowrun_gecko_end(
	long unit_index)
{
	struct biped_datum *biped = biped_try_and_get(unit_index);
	struct object_iterator iterator;
	struct object_datum *object;
	long definition_index;
	short held;

	if (!biped)
	{
		return;
	}
	definition_index = biped->definition_index;
	for (held = 0; held < shadowrun_gecko_held_count; held++)
	{
		if (shadowrun_gecko_held[held].definition_index == definition_index)
		{
			break;
		}
	}
	if (held == shadowrun_gecko_held_count)
	{
		return;
	}
	/* (another unit of the definition still geckoing keeps it on) */
	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		struct shadowrun_unit *other = shadowrun_unit_get(iterator.index);
		struct biped_datum *other_biped = biped_try_and_get(iterator.index);

		if (iterator.index != unit_index && other && other_biped && other->tech_ticks > 0 &&
			other->tech == _shadowrun_tech_gecko_id && other_biped->definition_index == definition_index)
		{
			return;
		}
	}
	{
		struct biped_definition *definition = biped_definition_get(definition_index);

		if (definition)
		{
			SET_FLAG(definition->biped.flags, _biped_climbs_anything_bit,
				shadowrun_gecko_held[held].climbed);
		}
	}
	shadowrun_gecko_held[held] = shadowrun_gecko_held[--shadowrun_gecko_held_count];

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
