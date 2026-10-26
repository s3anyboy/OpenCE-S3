/*
SHADOWRUN_RACES.C

What the Shadowrun prototype's races (shadowrun_definitions.h) make of the
damage a unit takes, its movement and its turning, as its executable
(cachesym.xbe) does: the troll's rage most of all.

A race takes damage at its own scale (melee at another), and a player of a
race deals melee at its own. A unit of a race that rages counts the damage
it took over the last half second; more than its race's threshold, and the
unit rages more with each hit (each costing it mana, if the race says so),
up to all the way. Its rage waits its race's ticks after the last hit, then
goes down by its race's decay each tick. As it rages the unit takes less
damage (and heals less), moves and turns more slowly, grows to its race's
raging scale, and its colours go pale; past its race's material threshold,
what hits it looks as it does on a jackal's shield.

How fast a player moves is its race's with the weapon in its hand (the
weapon's srwd, else its srud's), scaled by its rage.
*/

/* ---------- headers */

#include "cseries.h"
#include "ai/actor_definitions.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "objects/damage.h"
#include "objects/damage_effect_definitions.h"
#include "objects/objects.h"
#include "units/units.h"
#include "shadowrun_units.h"

#include <math.h>
#include <stddef.h>

/* ---------- constants */

enum
{
	/* damage.c's (its enums are its own): melee's category, and the
	definition's flag of damage that skips shields */
	_shadowrun_damage_category_melee = 6,
	_shadowrun_damage_skips_shields_bit = 9,

	/* where an actor variant's definition names its race */
	SHADOWRUN_ACTOR_VARIANT_RACE_OFFSET = 0x44,
};

typedef char shadowrun_actor_variant_race_offset_assert[
	SHADOWRUN_ACTOR_VARIANT_RACE_OFFSET >= offsetof(struct actor_variant_definition, unused) &&
	SHADOWRUN_ACTOR_VARIANT_RACE_OFFSET + sizeof(short) <=
		offsetof(struct actor_variant_definition, unused) + sizeof(((struct actor_variant_definition *)0)->unused) ? 1 : -1];

/* a colour channel goes towards this as its unit rages (one with none of
it: the others go towards all of it) */
#define RAGE_PALE_CHANNEL 0.8f

/* ---------- private prototypes */

static long shadowrun_object_root(
	long object_index);
static void shadowrun_unit_rage_damage(
	long unit_index,
	real damage);
static boolean shadowrun_unit_rages(
	long unit_index);
static real shadowrun_unit_rage_material_threshold(
	long unit_index);

/* ---------- public code */

/* 0x1BA940's: the damage an object takes, scaled by its race (melee by
another of its scales), and by its rage, and by the melee scale of a player
of a race dealing it; it makes the object rage */
real shadowrun_damage_scale(
	struct damage_data const *damage,
	struct damage_definition const *definition,
	long object_index,
	real total_damage)
{
	long root_index = shadowrun_object_root(object_index);
	struct shadowrun_race *race = shadowrun_unit_race_get(root_index);

	if (race)
	{
		boolean rage_guards = TRUE;

		total_damage *= definition->category == _shadowrun_damage_category_melee ?
			race->melee_damage_scale :
			race->damage_scale;
		shadowrun_unit_rage_damage(root_index, definition->active_camouflage_damage);

		/* 0x1BAB81: a race whose rage guards its front only: hit from
		behind (within two radians of where it looks), its rage keeps off
		nothing */
		if (TEST_FLAG(race->flags, _shadowrun_race_rage_guards_front_bit))
		{
			struct unit_datum *victim = unit_try_and_get(root_index);
			struct unit_datum *owner = damage->owner_object_index != NONE ?
				unit_try_and_get(damage->owner_object_index) : NULL;

			if (victim && owner && victim->object.type == _object_type_biped)
			{
				real_vector3d facing = victim->unit.looking_vector;
				real_vector3d from_owner;
				long whole_radians;

				vector_from_points3d(&owner->object.bounding_sphere_center,
					&victim->object.bounding_sphere_center, &from_owner);
				from_owner.k = 0.f;
				facing.k = 0.f;
				/* BUG (original, preserved): the angle is truncated to whole
				radians before it is compared with a right angle */
				whole_radians = (long)angle_between_vectors3d(&from_owner, &facing);
				if (whole_radians < 0)
				{
					whole_radians = -whole_radians;
				}
				if ((real)whole_radians < _pi / 2.f)
				{
					rage_guards = FALSE;
				}
			}
		}
		if (rage_guards &&
			!TEST_FLAG(damage->flags, _damage_bypasses_shields_bit) &&
			!TEST_FLAG(definition->flags, _shadowrun_damage_skips_shields_bit))
		{
			total_damage *= shadowrun_unit_heal_scale(root_index);
		}

		if (definition->category == _shadowrun_damage_category_melee &&
			damage->owner_player_index != NONE)
		{
			struct player_datum *player = player_try_and_get(damage->owner_player_index);

			if (player && player->unit_index != NONE)
			{
				struct shadowrun_race *owner_race = shadowrun_unit_race_get(player->unit_index);

				if (owner_race)
				{
					total_damage *= owner_race->melee_scale;
				}
			}
		}
	}

	return total_damage;
}

/* 0x1BB486: in a campaign, damage strikes a troll raging past its race's
threshold as it would a jackal's shield */
void shadowrun_damage_material(
	struct damage_data *damage,
	long object_index)
{
	long root_index = shadowrun_object_root(object_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(root_index);

	if (shadowrun_unit &&
		shadowrun_unit_race_get(root_index) &&
		!game_engine_running() &&
		shadowrun_unit_rages(root_index) &&
		shadowrun_unit_rage_material_threshold(root_index) < shadowrun_unit->rage)
	{
		damage->material_type = _material_jackal_energy_shield;
	}

	return;
}

/* 0x2541D0's: a player's biped moves at its race's speed with its weapon
(scaled by its rage), and gets there at its race's acceleration; 0 or less
leaves either be. (The executable halves the speed in a state of the biped's
that Shadowrun's tech sets: not played.) */
void shadowrun_biped_movement_scales(
	long biped_index,
	real *movement_scale,
	real *acceleration_scale)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(biped_index);
	struct shadowrun_weapon_data *data = shadowrun_unit ? shadowrun_unit_weapon_data_get(biped_index) : NULL;

	*movement_scale = 1.f;
	*acceleration_scale = 1.f;
	if (data && shadowrun_unit->race_index >= 0 && shadowrun_unit->race_index < data->races.count)
	{
		struct shadowrun_weapon_data_race *race = tag_block_get_element_with_size(&data->races,
			shadowrun_unit->race_index, sizeof(struct shadowrun_weapon_data_race));

		if (race->movement_scale > 0.f)
		{
			/* 0x264B00 */
			*movement_scale = shadowrun_unit->rage_movement_scale * race->movement_scale;
		}
		if (race->acceleration_scale > 0.f)
		{
			*acceleration_scale = race->acceleration_scale;
		}
	}

	return;
}

/* 0x264B30: how fast the unit turns (its rage's) */
real shadowrun_unit_turning_scale(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	return shadowrun_unit ? shadowrun_unit->rage_turning_scale : 1.f;
}

/* 0x52E10's: an actor's unit takes the race its variant names (in what
this build's actv has as unused: its race, 0x44, after its srac's index,
0x40) */
void shadowrun_actor_attached(
	long actor_variant_definition_index,
	long unit_index)
{
	if (shadowrun_unit_get(unit_index))
	{
		struct actor_variant_definition *definition = actor_variant_definition_get(actor_variant_definition_index);

		shadowrun_unit_set_race(unit_index, *(short *)((byte *)definition + SHADOWRUN_ACTOR_VARIANT_RACE_OFFSET));
	}

	return;
}

/* 0x521E0's: an actor's unit keeps its variant's colours, which its rage
paler */
void shadowrun_actor_change_color(
	long unit_index,
	short change_color_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit && change_color_index >= 0 && change_color_index < NUMBER_OF_OBJECT_CHANGE_COLORS)
	{
		shadowrun_unit->actor_change_colors[change_color_index] =
			unit_get(unit_index)->object.base_change_colors[change_color_index];
	}

	return;
}

/* 0x264BE0: each tick, a unit of a race that rages: its rage. More damage
over the last half second than the race's threshold, and the damage of this
tick makes it rage more (if it pays the race's mana for it); after its wait,
its rage goes down. Its rage sets its scales of damage, movement and turning
between the race's calm and raging ones, grows it towards the race's raging
scale and turns its colours pale */
void shadowrun_unit_rage_update(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	/* port: the executable reads the race's decay and threshold before it
	looks whether the unit has a race */
	struct shadowrun_race *race = shadowrun_unit ? shadowrun_unit_race_get(unit_index) : NULL;

	if (race && TEST_FLAG(race->flags, _shadowrun_race_rages_bit))
	{
		struct unit_datum *unit = unit_get(unit_index);
		real recent_damage = 0.f;
		real rage_root = 0.f;
		real scale;
		short index;

		for (index = 0; index < SHADOWRUN_RAGE_DAMAGE_TICKS; index++)
		{
			recent_damage += shadowrun_unit->rage_damage[index];
		}
		if (race->rage_threshold < recent_damage)
		{
			real damage = shadowrun_unit->rage_damage[shadowrun_unit->rage_damage_tick];

			if (race->rage_mana_cost <= 0.f ||
				shadowrun_unit_spend_mana(unit_index, race->rage_mana_cost * damage, TRUE, TRUE))
			{
				shadowrun_unit->rage += damage;
			}
		}
		if (shadowrun_unit->rage_wait_ticks < 1)
		{
			shadowrun_unit->rage -= race->rage_decay;
		}
		else
		{
			shadowrun_unit->rage_wait_ticks--;
		}
		shadowrun_unit->rage = PIN(shadowrun_unit->rage, 0.f, 1.f);

		shadowrun_unit->rage_damage_scale = shadowrun_unit->rage * race->raging_damage_scale +
			(1.f - shadowrun_unit->rage) * race->calm_damage_scale;
		shadowrun_unit->rage_movement_scale = shadowrun_unit->rage * race->raging_movement_scale +
			(1.f - shadowrun_unit->rage) * race->calm_movement_scale;
		shadowrun_unit->rage_turning_scale = shadowrun_unit->rage * race->raging_turning_scale +
			(1.f - shadowrun_unit->rage) * race->calm_turning_scale;
		if (shadowrun_unit->rage > 0.f)
		{
			rage_root = (real)sqrt(shadowrun_unit->rage);
		}

		/* it grows (or shrinks) towards its scale at the race's speed */
		scale = rage_root * race->raging_scale + (1.f - rage_root) * race->scale;
		if (unit->object.scale < scale)
		{
			unit->object.scale += race->rage_scale_speed / 30.f;
			if (scale < unit->object.scale)
			{
				unit->object.scale = scale;
			}
		}
		else if (scale < unit->object.scale)
		{
			unit->object.scale -= race->rage_scale_speed / 30.f;
			if (unit->object.scale < scale)
			{
				unit->object.scale = scale;
			}
		}

		/* a player's colours (its player's), or an actor's (its variant's),
		go pale with its rage. (So would those of the drones Shadowrun's tech
		gives a player, in their player's colour: not played.) */
		if (unit->unit.player_index != NONE || unit->unit.actor_index != NONE)
		{
			for (index = 0; index < NUMBER_OF_OBJECT_CHANGE_COLORS; index++)
			{
				real_rgb_color color;

				if (unit->unit.actor_index == NONE)
				{
					game_engine_player_get_change_color(&color, unit->unit.player_index);
				}
				else
				{
					color = shadowrun_unit->actor_change_colors[index];
				}
				color.red = rage_root * (color.red > 0.f ? 1.f : RAGE_PALE_CHANNEL) + (1.f - rage_root) * color.red;
				color.green = rage_root * (color.green > 0.f ? 1.f : RAGE_PALE_CHANNEL) +
					(1.f - rage_root) * color.green;
				color.blue = rage_root * (color.blue > 0.f ? 1.f : RAGE_PALE_CHANNEL) + (1.f - rage_root) * color.blue;
				unit->object.outgoing_change_colors[index] = color;
			}
		}

		/* the next tick's damage counts afresh */
		shadowrun_unit->rage_damage_tick++;
		if (shadowrun_unit->rage_damage_tick >= SHADOWRUN_RAGE_DAMAGE_TICKS)
		{
			shadowrun_unit->rage_damage_tick = 0;
		}
		shadowrun_unit->rage_damage[shadowrun_unit->rage_damage_tick] = 0.f;
	}

	return;
}

/* ---------- private code */

/* the object an object is attached to, all the way up */
static long shadowrun_object_root(
	long object_index)
{
	long root_index = object_index;

	while (root_index != NONE && object_get(root_index)->object.parent_object_index != NONE)
	{
		root_index = object_get(root_index)->object.parent_object_index;
	}

	return root_index;
}

/* 0x265140: a unit of a race that rages counts the damage (up to a whole
point a tick); more over the last half second than its race's threshold,
and it waits its race's ticks before its rage goes down */
static void shadowrun_unit_rage_damage(
	long unit_index,
	real damage)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	/* port: the executable reads the race's threshold and wait before it
	looks whether the unit has a race */
	struct shadowrun_race *race = shadowrun_unit ? shadowrun_unit_race_get(unit_index) : NULL;

	if (race && TEST_FLAG(race->flags, _shadowrun_race_rages_bit))
	{
		real *tick_damage = &shadowrun_unit->rage_damage[shadowrun_unit->rage_damage_tick];
		real recent_damage = 0.f;
		short index;

		*tick_damage = PIN(*tick_damage + damage, 0.f, 1.f);
		for (index = 0; index < SHADOWRUN_RAGE_DAMAGE_TICKS; index++)
		{
			recent_damage += shadowrun_unit->rage_damage[index];
		}
		if (race->rage_threshold < recent_damage)
		{
			shadowrun_unit->rage_wait_ticks = (long)race->rage_wait_ticks;
		}
	}

	return;
}

/* 0x264B90: whether the unit's race rages */
static boolean shadowrun_unit_rages(
	long unit_index)
{
	struct shadowrun_race *race = shadowrun_unit_race_get(unit_index);

	return race && TEST_FLAG(race->flags, _shadowrun_race_rages_bit);
}

/* 0x264A80: how far the unit rages before damage strikes it as a shield (its
race's; all the way without one) */
static real shadowrun_unit_rage_material_threshold(
	long unit_index)
{
	struct shadowrun_race *race = shadowrun_unit_race_get(unit_index);

	return race ? race->rage_material_threshold : 1.f;
}
