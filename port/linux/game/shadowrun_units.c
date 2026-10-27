/*
SHADOWRUN_UNITS.C

What the Shadowrun prototype adds to its units and players (shadowrun.h):
their state, their definitions, mana, and the scripts that give magic. Its
spells are shadowrun_magic.c's.

Ported from the prototype's executable (cachesym.xbe, build 01.12.07.0132);
each function names the one it is (its address in the executable).
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "game/players.h"
#include "items/weapon_definitions.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "effects/effects.h"
#include "saved games/game_state.h"
#include "sound/game_sound.h"
#include "tag_files/tag_groups.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "units/units.h"
#include "shadowrun_units.h"
#include "shadowrun_tech.h"

#include <string.h>

/* ---------- constants */

enum
{
	/* where a biped's definition names its srud: a tag index in what this
	build's bipd has as unused (the reference's index, 0x468) */
	SHADOWRUN_BIPED_UNIT_DEFINITION_OFFSET = 0x45C,
	/* where a weapon's definition names its srwd: in what this build's weap
	has as unused, after its heat illumination (the reference's index,
	0x370) */
	SHADOWRUN_WEAPON_DATA_OFFSET = 0x364,
};

typedef char shadowrun_weapon_data_offset_assert[
	SHADOWRUN_WEAPON_DATA_OFFSET == offsetof(struct weapon_definition, weapon.heat_unused) &&
	SHADOWRUN_WEAPON_DATA_OFFSET + sizeof(struct tag_reference) <=
		offsetof(struct weapon_definition, weapon.heat_unused) + sizeof(((struct weapon_definition *)0)->weapon.heat_unused) ? 1 : -1];

typedef char shadowrun_biped_unit_definition_offset_assert[
	SHADOWRUN_BIPED_UNIT_DEFINITION_OFFSET >= offsetof(struct biped_definition, biped.unused) &&
	SHADOWRUN_BIPED_UNIT_DEFINITION_OFFSET + sizeof(struct tag_reference) <=
		offsetof(struct biped_definition, biped.unused) + sizeof(((struct biped_definition *)0)->biped.unused) ? 1 : -1];

/* ---------- globals */

static struct
{
	/* the map loaded is the Shadowrun prototype's */
	boolean map_loaded;
	/* the players' (in the game state) */
	struct shadowrun_player *players;
} shadowrun_globals;

/* ---------- private prototypes */

static struct tag_reference *shadowrun_biped_unit_definition_reference(
	struct biped_definition *definition);
static struct tag_reference *shadowrun_weapon_data_reference(
	struct weapon_definition *definition);
static void shadowrun_unit_mana_update(
	long unit_index);
static void shadowrun_unit_new_head(
	long unit_index);
static void shadowrun_unit_delete_head(
	long unit_index);
static void shadowrun_unit_head_update(
	long unit_index);

/* ---------- public code */

void shadowrun_initialize(
	void)
{
	shadowrun_globals.players = game_state_malloc("shadowrun players", NULL,
		HALO_PORT_MAXIMUM_NETWORK_PLAYERS * sizeof(struct shadowrun_player));
	shadowrun_magic_initialize();

	return;
}

void shadowrun_initialize_for_new_map(
	void)
{
	csmemset(shadowrun_globals.players, 0, HALO_PORT_MAXIMUM_NETWORK_PLAYERS * sizeof(struct shadowrun_player));
	shadowrun_magic_initialize_for_new_map();
	shadowrun_buy_menu_initialize_for_new_map();

	return;
}

void shadowrun_tags_loaded(
	boolean shadowrun)
{
	shadowrun_globals.map_loaded = shadowrun;
	if (shadowrun)
	{
		struct tag_iterator iterator;
		long tag_index;
		long corrected = 0;

		shadowrun_projectiles_loaded();
		/* each biped's srud, in what the validator does not check */
		tag_iterator_new(&iterator, BIPED_DEFINITION_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			struct tag_reference *reference =
				shadowrun_biped_unit_definition_reference(tag_get(BIPED_DEFINITION_TAG, tag_index));

			if (reference->index != NONE &&
				cache_file_tag_index_group_tag(reference->index) != SHADOWRUN_UNIT_DEFINITION_TAG)
			{
				reference->index = NONE;
				corrected++;
			}
		}
		if (corrected)
		{
			error(_error_silent, "shadowrun: %ld bipeds named no Shadowrun unit", corrected);
		}
		/* each weapon's srwd, likewise */
		corrected = 0;
		tag_iterator_new(&iterator, WEAPON_DEFINITION_TAG);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			struct tag_reference *reference =
				shadowrun_weapon_data_reference(tag_get(WEAPON_DEFINITION_TAG, tag_index));

			if (reference->index != NONE &&
				cache_file_tag_index_group_tag(reference->index) != SHADOWRUN_WEAPON_DATA_TAG)
			{
				reference->index = NONE;
				corrected++;
			}
		}
		if (corrected)
		{
			error(_error_silent, "shadowrun: %ld weapons named no Shadowrun weapon data", corrected);
		}
	}

	return;
}

boolean shadowrun_map_loaded(
	void)
{
	return shadowrun_globals.map_loaded;
}

short shadowrun_object_extra_size(
	short object_type)
{
	short size = 0;

	if (shadowrun_globals.map_loaded && TEST_FLAG(_object_mask_unit, object_type))
	{
		size = sizeof(struct shadowrun_unit);
	}

	return size;
}

struct shadowrun_unit *shadowrun_unit_get(
	long unit_index)
{
	struct shadowrun_unit *result = NULL;

	if (shadowrun_globals.map_loaded && unit_index != NONE)
	{
		struct object_datum *object = object_try_and_get_and_verify_type(unit_index, _object_mask_unit);

		if (object)
		{
			struct object_header_datum *header = object_header_get(unit_index);
			short datum_size = object_type_get_datum_size(object->object.type);

			if (header->data_size >= datum_size + (short)sizeof(struct shadowrun_unit))
			{
				result = (struct shadowrun_unit *)((byte *)object + datum_size);
			}
		}
	}

	return result;
}

struct shadowrun_player *shadowrun_player_get(
	long player_index)
{
	struct shadowrun_player *result = NULL;

	if (shadowrun_globals.map_loaded && player_index != NONE && player_try_and_get(player_index))
	{
		result = &shadowrun_globals.players[DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)];
	}

	return result;
}

/* the unit's new state (0x25F6B0, unit_new's) */
void shadowrun_unit_new(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit)
	{
		short index;

		csmemset(shadowrun_unit, 0, sizeof(*shadowrun_unit));
		shadowrun_unit->strangle_looping_sound_index = NONE;
		shadowrun_unit->resurrection_ball_index = NONE;
		for (index = 0; index < MAXIMUM_SHADOWRUN_RESURRECTED_ACTORS; index++)
		{
			shadowrun_unit->resurrected_actor_indices[index] = NONE;
		}
		shadowrun_unit->master_player_index = NONE;
		shadowrun_unit->master_unit_index = NONE;
		for (index = 0; index < MAXIMUM_SHADOWRUN_SUMMONS; index++)
		{
			shadowrun_unit->summoned_actor_indices[index] = NONE;
		}
		shadowrun_unit->race_scale = 1.f;
		shadowrun_unit->head_object_index = NONE;
		shadowrun_unit->head_definition_index = NONE;
		for (index = 0; index < MAXIMUM_SHADOWRUN_TREES; index++)
		{
			shadowrun_unit->tree_indices[index] = NONE;
		}
		for (index = 0; index < MAXIMUM_SHADOWRUN_OLD_TREES; index++)
		{
			shadowrun_unit->old_tree_indices[index] = NONE;
		}
		shadowrun_unit->spell_effect_index = NONE;
		shadowrun_unit->smoke_end_time = -SMOKE_RECAST_WAIT;
		shadowrun_unit->rage_damage_scale = 1.f;
		shadowrun_unit->rage_movement_scale = 1.f;
		shadowrun_unit->rage_turning_scale = 1.f;
	}

	return;
}

/* 0x25E440: a biped's srud */
struct shadowrun_unit_definition *shadowrun_unit_definition_get(
	long unit_index)
{
	struct shadowrun_unit_definition *result = NULL;
	struct biped_datum *biped;

	if (shadowrun_globals.map_loaded && (biped = biped_try_and_get(unit_index)) != NULL)
	{
		struct tag_reference *reference =
			shadowrun_biped_unit_definition_reference(biped_definition_get(biped->definition_index));

		if (reference->index != NONE)
		{
			result = tag_get(SHADOWRUN_UNIT_DEFINITION_TAG, reference->index);
		}
	}

	return result;
}

/* 0x25E4C0 */
struct shadowrun_race *shadowrun_unit_race_get(
	long unit_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct shadowrun_race *result = NULL;

	if (definition && shadowrun_unit &&
		shadowrun_unit->race_index >= 0 && shadowrun_unit->race_index < definition->races.count)
	{
		result = tag_block_get_element_with_size(&definition->races, shadowrun_unit->race_index,
			sizeof(struct shadowrun_race));
	}

	return result;
}

/* 0x25E5E0 */
struct shadowrun_magic1_definition *shadowrun_magic1_get(
	long unit_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_get(unit_index);

	return definition && definition->magic1.index != NONE ?
		tag_get(SHADOWRUN_MAGIC1_DEFINITION_TAG, definition->magic1.index) : NULL;
}

/* 0x25E680 */
struct shadowrun_magic2_definition *shadowrun_magic2_get(
	long unit_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_get(unit_index);

	return definition && definition->magic2.index != NONE ?
		tag_get(SHADOWRUN_MAGIC2_DEFINITION_TAG, definition->magic2.index) : NULL;
}

/* 0x25E720 */
struct shadowrun_magic3_definition *shadowrun_magic3_get(
	long unit_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_get(unit_index);

	return definition && definition->magic3.index != NONE ?
		tag_get(SHADOWRUN_MAGIC3_DEFINITION_TAG, definition->magic3.index) : NULL;
}

/* 0x264AD0: how much a heal heals the unit: as much as damage hurts it (a
troll's rage keeps off both) */
real shadowrun_unit_heal_scale(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	return shadowrun_unit ? shadowrun_unit->rage_damage_scale : 1.f;
}

/* 0x25E9A0: the srwd of the biped's weapon in hand (the smartlinked one when
it is smartlinked: Shadowrun's tech, not played), else its srud's */
struct shadowrun_weapon_data *shadowrun_unit_weapon_data_get(
	long unit_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_get(unit_index);
	struct shadowrun_weapon_data *result = NULL;

	if (definition)
	{
		struct unit_datum *unit = unit_get(unit_index);
		long weapon_index = unit->unit.current_weapon_index != NONE ?
			unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index) : NONE;
		struct object_datum *weapon = weapon_index != NONE ? object_get(weapon_index) : NULL;
		long data_index = weapon ?
			shadowrun_weapon_data_reference(weapon_definition_get(weapon->definition_index))->index : NONE;

		if (data_index != NONE)
		{
			result = tag_get(SHADOWRUN_WEAPON_DATA_TAG, data_index);
		}
		else if (definition->default_weapon_data.index != NONE)
		{
			result = tag_get(SHADOWRUN_WEAPON_DATA_TAG, definition->default_weapon_data.index);
		}
	}

	return result;
}

/* 0x285040, 0x2850A0: the race's head hidden, or seen */
void shadowrun_unit_show_head(
	long unit_index,
	boolean show)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit && shadowrun_unit->head_object_index != NONE)
	{
		struct object_datum *head = object_try_and_get_and_verify_type(shadowrun_unit->head_object_index, NONE);

		if (head)
		{
			SET_FLAG(head->object.flags, _object_invisible_bit, !show);
		}
	}

	return;
}

/* 0x25E540 */
struct shadowrun_buy_menu_definition *shadowrun_buy_menu_get(
	long unit_index)
{
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_get(unit_index);

	/* BUG (original, preserved): it asks whether the unit has magic1, not a
	buy menu */
	return definition && definition->magic1.index != NONE && definition->buy_menu.index != NONE ?
		tag_get(SHADOWRUN_BUY_MENU_DEFINITION_TAG, definition->buy_menu.index) : NULL;
}

/* 0x25EB20 */
void shadowrun_spell_get(
	long unit_index,
	short spell,
	long *mana_cost,
	real *mana_cost_per_second,
	long *capacity_reserved,
	long *cast_cooldown_ticks,
	long *spell_cooldown_ticks)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (unit && shadowrun_unit)
	{
		if (unit->unit.actor_index == NONE)
		{
			struct tag_block *levels = NULL;
			long cooldown = NONE;
			struct shadowrun_spell_level *level = NULL;

			switch (spell)
			{
			case _shadowrun_spell_teleport:
			case _shadowrun_spell_gust:
			case _shadowrun_spell_tree_of_life:
			{
				struct shadowrun_magic1_definition *magic = shadowrun_magic1_get(unit_index);

				if (magic)
				{
					switch (spell)
					{
					case _shadowrun_spell_teleport:
						levels = &magic->teleport_levels;
						cooldown = magic->teleport_cooldown_ticks;
						break;
					case _shadowrun_spell_gust:
						levels = &magic->gust_levels;
						cooldown = magic->gust_cooldown_ticks;
						break;
					default:
						levels = &magic->tree_of_life_levels;
						cooldown = magic->tree_of_life_cooldown_ticks;
						break;
					}
				}
				break;
			}
			case _shadowrun_spell_resurrection:
			case _shadowrun_spell_strangle:
			case _shadowrun_spell_summon_elemental:
			{
				struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);

				if (magic)
				{
					switch (spell)
					{
					case _shadowrun_spell_resurrection:
						levels = &magic->resurrection_levels;
						cooldown = magic->resurrection_cooldown_ticks;
						break;
					case _shadowrun_spell_strangle:
						levels = &magic->strangle_levels;
						cooldown = magic->strangle_cooldown_ticks;
						break;
					default:
						levels = &magic->summon_levels;
						cooldown = magic->summon_cooldown_ticks;
						break;
					}
				}
				break;
			}
			case _shadowrun_spell_spirit_animal:
			case _shadowrun_spell_smoke:
			{
				struct shadowrun_magic3_definition *magic = shadowrun_magic3_get(unit_index);

				if (magic)
				{
					if (spell == _shadowrun_spell_spirit_animal)
					{
						levels = &magic->spirit_animal_levels;
						cooldown = magic->spirit_animal_cooldown_ticks;
					}
					else
					{
						levels = &magic->smoke_levels;
						cooldown = magic->smoke_cooldown_ticks;
					}
				}
				break;
			}
			default:
				/* (pillar of salt and the rest have none) */
				break;
			}
			if (levels && shadowrun_unit->race_index >= 0 && shadowrun_unit->race_index < levels->count)
			{
				level = tag_block_get_element_with_size(levels, shadowrun_unit->race_index,
					sizeof(struct shadowrun_spell_level));
			}
			if (level)
			{
				if (mana_cost)
					*mana_cost = level->mana_cost;
				if (mana_cost_per_second)
					*mana_cost_per_second = level->mana_cost_per_second;
				if (capacity_reserved)
					*capacity_reserved = level->capacity_reserved;
				if (spell_cooldown_ticks)
					*spell_cooldown_ticks = level->cooldown_ticks;
			}
			if (cooldown != NONE && cast_cooldown_ticks)
			{
				*cast_cooldown_ticks = cooldown;
			}
		}
		else
		{
			if (mana_cost)
				*mana_cost = 0;
			if (mana_cost_per_second)
				*mana_cost_per_second = 0.f;
			if (capacity_reserved)
				*capacity_reserved = 0;
			if (cast_cooldown_ticks)
				*cast_cooldown_ticks = 0;
			if (spell_cooldown_ticks)
				*spell_cooldown_ticks = 0;
		}
	}

	return;
}

/* 0x27CCB0 */
real shadowrun_unit_mana_capacity(
	long unit_index)
{
	struct shadowrun_race *race = shadowrun_unit_race_get(unit_index);
	struct unit_datum *unit = unit_try_and_get(unit_index);
	real bonus = 0.f;

	if (unit)
	{
		struct shadowrun_player *player = shadowrun_player_get(unit->unit.player_index);

		if (player)
		{
			bonus = (real)player->mana_capacity_bonus;
		}
	}

	return race ? (real)race->mana_capacity + bonus : bonus;
}

/* 0x27CD40 */
real shadowrun_unit_mana_regeneration(
	long unit_index)
{
	struct shadowrun_race *race = shadowrun_unit_race_get(unit_index);

	return race ? race->mana_regeneration_per_second : 0.f;
}

/* 0x27CD70 */
real shadowrun_unit_mana_regeneration_delay(
	long unit_index)
{
	struct shadowrun_race *race = shadowrun_unit_race_get(unit_index);

	return race ? race->mana_regeneration_delay : 0.f;
}

/* 0x27CFD0 */
boolean shadowrun_unit_has_mana(
	long unit_index,
	real mana)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	return shadowrun_unit && shadowrun_unit->mana - mana >= 0.f;
}

/* 0x27CEE0 */
boolean shadowrun_unit_spend_mana(
	long unit_index,
	real mana,
	boolean take_all,
	boolean keep_waiting)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean result = FALSE;

	if (!unit || !shadowrun_unit)
	{
		result = FALSE;
	}
	else if (mana == 0.f)
	{
		result = TRUE;
	}
	else if (unit->unit.actor_index == NONE)
	{
		if (player_try_and_get(unit->unit.player_index))
		{
			if (shadowrun_unit->mana - mana >= 0.f)
			{
				shadowrun_unit->mana -= mana;
				if (!keep_waiting)
				{
					shadowrun_unit->mana_regeneration_ticks = 0;
				}
				result = TRUE;
			}
			else if (take_all)
			{
				shadowrun_unit->mana = 0.f;
			}
		}
	}
	else
	{
		result = TRUE;
	}

	return result;
}

/* 0x27D2A0 */
boolean shadowrun_unit_can_reserve_capacity(
	long unit_index,
	long capacity)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean result = TRUE;

	if (unit && shadowrun_unit && unit->unit.actor_index == NONE)
	{
		result = shadowrun_unit_mana_capacity(unit_index) -
			((real)shadowrun_unit->tech_capacity_reserved + (real)shadowrun_unit->magic_capacity_reserved +
			(real)capacity) >= 0.f;
	}

	return result;
}

/* 0x27D220 */
boolean shadowrun_unit_reserve_capacity(
	long unit_index,
	long capacity)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean result = TRUE;

	if (unit && shadowrun_unit && unit->unit.actor_index == NONE)
	{
		result = shadowrun_unit_can_reserve_capacity(unit_index, capacity);
		if (result)
		{
			shadowrun_unit->magic_capacity_reserved += capacity;
		}
	}

	return result;
}

/* 0x27D310 */
void shadowrun_unit_release_capacity(
	long unit_index,
	long capacity)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (unit && shadowrun_unit && unit->unit.actor_index == NONE)
	{
		shadowrun_unit->magic_capacity_reserved -= capacity;
	}

	return;
}

short shadowrun_unit_local_player_index(
	long unit_index)
{
	long player_index = player_index_from_unit_index(unit_index);

	return player_index != NONE ? player_get(player_index)->local_player_index : NONE;
}

void shadowrun_unit_cast_failed(
	long unit_index,
	short spell)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	shadowrun_unit_cast_failed_sound(unit_index);
	if (shadowrun_unit && spell >= 0 && spell < NUMBER_OF_SHADOWRUN_SPELLS)
	{
		shadowrun_unit->spell_failed[spell] = TRUE;
	}

	return;
}

void shadowrun_unit_cast_failed_sound(
	long unit_index)
{
	if (shadowrun_unit_local_player_index(unit_index) != NONE)
	{
		struct shadowrun_buy_menu_definition *buy_menu = shadowrun_buy_menu_get(unit_index);

		if (buy_menu && buy_menu->cast_failed_sound.index != NONE)
		{
			unspatialized_impulse_sound_new(buy_menu->cast_failed_sound.index, 1.f);
		}
	}

	return;
}

/* 0x26AEC0 */
void shadowrun_unit_cast_feedback(
	long unit_index,
	long damage_definition_index,
	long effect_definition_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);

	if (unit)
	{
		if (damage_definition_index != NONE)
		{
			struct damage_data damage;

			damage_data_new(&damage, damage_definition_index);
			SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
			negate_vector3d(&unit->unit.aiming_vector, &damage.direction);
			object_cause_damage(&damage, unit_index, NONE, NONE, NONE, NULL);
		}
		if (effect_definition_index != NONE)
		{
			effect_new_from_object(effect_definition_index, unit_index, unit_index, NONE, 0.f, 0.f, NULL, NULL);
		}
	}

	return;
}

/* each tick (0x2604A0, unit_update's): mana, the spells and the waits */
void shadowrun_unit_update(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit)
	{
		short index;

		if (shadowrun_unit->undead)
		{
			shadowrun_undead_update(unit_index);
		}
		if (shadowrun_unit->summoned)
		{
			shadowrun_summoned_update(unit_index);
		}
		shadowrun_unit_mana_update(unit_index);
		shadowrun_unit_regeneration_update(unit_index);
		shadowrun_unit_rage_update(unit_index);

		if (unit->object.type == _object_type_biped)
		{
			/* a player's right bumper casts its spell, if it has one: it
			comes as the flashlight's flag, which on these maps lights
			nothing (player_control.c). (The executable cast from slots its
			radial menu assigned to buttons) */
			if (unit->unit.player_index != NONE &&
				TEST_FLAG(unit->unit.control_flags, _unit_control_integrated_light_bit))
			{
				SET_FLAG(unit->unit.control_flags, _unit_control_integrated_light_bit, FALSE);
				if (shadowrun_unit->current_spell != 0)
				{
					shadowrun_spell_cast(unit_index, (short)(shadowrun_unit->current_spell - 1));
				}
			}
			/* port: a player's left bumper uses its tech (the crouch control,
			which on these maps does not crouch: port/linux/game/shadowrun_tech.c) */
			if (unit->unit.player_index != NONE &&
				TEST_FLAG(unit->unit.control_flags, _unit_control_crouch_modifier_bit))
			{
				SET_FLAG(unit->unit.control_flags, _unit_control_crouch_modifier_bit, FALSE);
				shadowrun_tech_cast(unit_index);
			}
			shadowrun_unit->teleported = FALSE;
			shadowrun_spells_update(unit_index);
			shadowrun_unit_head_update(unit_index);
		}

		if (shadowrun_unit->cast_cooldown_ticks > 0)
		{
			shadowrun_unit->cast_cooldown_ticks--;
		}
		for (index = 0; index < MAXIMUM_SHADOWRUN_POWERS; index++)
		{
			if (shadowrun_unit->power_cooldown_ticks[index] > 0)
			{
				shadowrun_unit->power_cooldown_ticks[index]--;
			}
		}
	}

	return;
}

/* 0x27C180: the unit takes the race (its srud's race of that index, else its
first): its scale, its head, its body's vitality (a player's), and a full
pool. Its player keeps the race for the units it spawns with. One raised by
Resurrection takes the first race whatever is asked */
void shadowrun_unit_set_race(
	long unit_index,
	long race_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct shadowrun_unit_definition *definition = shadowrun_unit_definition_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (unit && definition && shadowrun_unit && unit->object.type == _object_type_biped)
	{
		struct shadowrun_race *race = NULL;

		if (race_index >= 0 && race_index < definition->races.count)
		{
			race = tag_block_get_element_with_size(&definition->races, race_index, sizeof(struct shadowrun_race));
		}
		if (!race && definition->races.count > 0)
		{
			race = tag_block_get_element_with_size(&definition->races, 0, sizeof(struct shadowrun_race));
		}
		if (race)
		{
			struct shadowrun_player *shadowrun_player = shadowrun_player_get(unit->unit.player_index);

			if (shadowrun_player)
			{
				shadowrun_player->race_index = race_index;
			}
			shadowrun_unit->mana = shadowrun_unit_mana_capacity(unit_index) -
				(real)shadowrun_unit->tech_capacity_reserved - (real)shadowrun_unit->magic_capacity_reserved;
			shadowrun_unit->race_index = shadowrun_unit->undead ? 0 : race_index;
			unit->object.scale = race->scale;
			shadowrun_unit->race_scale = race->scale;
			shadowrun_unit->head_definition_index = race->head.index;
			shadowrun_unit_new_head(unit_index);
			if (unit->unit.actor_index == NONE)
			{
				unit->object.maximum_body_vitality = race->maximum_body_vitality;
			}
		}
	}

	return;
}

/* 0x0F6480's (a player's new unit): it takes the player's race and the
spells the player knows, and starts with a full pool */
void shadowrun_player_spawned(
	long player_index)
{
	struct player_datum *player = player_get(player_index);
	struct shadowrun_player *shadowrun_player = shadowrun_player_get(player_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(player->unit_index);

	if (shadowrun_player && shadowrun_unit)
	{
		short power;

		shadowrun_buy_menu_player_spawned(player_index);
		shadowrun_unit_set_race(player->unit_index, shadowrun_player->race_index);
		/* port: the tech the player knows (shadowrun_tech.c) */
		shadowrun_unit_set_tech(player->unit_index, (short)shadowrun_player->known_tech);
		for (power = 0; power < MAXIMUM_SHADOWRUN_POWERS; power++)
		{
			if (shadowrun_player->known_powers[power] != 0)
			{
				shadowrun_unit_learn_power(player->unit_index, power);
			}
		}
		shadowrun_unit->mana = shadowrun_unit_mana_capacity(player->unit_index) -
			(real)shadowrun_unit->tech_capacity_reserved;
		/* port: the spell the scripts gave the player is its new unit's
		(the executable's player chose it again from its radial menu) */
		if (shadowrun_player->magic_assigned)
		{
			shadowrun_unit->current_spell = shadowrun_player->assigned_spell;
			shadowrun_unit->desired_spell = shadowrun_player->assigned_spell;
		}
	}

	return;
}

/* 0x27D3A0: the unit learns the power (magic, or tech); its player keeps it */
boolean shadowrun_unit_learn_power(
	long unit_index,
	short power)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean result = FALSE;

	if (unit && shadowrun_unit && shadowrun_unit_definition_get(unit_index) &&
		power >= 0 && power < MAXIMUM_SHADOWRUN_POWERS &&
		shadowrun_unit->known_powers[power] != 1)
	{
		struct shadowrun_player *shadowrun_player = shadowrun_player_get(unit->unit.player_index);

		shadowrun_unit->known_powers[power] = 1;
		shadowrun_unit->known_power_count++;
		if (shadowrun_player)
		{
			shadowrun_player->known_powers[power] = 1;
		}
		result = TRUE;
	}

	return result;
}

/* the unit died for good (unit_died): what its spells leave (0x27C350) */
void shadowrun_unit_died(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (unit && shadowrun_unit && unit->unit.feign_death_timer == 0)
	{
		shadowrun_spells_unit_died(unit_index);
		shadowrun_unit_delete_head(unit_index);
		shadowrun_spirit_animal_died(unit_index);
		shadowrun_unit_tech_died(unit_index);
	}

	return;
}

/* the unit deleted (unit_delete): its head and its resurrection ball go too */
void shadowrun_unit_delete(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit)
	{
		shadowrun_unit_tech_delete(unit_index);
		shadowrun_unit_delete_head(unit_index);
		if (shadowrun_unit->resurrection_ball_index != NONE)
		{
			if (object_try_and_get_and_verify_type(shadowrun_unit->resurrection_ball_index, NONE))
			{
				object_delete(shadowrun_unit->resurrection_ball_index);
			}
			shadowrun_unit->resurrection_ball_index = NONE;
		}
	}

	return;
}

/* port: the d-pad's right (on these maps) chooses the spell the right bumper
casts: each spell the unit knows in turn, then its grenades (rotated as they
are otherwise, with no spell to cast), then its first spell again. The
executable's player chose them
from a radial menu, its own HUD's (the globals' assignment scheme). Returns
whether the control was taken; a unit that knows no spell rotates its
grenades as ever */
boolean shadowrun_unit_rotate_selection(
	long unit_index,
	short desired_grenade_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean result = FALSE;

	if (shadowrun_unit && shadowrun_unit->known_power_count > 0)
	{
		short first_spell = NONE;
		short next_spell = NONE;
		short spell;

		for (spell = 0; spell < NUMBER_OF_SHADOWRUN_SPELLS; spell++)
		{
			if (shadowrun_unit->known_powers[spell] != 0)
			{
				if (first_spell == NONE)
				{
					first_spell = spell;
				}
				if (next_spell == NONE && spell > shadowrun_unit->current_spell - 1 && shadowrun_unit->current_spell != 0)
				{
					next_spell = spell;
				}
			}
		}
		if (first_spell != NONE)
		{
			if (shadowrun_unit->current_spell != 0)
			{
				/* the next spell, else the grenades (or the first spell, without
				any) */
				if (next_spell != NONE)
				{
					shadowrun_unit->current_spell = next_spell + 1;
				}
				else if (unit_inventory_next_grenade(unit_index, NONE, 1) != NONE)
				{
					shadowrun_unit->current_spell = 0;
				}
				else
				{
					shadowrun_unit->current_spell = first_spell + 1;
				}
				result = TRUE;
			}
			else
			{
				short next_grenade = unit_inventory_next_grenade(unit_index, desired_grenade_index, 1);

				/* the grenades rotate, until they come round */
				if (desired_grenade_index == NONE || next_grenade == NONE || next_grenade <= desired_grenade_index)
				{
					shadowrun_unit->current_spell = first_spell + 1;
					result = TRUE;
				}
			}
			shadowrun_unit->desired_spell = shadowrun_unit->current_spell;
		}
	}

	return result;
}

/* 0x11E350 */
void shadowrun_players_set_magic(
	short spell)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(player->unit_index);
		struct shadowrun_player *shadowrun_player = shadowrun_player_get(iterator.datum_index);

		if (shadowrun_unit && shadowrun_player)
		{
			shadowrun_unit->current_spell = spell + 1;
			shadowrun_unit->desired_spell = spell + 1;
			shadowrun_player->magic_assigned = TRUE;
			shadowrun_player->assigned_spell = spell + 1;
		}
	}

	return;
}

/* 0x11E3E0 */
void shadowrun_players_change_magic(
	short spell)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		/* (the executable's asserts on a player without a unit) */
		shadowrun_unit_learn_power(player->unit_index, spell);
	}

	return;
}

/* 0x10E2E0: how magic and tech are given to the controls (the globals'
Shadowrun block, which the buy menu's assignment will read) */
void shadowrun_game_set_assignment_scheme(
	short scheme)
{
	/* (the buy menu is not played yet: nothing reads it) */
	(void)scheme;

	return;
}

/* ---------- private code */

static struct tag_reference *shadowrun_biped_unit_definition_reference(
	struct biped_definition *definition)
{
	return (struct tag_reference *)((byte *)definition + SHADOWRUN_BIPED_UNIT_DEFINITION_OFFSET);
}

static struct tag_reference *shadowrun_weapon_data_reference(
	struct weapon_definition *definition)
{
	return (struct tag_reference *)((byte *)definition + SHADOWRUN_WEAPON_DATA_OFFSET);
}

/* 0x2604A0's: mana comes back, after a wait, up to what the capacity has
free */
static void shadowrun_unit_mana_update(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit && shadowrun_unit_definition_get(unit_index))
	{
		real capacity = shadowrun_unit_mana_capacity(unit_index);
		real regeneration = shadowrun_unit_mana_regeneration(unit_index);
		real delay = shadowrun_unit_mana_regeneration_delay(unit_index);

		if ((real)shadowrun_unit->mana_regeneration_ticks <= delay * 30.f)
		{
			shadowrun_unit->mana_regeneration_ticks++;
		}
		else
		{
			real free_capacity = capacity -
				((real)shadowrun_unit->tech_capacity_reserved + (real)shadowrun_unit->magic_capacity_reserved);

			shadowrun_unit->mana += regeneration / 30.f;
			if (free_capacity < shadowrun_unit->mana)
			{
				shadowrun_unit->mana = PIN(free_capacity, 0.f, capacity);
			}
		}
	}

	return;
}

/* 0x27BDD0: the race's head, scenery the unit wears (made anew) */
static void shadowrun_unit_new_head(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	shadowrun_unit_delete_head(unit_index);
	if (shadowrun_unit->head_definition_index != NONE)
	{
		struct object_placement_data placement;

		object_placement_data_new(&placement, shadowrun_unit->head_definition_index, unit_index);
		placement.position = unit->object.position;
		placement.forward = unit->object.forward;
		placement.up = unit->object.up;
		shadowrun_unit->head_object_index = object_new(&placement);
	}

	return;
}

static void shadowrun_unit_delete_head(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit && shadowrun_unit->head_object_index != NONE)
	{
		if (object_try_and_get_and_verify_type(shadowrun_unit->head_object_index, NONE))
		{
			object_delete(shadowrun_unit->head_object_index);
		}
		shadowrun_unit->head_object_index = NONE;
	}

	return;
}

/* the head follows the unit's head marker (as 0x11A360 places a player's
and 0x55DD0 an actor's, each tick) */
static void shadowrun_unit_head_update(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit->head_object_index != NONE)
	{
		struct object_marker markers[1];

		if (!object_try_and_get_and_verify_type(shadowrun_unit->head_object_index, NONE))
		{
			shadowrun_unit->head_object_index = NONE;
		}
		else if (object_get_marker_by_name(unit_index, "head", markers, NUMBEROF(markers)) > 0)
		{
			object_set_position(shadowrun_unit->head_object_index, &markers[0].matrix.position,
				&markers[0].matrix.forward, &markers[0].matrix.up);
		}
	}

	return;
}
