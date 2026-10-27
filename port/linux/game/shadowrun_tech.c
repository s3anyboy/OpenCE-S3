/*
SHADOWRUN_TECH.C

The Shadowrun prototype's tech (shadowrun_definitions.h's t1ed, t2ed and t3ed):
which power a tech item is, and using it.

A unit's known_powers (shadowrun_units.h) holds its magic and its tech
together: the spells first, then the tech, which is why shadowrun_tech_power
gives a tech item's place after NUMBER_OF_SHADOWRUN_SPELLS. Buying tech works
as buying a spell does (shadowrun_buy_menu.c), and the left bumper uses it
(player_control.c).

None of the tech's tags say what the tech does, but two do something here:
Enhanced Vision marks every enemy within 60 m with a nav point, and the
Hologram spawns an actor of the hologram's actor variant
(`characters\hologram\hologram assault rifle`) where the caster looks, on its
side. The rest are bought and do nothing yet, since what they do is in the
prototype's executable, not in its tags.
*/

#include "cseries.h"
#include "ai/actor_placement.h"
#include "ai/ai_scenario_definitions.h"
#include "game/players.h"
#include "interface/hud.h"
#include "interface/hud_nav_points.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "tag_files/tag_files.h"
#include "units/units.h"
#include "shadowrun_definitions.h"
#include "shadowrun_units.h"
#include "shadowrun_tech.h"

/* how far Enhanced Vision reaches: 60 metres, in the engine's world units.
A world unit is a tenth of a foot here (global_gravity, physics.c, 0.0035651792
per tick squared, is 32.09 units per second squared, against 32.174 feet: 10.03
units to the foot), so a metre is 3.057 units and 60 m is 183.4. */
#define SHADOWRUN_VISION_RADIUS (60.f * 3.057f)
/* the nav point it draws, by the hud tag's arrows (a goal's, as the HUD's own
waypoints use) */
#define SHADOWRUN_VISION_NAV_POINT 0

/* the tech items the buy menu sells, in its order (shadowrun_buy_menu.c's
_ shadowrun_shop_first_tech items: Grapple, Glider, Hologram, Smartlink,
Enhanced Vision) */
enum
{
	SHADOWRUN_TECH_GRAPPLE = 0,
	SHADOWRUN_TECH_GLIDER,
	SHADOWRUN_TECH_HOLOGRAM,
	SHADOWRUN_TECH_SMARTLINK,
	SHADOWRUN_TECH_VISION,
	NUMBER_OF_SHADOWRUN_TECH
};

/* how far the Hologram is placed: 5 m, in world units (see
SHADOWRUN_VISION_RADIUS for the scale) */
#define SHADOWRUN_HOLOGRAM_RANGE (5.f * 3.057f)
/* the actor variant a Hologram is, by the name its tag has in the map */
#define SHADOWRUN_HOLOGRAM_VARIANT "characters\\hologram\\hologram assault rifle"

/* whether the unit has bought that tech */
static boolean shadowrun_tech_has(
	long unit_index,
	short tech);
static void shadowrun_tech_place_hologram(
	long unit_index);

short shadowrun_tech_power(
	short tech)
{
	short power = (short)(NUMBER_OF_SHADOWRUN_SPELLS + tech);

	return tech >= 0 && power < MAXIMUM_SHADOWRUN_POWERS ? power : NONE;
}

/* whether the unit has bought that tech */
static boolean shadowrun_tech_has(
	long unit_index,
	short tech)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	short power = shadowrun_tech_power(tech);

	return shadowrun_unit && power != NONE && shadowrun_unit->known_powers[power] > 0;
}

/* The Hologram: an actor of the hologram's actor variant (the assault rifle
one), at the caster's side, where it looks and up to SHADOWRUN_HOLOGRAM_RANGE.
Its starting location is made as a released rider's is
(coop_enemies.c). */
static void shadowrun_tech_place_hologram(
	long unit_index)
{
	struct object_datum *caster = object_try_and_get(unit_index);
	struct collision_result collision;
	struct actor_starting_location location;
	real_point3d origin;
	real_point3d target;
	real_vector3d reach;
	real distance;
	long variant_index;
	long placed_unit;

	variant_index = tag_loaded('actv', SHADOWRUN_HOLOGRAM_VARIANT);
	if (!caster || variant_index == NONE)
	{
		return;
	}
	object_get_origin(unit_index, &origin);
	/* (where it looks, stopped by what is in the way) */
	distance = SHADOWRUN_HOLOGRAM_RANGE;
	scale_vector3d(&caster->object.forward, distance, &reach);
	if (collision_test_vector(_collision_test_for_projectiles_flags, &origin, &reach, unit_index, &collision))
	{
		target = collision.point;
	}
	else
	{
		add_vectors3d((real_vector3d const *)&origin, &reach, (real_vector3d *)&target);
	}
	csmemset(&location, 0, sizeof(location));
	location.position = target;
	location.facing = 0.f;
	location.cluster_index = NONE;
	location.noncombat_sequence_id = NONE;
	location.default_state = NONE;
	location.initial_state = NONE;
	location.actor_variant_index = (short)variant_index;
	location.command_list_index = NONE;
	placed_unit = actor_place(variant_index, NONE, NONE, &location, FALSE, 0);
	if (placed_unit != NONE)
	{
		struct object_datum *hologram = object_try_and_get(placed_unit);

		if (hologram)
		{
			/* (on the caster's side, so its enemies shoot it) */
			hologram->object.owner_team_index = caster->object.owner_team_index;
		}
	}
}

void shadowrun_tech_use(
	long unit_index)
{
	/* (the Hologram is used here; Enhanced Vision draws each tick, see
	shadowrun_tech_render_nav_points) */
	if (shadowrun_tech_has(unit_index, SHADOWRUN_TECH_HOLOGRAM))
	{
		shadowrun_tech_place_hologram(unit_index);
	}
}

/* whether the unit has bought Enhanced Vision */
static boolean shadowrun_tech_has_vision(
	long unit_index)
{
	return shadowrun_tech_has(unit_index, SHADOWRUN_TECH_VISION);
}

void shadowrun_tech_render_nav_points(
	short local_player_index)
{
	long player_index = local_player_get_player_index(local_player_index);
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct object_iterator iterator;
	struct object_datum *object;
	struct object_datum *caster;
	real_point3d head;
	real radius_squared;

	if (!player || player->unit_index == NONE || !shadowrun_tech_has_vision(player->unit_index))
	{
		return;
	}
	caster = object_get(player->unit_index);
	unit_get_head_position(player->unit_index, &head);
	/* (Shadowrun's 60 m, in world units: SHADOWRUN_VISION_RADIUS) */
	radius_squared = SHADOWRUN_VISION_RADIUS * SHADOWRUN_VISION_RADIUS;
	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		real_point3d origin;
		real dx, dy, dz;
		short render_type;

		if (TEST_FLAG(object->object.damage_flags, _object_dead_bit) ||
			object->object.owner_team_index == caster->object.owner_team_index)
		{
			continue;
		}
		object_get_origin(DATUM_INDEX_TO_ABSOLUTE_INDEX(object->header.index), &origin);
		dx = origin.x - head.x;
		dy = origin.y - head.y;
		dz = origin.z - head.z;
		if (dx * dx + dy * dy + dz * dz > radius_squared)
		{
			continue;
		}
		render_type = hud_get_nav_point_render_type(local_player_index, &head, &origin, NONE);
		custom_render_nav_point(local_player_index, &origin, SHADOWRUN_VISION_NAV_POINT, render_type);
	}
}
