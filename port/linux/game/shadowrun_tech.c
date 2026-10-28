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
#include "cache/cache_files.h" /* port: the tag iterator (the techs' tags) */
#include "tag_files/tag_files.h"
#include "items/projectile_definitions.h"
#include "objects/damage.h"
#include "objects/object_definitions.h"
#include "math/real_math.h"
#include "game/game.h"
#include "game/game_globals.h"
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
#include "shadowrun_definitions.h"
#include "shadowrun_units.h"
#include "shadowrun_tech.h"
#include "shadowrun_cable.h"

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
static void shadowrun_hologram_remove(
	long *actor_index);
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
	shadowrun_antimagic_update();

	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(iterator.index);

		/* (the cable runs whether or not its tech's time has run: the bolt
		was fired, and the unit is being reeled in) */
		shadowrun_cable_update(iterator.index);

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
			/* (what the one before it left: its marks, its climb, its holograms,
			its glider, its cable) */
			shadowrun_vision_clear(unit_index);
			shadowrun_gecko_end(unit_index);
			shadowrun_holograms_clear(unit_index);
			shadowrun_glider_end(unit_index);
			shadowrun_unit->tech_ticks = 0;
		}
		/* (the cable's own: taken up by the grapple's tech, let go by
		another, as FUN_0027D4E0 and FUN_0027D760 do) */
		if (tech == _shadowrun_tech_grapple_id)
		{
			shadowrun_cable_begin(unit_index);
		}
		else
		{
			shadowrun_cable_end(unit_index);
		}
		shadowrun_unit->tech = tech;
	}

	return;
}

short shadowrun_unit_tech(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	return shadowrun_unit ? (short)shadowrun_unit->tech : _shadowrun_tech_none;
}

/* the left bumper's: the unit's tech does what it does. TRUE when one ran */
boolean shadowrun_tech_cast(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	/* (reflexes is 0, so a unit's tech is _shadowrun_tech_none until it has
	one: 0 is a tech's own) */
	if (!shadowrun_unit || shadowrun_unit->tech == _shadowrun_tech_none)
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

	case _shadowrun_tech_grapple_id:
		/* port: the cable's grapple, which fires along the unit's forward and
		reels it in where it hits (bipeds.c FUN_0025B7D0, FUN_002541D0:
		port/linux/game/shadowrun_cable.c) */
		return shadowrun_cable_fire(unit_index);

	case _shadowrun_tech_glider_id:
		/* port: glider, a toggle (units.c 0x251EE0): out, the scenery is put on
		its back and it climbs (its fall held off); pressed again, it goes */
		if (shadowrun_glider_worn(unit_index))
		{
			shadowrun_glider_end(unit_index);
			return TRUE;
		}
		shadowrun_glider_begin(unit_index);
		shadowrun_unit->tech_ticks = SHADOWRUN_GLIDER_WEAR_TICKS;
		return TRUE;

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

/* ---------- anti-magic ---------- */

/* a zone standing: where it is, its team's (the thrower's), the ticks left
and all it had, its start radius (ticks x 0.75 is the radius each: the
executable's, FUN_0026FCA0), its reach, and the tag it works from */
struct shadowrun_antimagic_zone
{
	real_point3d position;
	short team_index;
	short pad;
	long ticks_left;
	long ticks;
	real radius;
	real reach;
	long definition_index;
	/* the ball standing in it (a scenery of the map's), or NONE */
	long object_index;
	/* the units inside it as last taken (of its enemies): those that leave it
	lose their anti-magic */
	long inside[MAXIMUM_SHADOWRUN_ANTIMAGIC_INSIDE];
	short inside_count;
};

static struct shadowrun_antimagic_zone shadowrun_antimagic_zones[MAXIMUM_SHADOWRUN_ANTIMAGIC_ZONES];
static short shadowrun_antimagic_zone_count;

/* the map's t3ed (anti-magic's tech tag), looked for once a map */
static long shadowrun_antimagic_definition_index = NONE;

/* the map's t3ed: the Shadowrun unit's tech3, of the unit that threw it (the
anti-magic grenade is a map's projectile, which has no unit's: the first t3ed
the map has) */
static struct shadowrun_tech3_definition *shadowrun_antimagic_definition(
	void)
{
	struct tag_iterator iterator;
	long definition_index = NONE;

	if (shadowrun_antimagic_definition_index >= 0)
	{
		definition_index = shadowrun_antimagic_definition_index;
	}
	else if (shadowrun_antimagic_definition_index != NONE)
	{
		return NULL;
	}
	else
	{
		tag_iterator_new(&iterator, SHADOWRUN_TECH3_DEFINITION_TAG);
		definition_index = tag_iterator_next(&iterator);
		if (definition_index == NONE)
		{
			error(_error_silent, "shadowrun: the map has no %s", SHADOWRUN_ANTIMAGIC_TECH_NAME);
			/* (a value no tag index is: looked for once a map) */
			shadowrun_antimagic_definition_index = -2;
			return NULL;
		}
		shadowrun_antimagic_definition_index = definition_index;
	}

	return (struct shadowrun_tech3_definition *)tag_get(SHADOWRUN_TECH3_DEFINITION_TAG, definition_index);
}

/* port: the map's second grenade (the second type of grenade a unit has, the
Covenant's plasma: units.h) made the anti-magic grenade: its projectile is the
map's anti-magic grenade, and its count the unit's own. A unit carrying two
throws it as it throws any grenade (the left trigger) */
void shadowrun_antimagic_initialize_for_new_map(
	void)
{
	struct game_globals_grenade *grenade;

	shadowrun_antimagic_zone_count = 0;
	shadowrun_antimagic_definition_index = NONE;
	if (!shadowrun_map_loaded())
	{
		return;
	}
	if (SHADOWRUN_ANTIMAGIC_GRENADE_TYPE >= scenario_get_game_globals()->grenades.count)
	{
		return;
	}
	grenade = TAG_BLOCK_GET_ELEMENT(&scenario_get_game_globals()->grenades,
		SHADOWRUN_ANTIMAGIC_GRENADE_TYPE, struct game_globals_grenade);
	grenade->projectile.index = shadowrun_antimagic_grenade_projectile();

	return;
}

/* whether the unit stands inside a zone of its enemies: inside when within the
zone's radius, which shrinks as its ticks run out (0.75 a tick, the
executable's), the way FUN_0026FCA0 asks it */
boolean shadowrun_antimagic_at(
	long unit_index)
{
	struct object_datum *object = object_try_and_get_and_verify_type(unit_index, NONE);
	short index;

	if (!object)
	{
		return FALSE;
	}
	for (index = 0; index < shadowrun_antimagic_zone_count; index++)
	{
		struct shadowrun_antimagic_zone *zone = &shadowrun_antimagic_zones[index];
		real radius = zone->radius - (real)(zone->ticks - zone->ticks_left) * 0.75f;
		real_vector3d offset;

		if (radius <= 0.f)
		{
			continue;
		}
		vector_from_points3d(&zone->position, &object->object.position, &offset);
		if (magnitude3d(&offset) <= radius &&
			game_team_is_enemy((short)object->object.owner_team_index, zone->team_index))
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* the unit's spells fail inside an enemy's zone */
boolean shadowrun_spells_blocked(
	long unit_index)
{
	return shadowrun_antimagic_at(unit_index);
}

/* the unit's spell is cast, unless it is inside an enemy's anti-magic */
boolean shadowrun_unit_can_cast(
	long unit_index)
{
	return !shadowrun_spells_blocked(unit_index);
}

/* a zone made where the anti-magic grenade detonated, on its thrower's side
(the executable's askers test a unit's team against its own) */
void shadowrun_antimagic_zone(
	long owner_object_index,
	real_point3d const *position)
{
	struct object_datum *owner = object_try_and_get_and_verify_type(owner_object_index, NONE);
	struct shadowrun_antimagic_zone *zone;
	struct shadowrun_tech3_definition *definition;

	definition = shadowrun_antimagic_definition();
	if (!definition || !owner)
	{
		return;
	}
	if (shadowrun_antimagic_zone_count >= MAXIMUM_SHADOWRUN_ANTIMAGIC_ZONES)
	{
		/* (the oldest goes for the new: the list is in the order they came) */
		csmemcpy(&shadowrun_antimagic_zones[0], &shadowrun_antimagic_zones[1],
			sizeof(shadowrun_antimagic_zones[0]) * (MAXIMUM_SHADOWRUN_ANTIMAGIC_ZONES - 1));
		shadowrun_antimagic_zone_count--;
	}
	zone = &shadowrun_antimagic_zones[shadowrun_antimagic_zone_count++];
	csmemset(zone, 0, sizeof(*zone));
	zone->position = *position;
	zone->ticks = definition->antimagic_duration_ticks;
	zone->ticks_left = zone->ticks;
	zone->radius = definition->antimagic_radius;
	zone->reach = definition->antimagic_reach;
	zone->definition_index = shadowrun_antimagic_definition_index;
	zone->team_index = (short)owner->object.owner_team_index;
	/* port: the ball it stands in (the map's antimagicball scenery), scaled to
	the zone's radius: the zone's own object, made and moved with it */
	if (definition->antimagic_ball_scenery.index != NONE)
	{
		struct object_placement_data placement;

		object_placement_data_new(&placement, definition->antimagic_ball_scenery.index, owner_object_index);
		placement.position = *position;
		placement.forward = owner->object.forward;
		placement.up = owner->object.up;
		zone->object_index = object_new(&placement);
		if (zone->object_index != NONE)
		{
			object_get(zone->object_index)->object.scale = definition->antimagic_radius;
		}
	}

	return;
}

/* port: whether a zone holds the unit: within its live radius, and an enemy
of its side (the test shadowrun_antimagic_at makes of all of them) */
static boolean shadowrun_antimagic_zone_holds(
	struct shadowrun_antimagic_zone *zone,
	long unit_index)
{
	struct object_datum *object = object_try_and_get_and_verify_type(unit_index, NONE);
	real radius = zone->radius - (real)(zone->ticks - zone->ticks_left) * 0.75f;
	real_vector3d offset;

	if (!object || radius <= 0.f)
	{
		return FALSE;
	}
	vector_from_points3d(&zone->position, &object->object.position, &offset);

	return magnitude3d(&offset) <= radius &&
		game_team_is_enemy((short)object->object.owner_team_index, zone->team_index);
}

/* port: a unit that left a zone (or was in it as the zone ended) loses its
anti-magic: the dump's "lose antimagic" damage and effect (t3ed 0x428 and
0x438), on it, aimed at the zone */
static void shadowrun_antimagic_lost(
	long unit_index,
	real_point3d const *position,
	real reach)
{
	struct shadowrun_tech3_definition *definition = shadowrun_antimagic_definition();
	struct unit_datum *unit = unit_try_and_get(unit_index);

	if (!definition || !unit)
	{
		return;
	}
	if (definition->antimagic_lost_damage.index != NONE)
	{
		struct damage_data damage;
		real_vector3d direction;

		damage_data_new(&damage, definition->antimagic_lost_damage.index);
		SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
		damage.origin = *position;
		damage.epicenter = *position;
		vector_from_points3d(position, &unit->object.position, &direction);
		normalize3d(&direction);
		damage.direction = direction;
		/* (its reach: the damage's own scale covers what the tag's does) */
		(void)reach;
		object_cause_damage(&damage, unit_index, NONE, NONE, NONE, NULL);
	}
	if (definition->antimagic_lost_effect.index != NONE)
	{
		effect_new_from_object(definition->antimagic_lost_effect.index, unit_index, unit_index, NONE, 0.f, 0.f, NULL, NULL);
	}

	return;
}

/* each tick: a zone's ticks run down, and one done goes; the units of its
enemies inside it are hurt by it (the executable's askers do, with the dump's
damage, each second) */
void shadowrun_antimagic_update(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	short index;

	for (index = shadowrun_antimagic_zone_count - 1; index >= 0; index--)
	{
		struct shadowrun_antimagic_zone *zone = &shadowrun_antimagic_zones[index];

		if (--zone->ticks_left <= 0)
		{
			/* (those still inside lose their anti-magic as it goes) */
			short inside;

			for (inside = 0; inside < zone->inside_count; inside++)
			{
				shadowrun_antimagic_lost(zone->inside[inside], &zone->position, zone->reach);
			}
			if (zone->object_index != NONE && object_try_and_get_and_verify_type(zone->object_index, NONE))
			{
				object_delete(zone->object_index);
			}
			shadowrun_antimagic_zones[index] = shadowrun_antimagic_zones[--shadowrun_antimagic_zone_count];
		}
	}
	if (game_time_get() % TICKS_PER_SECOND != 0)
	{
		return;
	}
	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long unit_index = iterator.index;

		/* port: a unit that was in a zone and is not now loses its anti-magic
		(as the "lose antimagic" the dump gives for those that leave) */
		{
			short zone_number;

			for (zone_number = 0; zone_number < shadowrun_antimagic_zone_count; zone_number++)
			{
				struct shadowrun_antimagic_zone *zone = &shadowrun_antimagic_zones[zone_number];
				boolean was_inside = FALSE;
				short inside;

				for (inside = 0; inside < zone->inside_count; inside++)
				{
					if (zone->inside[inside] == unit_index)
					{
						was_inside = TRUE;
						break;
					}
				}
				if (was_inside && !shadowrun_antimagic_zone_holds(zone, unit_index))
				{
					zone->inside[inside] = zone->inside[--zone->inside_count];
					shadowrun_antimagic_lost(unit_index, &zone->position, zone->reach);
				}
			}
		}
		if (shadowrun_antimagic_at(unit_index))
		{
			struct shadowrun_tech3_definition *definition = shadowrun_antimagic_definition();
			struct unit_datum *unit = unit_try_and_get(unit_index);
			short zone_number;

			/* port: the zone it is in remembers it, to know when it leaves */
			for (zone_number = 0; zone_number < shadowrun_antimagic_zone_count; zone_number++)
			{
				struct shadowrun_antimagic_zone *zone = &shadowrun_antimagic_zones[zone_number];
				short inside;
				boolean held = FALSE;

				for (inside = 0; inside < zone->inside_count; inside++)
				{
					if (zone->inside[inside] == unit_index)
					{
						held = TRUE;
					}
				}
				if (!held && shadowrun_antimagic_zone_holds(zone, unit_index) &&
					zone->inside_count < MAXIMUM_SHADOWRUN_ANTIMAGIC_INSIDE)
				{
					zone->inside[zone->inside_count++] = unit_index;
				}
			}

			if (definition && unit)
			{
				struct damage_data damage;

				if (definition->antimagic_damage.index != NONE)
				{
					damage_data_new(&damage, definition->antimagic_damage.index);
					SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
					damage.direction = *global_up3d;
					object_cause_damage(&damage, unit_index, NONE, NONE, NONE, NULL);
				}
				/* port: and its essence drains (the dump's essence_drain, t3ed
				0x404), the same each second */
				if (definition->antimagic_drain_damage.index != NONE)
				{
					damage_data_new(&damage, definition->antimagic_drain_damage.index);
					SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
					damage.direction = *global_up3d;
					object_cause_damage(&damage, unit_index, NONE, NONE, NONE, NULL);
				}
			}
		}
	}

	return;
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

/* the glider's scenery: the unit's t2ed's, as the executable reads it
(bipeds.c FUN_00251C00's 0x288); the map's by name where the tag names none
(its 'scen': the map also has a 'mode' of the name). NONE until looked for
(a static starts at 0, which is a tag's index) */
static long shadowrun_glider_scenery_index = NONE;

static long shadowrun_glider_scenery(
	long unit_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_get(unit_index);

	if (definition && definition->tech2.index != NONE)
	{
		struct shadowrun_tech2_definition *tech2 = tag_get(SHADOWRUN_TECH2_DEFINITION_TAG,
			definition->tech2.index);

		if (tech2 && tech2->glider_scenery.index != NONE)
		{
			return tech2->glider_scenery.index;
		}
	}
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

/* whether the unit's glider is out: its tech is the glider, and its ticks
are not up (bipeds.c's 0x1a4, the executable's scale, is 1.0 when it is not) */
boolean shadowrun_glider_worn(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	return shadowrun_unit && shadowrun_unit->tech == _shadowrun_tech_glider_id &&
		shadowrun_unit->tech_ticks > 0;
}

/* 0x251f30: the glider's flight, which turns the biped by how fast it moves
(the executable's turn rates at bipd +0x30c, this build's
moving_turning_speed): a fast glider turns, a still one barely does. The
turn step bipeds.c takes is scaled by that (shadowrun_glider_turn_scale) */
real shadowrun_glider_turn_scale(
	long unit_index)
{
	struct biped_datum *biped = biped_try_and_get(unit_index);
	struct biped_definition *definition;
	real speed;
	real maximum = 1.f;

	if (!biped || !shadowrun_glider_worn(unit_index))
	{
		return 1.f;
	}
	definition = biped_definition_get(biped->definition_index);
	if (!definition || definition->biped.moving_turning_speed <= 0.f)
	{
		return maximum;
	}
	/* (a tick's worth of turning, as the executable divides it) */
	speed = magnitude3d(&biped->object.translational_velocity) /
		(definition->biped.moving_turning_speed / TICKS_PER_SECOND);

	return PIN(speed, SHADOWRUN_GLIDER_TURN_SCALE_MINIMUM, maximum);
}

/* the glider put on the unit's back: a scenery of the map's, attached to its
back's node ("body": the glider's own model has no markers, so it is
attached by node, not by marker: object_attach_to_marker would ask it for
one, and assert) */
static long shadowrun_glider_begin(
	long unit_index)
{
	long definition_index = shadowrun_glider_scenery(unit_index);
	struct object_placement_data placement;
	long scenery_index;
	long parent_node_index = NONE;
	struct object_marker markers[1];
	char const *marker_name = NULL;

	if (definition_index == NONE)
	{
		return NONE;
	}
	/* (a biped's "back", or the nearest it has) */
	if (object_get_marker_by_name(unit_index, "back", markers, NUMBEROF(markers)) > 0)
	{
		marker_name = "back";
	}
	else if (object_get_marker_by_name(unit_index, "body", markers, NUMBEROF(markers)) > 0)
	{
		marker_name = "body";
	}
	object_placement_data_new(&placement, definition_index, unit_index);
	object_get_origin(unit_index, &placement.position);
	placement.forward = object_get(unit_index)->object.forward;
	placement.up = object_get(unit_index)->object.up;
	scenery_index = object_new(&placement);
	if (scenery_index != NONE)
	{
		if (marker_name && object_get_marker_by_name(unit_index, marker_name, markers, NUMBEROF(markers)) > 0)
		{
			parent_node_index = markers[0].node_index;
			/* (where the marker is: the glider's model has no marker to align
			with, so it is placed by position) */
			object_set_position(scenery_index, &markers[0].matrix.position, &markers[0].matrix.forward,
				&markers[0].matrix.up);
		}
		if (parent_node_index != NONE)
		{
			object_attach_to_node(unit_index, scenery_index, (short)parent_node_index);
		}
	}
	/* (0x251C00: out, the unit's fall is held off: its downward velocity is
	made at least none, so it rides the glider rather than dropping) */
	{
		struct biped_datum *biped = biped_try_and_get(unit_index);

		if (biped)
		{
			real downward = dot_product3d(&biped->object.translational_velocity, &biped->object.up);

			if (downward < 0.f)
			{
				real_vector3d correction;

				scale_vector3d(&biped->object.up, -downward, &correction);
				add_vectors3d(&biped->object.translational_velocity, &correction,
					&biped->object.translational_velocity);
			}
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



/* ---------- the cable's bolt ---------- */

/* the bolt of a tag's projectile (the map's t2ed's: shadowrun_cable.c), fired
from the unit's body the way it faces */
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
	/* port: only a projectile the map loaded (a t2ed's reference that did
	not resolve is no tag index at all, and object_new would make an empty
	tag's object, or none) */
	if (projectile_definition_index == NONE ||
		tag_get_group_tag(projectile_definition_index) != PROJECTILE_DEFINITION_TAG)
	{
		error(_error_silent, "shadowrun cable: %08lx is not a projectile tag (the t2ed's bolt)",
			(unsigned long)projectile_definition_index);
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
	forward = unit->object.forward;
	object_placement_data_new(&placement, projectile_definition_index, unit_index);
	placement.position = origin;
	placement.forward = forward;
	perpendicular3d(&forward, &placement.up);
	normalize3d(&placement.up);
	placement.translational_velocity = *global_zero_vector3d;
	projectile_index = object_new(&placement);

	return projectile_index;
}

/* port: the map's anti-magic grenade projectile, looked for once a map (the
executable asks where it explodes, units.c FUN_0026EA00; the map's projectiles
carry none of the port's flags) */
static long shadowrun_antimagic_grenade_index = NONE;

long shadowrun_antimagic_grenade_projectile(
	void)
{
	if (shadowrun_antimagic_grenade_index == NONE)
	{
		shadowrun_antimagic_grenade_index = tag_loaded(PROJECTILE_DEFINITION_TAG, SHADOWRUN_ANTIMAGIC_GRENADE_PROJECTILE);
		if (shadowrun_antimagic_grenade_index == NONE)
		{
			error(_error_silent, "shadowrun: the map has no %s", SHADOWRUN_ANTIMAGIC_GRENADE_PROJECTILE);
		}
	}

	return shadowrun_antimagic_grenade_index;
}

/* whether the projectile is the anti-magic grenade's */
boolean shadowrun_antimagic_grenade_is(
	long projectile_index)
{
	struct object_datum *projectile = object_try_and_get_and_verify_type(projectile_index, _object_mask_projectile);

	return projectile && shadowrun_antimagic_grenade_index != NONE &&
		projectile->definition_index == shadowrun_antimagic_grenade_index;
}



/* port: the controls a local player's unit was given this tick
(players.c), which the game's own control flags cannot carry the left
bumper's tech in: it is used here, where the unit's controls are its own */
void shadowrun_player_controls(
	long unit_index,
	unsigned long flags)
{
	if ((flags & FLAG(UNIT_CONTROL_PORT_SWITCH_TECH_BIT)) && shadowrun_map_loaded())
	{
		/* the d-pad's left: the next tech the player has bought */
		shadowrun_players_switch_tech();
	}
	if (UNIT_CONTROL_PORT_TECH_BIT < 32 && (flags & FLAG(UNIT_CONTROL_PORT_TECH_BIT)) &&
		shadowrun_map_loaded())
	{
		/* port: said, as a tech that does nothing is otherwise silent. Said
		once each a second, a press being one a tick while held */
		static long last_told;

		if (game_time_get() != last_told)
		{
			struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

			error(_error_silent, "shadowrun: tech %ld used%s",
				shadowrun_unit ? shadowrun_unit->tech : -1L,
				shadowrun_tech_cast(unit_index) ? "" : " (nothing came of it)");
			last_told = game_time_get();
		}
		else
		{
			shadowrun_tech_cast(unit_index);
		}
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
		shadowrun_cable_end(unit_index);
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

/* port: whether the player has bought that tech */
boolean shadowrun_player_has_tech(
	struct shadowrun_player *shadowrun_player,
	short tech)
{
	short index;

	if (!shadowrun_player || tech < 0)
	{
		return FALSE;
	}
	for (index = 0; index < shadowrun_player->known_tech_count; index++)
	{
		if (shadowrun_player->known_techs[index] == tech)
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* port: the player takes up one of the techs it has bought (d-pad left goes
through them). TRUE if it changed */
boolean shadowrun_player_select_tech(
	struct shadowrun_player *shadowrun_player,
	long unit_index,
	short tech)
{
	if (!shadowrun_player || !shadowrun_player_has_tech(shadowrun_player, tech))
	{
		return FALSE;
	}
	shadowrun_player->chosen_tech = tech;
	if (unit_index != NONE)
	{
		shadowrun_unit_set_tech(unit_index, tech);
	}

	return TRUE;
}

/* port: the next tech the player has bought, after the one it has chosen (and
round again) */
short shadowrun_player_next_tech(
	struct shadowrun_player *shadowrun_player)
{
	short index;

	if (!shadowrun_player || shadowrun_player->known_tech_count <= 0)
	{
		return _shadowrun_tech_none;
	}
	for (index = 0; index < shadowrun_player->known_tech_count; index++)
	{
		if (shadowrun_player->known_techs[index] == shadowrun_player->chosen_tech)
		{
			return (short)shadowrun_player->known_techs[(index + 1) % shadowrun_player->known_tech_count];
		}
	}

	return (short)shadowrun_player->known_techs[0];
}

/* port: the player buys the tech: it is added to those it has (once each),
and taken up, so the rest are kept */
boolean shadowrun_player_buy_tech(
	struct shadowrun_player *shadowrun_player,
	long unit_index,
	short tech)
{
	if (!shadowrun_player || tech < 0 || tech >= MAXIMUM_SHADOWRUN_TECHS)
	{
		return FALSE;
	}
	if (!shadowrun_player_has_tech(shadowrun_player, tech))
	{
		/* port: at most two techs (port/linux/game/shadowrun_units.h) */
		if (shadowrun_player->known_tech_count >= MAXIMUM_SHADOWRUN_PLAYER_TECHS)
		{
			return FALSE;
		}
		shadowrun_player->known_techs[shadowrun_player->known_tech_count++] = tech;
	}
	shadowrun_player->chosen_tech = tech;
	if (unit_index != NONE)
	{
		shadowrun_unit_set_tech(unit_index, tech);
	}

	return TRUE;
}

/* port: a player takes the tech off; the others it has stay, and it takes up
one of them (so taking one off does not leave it with none) */
boolean shadowrun_player_remove_tech(
	struct shadowrun_player *shadowrun_player,
	long unit_index,
	short tech)
{
	short index;

	if (!shadowrun_player)
	{
		return FALSE;
	}
	for (index = 0; index < shadowrun_player->known_tech_count; index++)
	{
		if (shadowrun_player->known_techs[index] == tech)
		{
			short last = --shadowrun_player->known_tech_count;

			for (; index < last; index++)
			{
				shadowrun_player->known_techs[index] = shadowrun_player->known_techs[index + 1];
			}
			break;
		}
	}
	shadowrun_player->chosen_tech = shadowrun_player->known_tech_count > 0 ?
		(short)shadowrun_player->known_techs[0] : _shadowrun_tech_none;
	if (unit_index != NONE)
	{
		shadowrun_unit_set_tech(unit_index, shadowrun_player->chosen_tech);
	}

	return TRUE;
}

/* the scripts' player_change_tech (and the console's): the players know the
tech, which their units take. FALSE if they are not on a Shadowrun map */
boolean shadowrun_players_change_tech(
	short tech)
{
	struct data_iterator iterator;
	struct player_datum *player;
	boolean changed = FALSE;

	if (!shadowrun_map_loaded() || tech < 0 || tech >= MAXIMUM_SHADOWRUN_TECHS)
	{
		return FALSE;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		struct shadowrun_player *shadowrun_player = shadowrun_player_get(iterator.datum_index);

		if (shadowrun_player)
		{
			/* (the script gives the tech: it is bought, so the others stay) */
			shadowrun_player_buy_tech(shadowrun_player, player->unit_index, tech);
			changed = TRUE;
		}
	}

	return changed;
}

/* port: the d-pad's left, on a Shadowrun map: the local players take up the
next of their techs */
void shadowrun_players_switch_tech(
	void)
{
	short local_player_index = local_player_get_next(NONE);

	while (local_player_index != NONE)
	{
		long player_index = local_player_get_player_index(local_player_index);
		struct player_datum *player = player_try_and_get(player_index);
		struct shadowrun_player *shadowrun_player = shadowrun_player_get(player_index);
		short next = shadowrun_player_next_tech(shadowrun_player);

		if (player && next != _shadowrun_tech_none)
		{
			shadowrun_player_select_tech(shadowrun_player, player->unit_index, next);
		}
		local_player_index = local_player_get_next(local_player_index);
	}

	return;
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

/* a hologram taken out of the world: its actor and its unit deleted (not
killed: a kill leaves a body, and a hologram leaves nothing) */
static void shadowrun_hologram_remove(
	long *actor_index)
{
	struct actor_datum *actor = actor_try_and_get(*actor_index);

	if (actor)
	{
		long unit_index = actor->meta.unit_index;

		actor_delete(*actor_index, TRUE);
		if (unit_index != NONE && object_try_and_get_and_verify_type(unit_index, NONE))
		{
			object_delete(unit_index);
		}
	}
	*actor_index = NONE;

	return;
}

/* the holograms whose time is up, or whose caster is gone, go: deleted and
forgotten */
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
			shadowrun_holograms[index] = shadowrun_holograms[--shadowrun_hologram_count];
			shadowrun_hologram_remove(&hologram->actor_index);
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
			long actor_index = hologram->actor_index;

			shadowrun_holograms[index] = shadowrun_holograms[--shadowrun_hologram_count];
			shadowrun_hologram_remove(&actor_index);
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
