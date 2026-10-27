/*
SHADOWRUN_MAGIC.C

The Shadowrun prototype's spells (shadowrun_units.h): whether a unit can cast
one, casting it, and what each does while it lasts.

Ported from the prototype's executable (cachesym.xbe, build 01.12.07.0132);
each function names the one it is (its address in the executable). Its bugs
are kept, each marked BUG (original, preserved).

Played: teleport and gust. The other spells (tree of life, resurrection,
strangle, summon elemental, spirit animal, smoke) fail as if the unit had not
the mana for them, until they are.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "ai/ai.h"
#include "ai/actors.h"
#include "ai/ai_script.h"
#include "ai/actor_placement.h"
#include "ai/ai_scenario_definitions.h"
#include "ai/encounters.h"
#include "cache/cache_files.h"
#include "tag_files/tag_files.h"
#include "effects/effects.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "math/real_math.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "physics/collisions.h"
#include "saved games/game_state.h"
#include "scenario/scenario.h"
#include "sound/game_sound.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/vehicle_datum.h"
#include "units/vehicle_definitions.h"
#include "items/projectile_definitions.h"
#include "units/units.h"
#include "shadowrun_units.h"
#include "shadowrun_tech.h" /* port: the techs, and grapple's pull */

/* ---------- constants */

enum
{
	/* the places around its destination a teleport tries, in its frame
	(the executable's, at 0x4BE300) */
	NUMBER_OF_TELEPORT_OFFSETS = 45,

	/* the fires of firewalls burning at once */
	MAXIMUM_SHADOWRUN_FIRES = 64,

	/* the map's projectiles with a spell's flags kept */
	MAXIMUM_SHADOWRUN_SPELL_PROJECTILES = 32,

	/* the AI's sound volumes (ai_handle_unit_effect) */
	SHADOWRUN_AI_SOUND_VOLUME_COUNT = 5,

	/* where the globals keep Shadowrun's block (in what this build's
	globals have as unused) and in it the sound of a strangling broken */
	SHADOWRUN_GLOBALS_BLOCK_OFFSET = 0xEC,
	SHADOWRUN_GLOBALS_STRANGLE_BROKEN_SOUND_OFFSET = 0x15C,
	SHADOWRUN_GLOBALS_UNDEAD_COLOR_OFFSET = 0xE0,
};

/* a teleport's destination is free of the structure and water */
#define TELEPORT_COLLISION_FLAGS \
	(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) | \
	FLAG(_collision_test_media_bit))
#define TELEPORT_OFFSET_SCALE 0.6f
/* the room a teleported unit needs above its destination */
#define TELEPORT_CLEARANCE_HEIGHT 0.35f
#define TELEPORT_CLEARANCE_RADIUS 0.1f

/* how fast a unit must move for a teleport to go its way, and how far its
controls must be pushed for it to go theirs */
#define TELEPORT_MINIMUM_SPEED 0.0003f
#define TELEPORT_MINIMUM_THROTTLE_SQUARED 0.3f

/* a gust starts before the caster's head, and bounces back onto it from a
wall this near */
#define GUST_ORIGIN_OFFSET 0.1f
#define GUST_SELF_DISTANCE 2.f
#define GUST_LIFT 0.45f

/* resurrection's ball starts this small, just above the ground; an actor
casting it has this much shield; the bodies it may raise at once; and the
ticks a raised body is settled for as it is made */
#define RESURRECTION_BALL_STARTING_SCALE 0.0001f
#define RESURRECTION_BALL_LIFT 0.001f
#define RESURRECTION_ACTOR_SHIELD_VITALITY 0.25f
#define RESURRECTION_MAXIMUM_BODIES 16
#define RESURRECTION_SETTLING_TICKS 30

/* summon elemental's aim starts before the caster's head; cast again this
soon after, it fails rather than dismisses */
#define SUMMON_ORIGIN_OFFSET 0.1f

/* firewall: its fires look for surfaces as strangle's thorns do, but for
objects; a fire stands just above its surface; the most it hurts at once;
and the raised take ten times its damage */
#define FIREWALL_COLLISION_FLAGS \
	(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) | \
	FLAG(_collision_test_media_bit))
#define FIREWALL_LIFT 0.1f
#define FIREWALL_MAXIMUM_VICTIMS 64
#define FIREWALL_UNDEAD_DAMAGE_SCALE 10.f

/* port: the actor variant Summon Elemental summons, the elite commander with
the energy sword (the prototype's elemental is an elite:
docs/shadowrun_caches.md), on any map that has it; else the map's first
variant of a name starting so */
/* (a tag's name has no group's extension: tag_get_name gives
"characters\elemental\elite commander\elite commander energy sword", which is
what these are compared with) */
#define SHADOWRUN_SUMMONED_ACTOR_VARIANT "characters\\elemental\\elite commander\\elite commander energy sword"
#define SHADOWRUN_SUMMONED_ACTOR_VARIANT_PREFIX "elite commander"

/* port: the actor variant the Hologram tech makes: its hologram with the
assault rifle (shadowrun_tech.c). No prefix search: this is the whole name */
#define SHADOWRUN_HOLOGRAM_ACTOR_VARIANT "characters\\hologram\\hologram assault rifle"
/* (the actor variant group's: ACTOR_VARIANT_DEFINITION_TAG) */
#define SHADOWRUN_ACTOR_VARIANT_TAG 'actv'

/* the Shadowrun projectile flags, where this build's definitions have
three of their own (cleared on the prototype's maps: its build had them not):
detonating, the projectile brings about the owner's anti-magic, its tree of
life, or its fire */
#define SHADOWRUN_PROJECTILE_ANTI_MAGIC_FLAG 0x10UL
#define SHADOWRUN_PROJECTILE_TREE_OF_LIFE_FLAG 0x20UL
#define SHADOWRUN_PROJECTILE_FIRE_FLAG 0x40UL
#define SHADOWRUN_PROJECTILE_FLAGS \
	(SHADOWRUN_PROJECTILE_ANTI_MAGIC_FLAG | SHADOWRUN_PROJECTILE_TREE_OF_LIFE_FLAG | SHADOWRUN_PROJECTILE_FIRE_FLAG)

/* tree of life: the grenade the caster throws (the globals' grenades' fourth),
an actor's aim starting before its head, a tree's starting scale (a tenth of
its own, at least this), a tree's heal's damage and effect each second, and
an actor healed thrice as fast */
#define TREE_OF_LIFE_GRENADE_INDEX 3
#define TREE_OF_LIFE_ORIGIN_OFFSET 0.1f
#define TREE_OF_LIFE_STARTING_SCALE 0.1f
#define TREE_OF_LIFE_HEAL_FEEDBACK_TICKS 30
#define TREE_OF_LIFE_ACTOR_HEAL_SCALE 3.f

/* smoke: its effect is in the caster's third change colour (cast too soon
after it ended, SMOKE_RECAST_WAIT, it fails) */
#define SMOKE_EFFECT_CHANGE_COLOR 2

/* spirit animal: the unit grows by this much of its scale over a second, and
on its death becomes the definition's biped, this large */
#define SPIRIT_ANIMAL_GROWTH 0.7f
#define SPIRIT_ANIMAL_GROWTH_TICKS 30
#define SPIRIT_ANIMAL_DEATH_SCALE 1.7f
#define SPIRIT_ANIMAL_DEATH_SETTLING_TICKS 3

/* strangle's aim starts before the caster's head; its ten places; a thorn
starts at a tenth of its scale (at least that), and is never smaller than
the least; and its turn's bound (the executable's, 3.14) */
#define STRANGLE_ORIGIN_OFFSET 0.1f
#define STRANGLE_PLACES 10
#define STRANGLE_STARTING_SCALE 0.1f
#define STRANGLE_MINIMUM_SCALE 0.01f
#define STRANGLE_PI 3.14f

/* summon elemental: cast again this soon after, it dismisses its summons */
#define SUMMON_DISMISS_TICKS 30

/* how far from where it was an actor's teleport hides it until it arrives */
#define TELEPORT_HIDING_COORDINATE 32767.f

static real_vector3d const teleport_offsets[NUMBER_OF_TELEPORT_OFFSETS] =
{
	{ { 1.f, 0.f, 0.f } }, { { 1.f, -1.f, 0.f } }, { { 1.f, 1.f, 0.f } },
	{ { 1.f, 0.f, 1.f } }, { { 1.f, -1.f, 1.f } }, { { 1.f, 1.f, 1.f } },
	{ { 1.f, 0.f, -1.f } }, { { 1.f, -1.f, -1.f } }, { { 1.f, 1.f, -1.f } },
	{ { 0.5f, 0.f, 0.f } }, { { 0.5f, -1.f, 0.f } }, { { 0.5f, 1.f, 0.f } },
	{ { 0.5f, 0.f, 1.f } }, { { 0.5f, -1.f, 1.f } }, { { 0.5f, 1.f, 1.f } },
	{ { 0.5f, 0.f, -1.f } }, { { 0.5f, -1.f, -1.f } }, { { 0.5f, 1.f, -1.f } },
	{ { 0.f, 0.f, 0.f } }, { { 0.f, -1.f, 0.f } }, { { 0.f, 1.f, 0.f } },
	{ { 0.f, 0.f, 1.f } }, { { 0.f, -1.f, 1.f } }, { { 0.f, 1.f, 1.f } },
	{ { 0.f, 0.f, -1.f } }, { { 0.f, -1.f, -1.f } }, { { 0.f, 1.f, -1.f } },
	{ { -0.5f, 0.f, 0.f } }, { { -0.5f, -1.f, 0.f } }, { { -0.5f, 1.f, 0.f } },
	{ { -0.5f, 0.f, 1.f } }, { { -0.5f, -1.f, 1.f } }, { { -0.5f, 1.f, 1.f } },
	{ { -0.5f, 0.f, -1.f } }, { { -0.5f, -1.f, -1.f } }, { { -0.5f, 1.f, -1.f } },
	{ { -1.f, 0.f, 0.f } }, { { -1.f, -1.f, 0.f } }, { { -1.f, 1.f, 0.f } },
	{ { -1.f, 0.f, 1.f } }, { { -1.f, -1.f, 1.f } }, { { -1.f, 1.f, 1.f } },
	{ { -1.f, 0.f, -1.f } }, { { -1.f, -1.f, -1.f } }, { { -1.f, 1.f, -1.f } },
};

/* ---------- globals */

/* strangle's thorns, oldest first (the executable's g_strangleChain), in the
game state */
/* a tree of life standing (the executable's g_tree_of_life_chain): it heals
so much a tick, so far, and its heal's damage and effect come each second */
struct shadowrun_standing_tree
{
	long object_index;
	real heal_rate;
	real heal_radius;
	char heal_feedback_ticks;
	byte pad[3];
};

/* a fire of a firewall burning (the executable's g_FireChain): till its
time, dealing its damage to those of the other sides in its reach over the
definition's ticks (every other tick) */
struct shadowrun_fire
{
	long object_index;
	long end_time;
	long wait_ticks;
	long damage_definition_index;
	real damage_ticks;
	real damage_radius;
	short owner_team_index;
	short pad;
};

static struct shadowrun_magic_globals
{
	long thorn_indices[MAXIMUM_SHADOWRUN_THORNS];
	long thorn_count;
	struct shadowrun_standing_tree trees[MAXIMUM_SHADOWRUN_STANDING_TREES];
	long tree_count;
	struct shadowrun_fire fires[MAXIMUM_SHADOWRUN_FIRES];
	long fire_count;
} *shadowrun_magic_globals;

/* the map's projectiles with a spell's flags (shadowrun_projectiles_loaded:
not in the game state, as they are the map's) */
static struct
{
	long definition_indices[MAXIMUM_SHADOWRUN_SPELL_PROJECTILES];
	unsigned long flags[MAXIMUM_SHADOWRUN_SPELL_PROJECTILES];
	short count;
} shadowrun_spell_projectiles;

/* ---------- private prototypes */

static void shadowrun_spell_spend(
	long unit_index,
	short spell);
static void shadowrun_teleport_cast(
	long unit_index,
	real_point3d const *target,
	real delay);
static void shadowrun_teleport_update(
	long unit_index);
static void shadowrun_gust_cast(
	long unit_index);
static boolean shadowrun_resurrection_cast(
	long unit_index);
static void shadowrun_summon_cast(
	long unit_index);
static void shadowrun_strangle_cast(
	long unit_index,
	real_point3d const *target);
static void shadowrun_firewall_cast(
	long unit_index,
	real_point3d const *target);
static void shadowrun_fires_update(
	void);
static void shadowrun_fire_burn(
	long object_index,
	struct shadowrun_fire *fire);
static boolean shadowrun_tree_of_life_castable(
	long unit_index);
static void shadowrun_tree_of_life_throw(
	long unit_index);
static boolean shadowrun_tree_of_life_grow(
	long unit_index,
	real_point3d const *position);
static void shadowrun_trees_update(
	long unit_index);
static void shadowrun_trees_heal(
	void);
static void shadowrun_tree_heal_unit(
	long unit_index,
	struct shadowrun_standing_tree *tree);
static void shadowrun_smoke_cast(
	long unit_index);
static void shadowrun_smoke_update(
	long unit_index);
static void shadowrun_smoke_end(
	long unit_index);
static void shadowrun_spirit_animal_cast(
	long unit_index);
static void shadowrun_spirit_animal_update(
	long unit_index);
static void shadowrun_spirit_animal_end(
	long unit_index);
static void shadowrun_unit_stop_spell_effect(
	long unit_index);
static long shadowrun_strangle_new_thorn(
	long unit_index,
	long definition_index,
	real_point3d const *position,
	real_vector3d const *up);
static void shadowrun_strangle_cull_thorns(
	long count,
	long maximum_count);
static void shadowrun_strangle_update(
	long unit_index);
static void shadowrun_summons_update(
	long unit_index);
static long shadowrun_summon_elemental_variant(
	void);
static long shadowrun_summon_elemental_place(
	long variant,
	long unit_index,
	real_point3d const *position);
static void shadowrun_summons_release(
	long unit_index,
	boolean kill);
static void shadowrun_resurrection_update(
	long unit_index);
static void shadowrun_resurrection_raise(
	long unit_index,
	real_point3d const *position,
	long race_index);
static void shadowrun_resurrection_release(
	long unit_index);
static void shadowrun_undead_damage(
	long unit_index,
	long damage_definition_index);
static long shadowrun_unit_fire_projectile(
	long unit_index,
	long projectile_definition_index,
	real_point3d const *target);
static void shadowrun_biped_push(
	long biped_index,
	real_vector3d *impulse,
	boolean face);
static void shadowrun_vehicle_push(
	long vehicle_index,
	real_vector3d const *impulse);
static void shadowrun_unit_end_tech_movement(
	long unit_index);
static void shadowrun_unit_damage_self(
	long unit_index,
	long damage_definition_index,
	real_vector3d const *direction);
static void shadowrun_unit_effect(
	long unit_index,
	long effect_definition_index);
static long shadowrun_globals_strangle_broken_sound(
	void);
static real_rgb_color shadowrun_globals_undead_color(
	void);

void biped_port_snap_facing(long biped_index); /* port: units/bipeds.c */
void object_port_update(long object_index); /* port: objects/objects.c */
void object_port_check_body(long object_index); /* port: objects/damage.c */

/* port: the map's actor variant Summon Elemental summons, found once a map
(shadowrun_summon_elemental_variant); none, or not found yet (NONE, not 0:
a static starts at 0, which is a tag's index) */
static long shadowrun_summoned_variant = NONE;
/* ... and the Hologram tech's */
static long shadowrun_hologram_variant_index = NONE;

/* ---------- public code */

void shadowrun_magic_initialize(
	void)
{
	shadowrun_magic_globals = game_state_malloc("shadowrun magic", NULL, sizeof(*shadowrun_magic_globals));

	return;
}

void shadowrun_magic_initialize_for_new_map(
	void)
{
	csmemset(shadowrun_magic_globals, 0, sizeof(*shadowrun_magic_globals));
	/* port: looked for in this map's tags */
	shadowrun_summoned_variant = NONE;
	shadowrun_hologram_variant_index = NONE;

	return;
}

void shadowrun_magic_update(
	void)
{
	shadowrun_trees_heal();
	shadowrun_fires_update();

	return;
}

void shadowrun_projectiles_loaded(
	void)
{
	struct tag_iterator iterator;
	long definition_index;

	csmemset(&shadowrun_spell_projectiles, 0, sizeof(shadowrun_spell_projectiles));
	tag_iterator_new(&iterator, PROJECTILE_DEFINITION_TAG);
	while ((definition_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct projectile_definition *definition = tag_get(PROJECTILE_DEFINITION_TAG, definition_index);
		unsigned long flags = definition->projectile.flags & SHADOWRUN_PROJECTILE_FLAGS;

		if (flags)
		{
			if (shadowrun_spell_projectiles.count < MAXIMUM_SHADOWRUN_SPELL_PROJECTILES)
			{
				shadowrun_spell_projectiles.definition_indices[shadowrun_spell_projectiles.count] = definition_index;
				shadowrun_spell_projectiles.flags[shadowrun_spell_projectiles.count] = flags;
				shadowrun_spell_projectiles.count++;
			}
			definition->projectile.flags &= ~SHADOWRUN_PROJECTILE_FLAGS;
		}
	}

	return;
}

/* 0x170DF0's (a projectile detonating): its owner's tree of life grows, or
its firewall burns, where it is (the anti-magic flag's is tech's, not played
yet) */
void shadowrun_projectile_detonated(
	long projectile_index,
	real_point3d const *position)
{
	struct object_datum *projectile = object_get(projectile_index);
	unsigned long flags = 0;
	short index;

	for (index = 0; index < shadowrun_spell_projectiles.count; index++)
	{
		if (shadowrun_spell_projectiles.definition_indices[index] == projectile->definition_index)
		{
			flags = shadowrun_spell_projectiles.flags[index];
		}
	}
	/* port: a grapple's projectile pulls its caster to where it landed
	(tech, port/linux/game/shadowrun_tech.c) */
	if (shadowrun_grapple_projectile_is(projectile_index))
	{
		shadowrun_grapple_pull(projectile_index, position);
	}
	/* (the executable gives one without an owner a local player's unit, and
	waits for one: here it has none) */
	if (flags && projectile->object.owner_object_index != NONE)
	{
		struct shadowrun_unit *owner = shadowrun_unit_get(projectile->object.owner_object_index);

		if (owner && biped_try_and_get(projectile->object.owner_object_index))
		{
			/* port: the anti-magic grenade makes its zone where it detonates, on
			the thrower's side (port/linux/game/shadowrun_tech.c) */
			if (flags & SHADOWRUN_PROJECTILE_ANTI_MAGIC_FLAG)
			{
				shadowrun_antimagic_zone(projectile->object.owner_object_index, position);
			}
			if (flags & SHADOWRUN_PROJECTILE_TREE_OF_LIFE_FLAG)
			{
				owner->cast_cooldown_ticks = 0;
				shadowrun_tree_of_life_grow(projectile->object.owner_object_index, position);
			}
			if (flags & SHADOWRUN_PROJECTILE_FIRE_FLAG)
			{
				owner->cast_cooldown_ticks = 0;
				shadowrun_firewall_cast(projectile->object.owner_object_index, position);
			}
		}
	}

	return;
}

boolean shadowrun_damage_allowed(
	long owner_object_index,
	long object_index)
{
	boolean result = TRUE;

	if (shadowrun_map_loaded())
	{
		struct shadowrun_unit *owner = shadowrun_unit_get(owner_object_index);
		long root_index = object_index;
		struct shadowrun_unit *victim;

		while (root_index != NONE && object_get(root_index)->object.parent_object_index != NONE)
		{
			root_index = object_get(root_index)->object.parent_object_index;
		}
		victim = shadowrun_unit_get(root_index);
		if (owner && owner->smoke_hidden)
		{
			result = FALSE;
		}
		/* 0x268F50 */
		if (victim)
		{
			struct shadowrun_magic3_definition *magic = shadowrun_magic3_get(root_index);

			if (magic && victim->smoke_active &&
				unit_get(root_index)->unit.active_camouflage == magic->smoke_camouflage)
			{
				result = FALSE;
			}
			if (magic && victim->current_spell == _shadowrun_spell_spirit_animal + 1 && victim->cast_cooldown_ticks > 0)
			{
				result = FALSE;
			}
		}
	}

	return result;
}

/* 0x2604A0's: with smoke's definition, the unit's camouflage comes over the
definition's time up to its camouflage, and goes over its own */
boolean shadowrun_unit_camouflage_update(
	long unit_index)
{
	struct shadowrun_magic3_definition *magic = shadowrun_map_loaded() ? shadowrun_magic3_get(unit_index) : NULL;
	boolean result = FALSE;

	if (magic)
	{
		struct unit_datum *unit = unit_get(unit_index);

		if (TEST_FLAG(unit->unit.flags, _unit_active_camouflaged_bit))
		{
			unit->unit.active_camouflage += 1.f / (magic->smoke_camouflage_time * (real)TICKS_PER_SECOND);
			if (unit->unit.active_camouflage > magic->smoke_camouflage)
			{
				unit->unit.active_camouflage = magic->smoke_camouflage;
				unit->unit.cause_for_camo_regrowth = 0;
			}
		}
		else if (unit->unit.active_camouflage > 0.f)
		{
			unit->unit.active_camouflage -= 1.f / (magic->smoke_uncamouflage_time * (real)TICKS_PER_SECOND);
			if (unit->unit.active_camouflage < 0.f)
			{
				unit->unit.active_camouflage = 0.f;
			}
		}
		result = TRUE;
	}

	return result;
}

void shadowrun_game_tick(
	void)
{
	if (shadowrun_map_loaded())
	{
		shadowrun_magic_update();
		shadowrun_tech_update();
	}

	return;
}

void shadowrun_projectile_detonate(
	long projectile_index,
	real_point3d const *position)
{
	if (shadowrun_map_loaded())
	{
		shadowrun_projectile_detonated(projectile_index, position);
	}

	return;
}

/* port: a projectile that attached (projectiles.c): a grapple's bolt pulls
its caster where it holds, as one that detonates does where it lands */
void shadowrun_projectile_attached(
	long projectile_index,
	real_point3d const *position)
{
	if (shadowrun_map_loaded() && shadowrun_grapple_projectile_is(projectile_index))
	{
		shadowrun_grapple_pull(projectile_index, position);
	}

	return;
}

boolean shadowrun_unit_intangible(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	return shadowrun_unit && shadowrun_unit->smoke_hidden;
}

boolean shadowrun_object_is_thorn(
	long object_index)
{
	long index;
	boolean result = FALSE;

	for (index = 0; index < shadowrun_magic_globals->thorn_count; index++)
	{
		if (shadowrun_magic_globals->thorn_indices[index] == object_index)
		{
			result = TRUE;
			break;
		}
	}

	return result;
}

/* 0x285600 */
boolean shadowrun_spell_castable(
	long unit_index,
	short spell)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long mana_cost = 0;
	long capacity_reserved = 0;
	boolean result = FALSE;

	if (!shadowrun_unit || spell < 0 || spell >= NUMBER_OF_SHADOWRUN_SPELLS)
	{
		return FALSE;
	}
	/* port: no spell is cast inside an enemy's anti-magic
	(port/linux/game/shadowrun_tech.c) */
	if (shadowrun_spells_blocked(unit_index))
	{
		shadowrun_unit->spell_failed[spell] = TRUE;
		return FALSE;
	}
	shadowrun_spell_get(unit_index, spell, &mana_cost, NULL, &capacity_reserved, NULL, NULL);
	if (!TEST_FLAG(unit->object.damage_flags, _object_dead_bit) &&
		(unit->unit.actor_index != NONE || unit->unit.player_index != NONE) &&
		unit->unit.feign_death_timer == 0 &&
		shadowrun_unit->cast_cooldown_ticks < 1 &&
		shadowrun_unit->power_cooldown_ticks[spell] < 1)
	{
		/* (smoke ends as it is cast, whatever it costs) */
		result =
			(spell == _shadowrun_spell_smoke && shadowrun_unit->smoke_active) ||
			(shadowrun_unit_has_mana(unit_index, (real)mana_cost) &&
			shadowrun_unit_can_reserve_capacity(unit_index, capacity_reserved));
	}

	return result;
}

/* 0x260290 */
void shadowrun_spell_cast(
	long unit_index,
	short spell)
{
	if (spell < 0 || spell >= NUMBER_OF_SHADOWRUN_SPELLS)
	{
		return;
	}
	if (!shadowrun_spell_castable(unit_index, spell))
	{
		struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

		/* (summon elemental, cast again a second after, dismisses its summons
		instead) */
		if (spell == _shadowrun_spell_summon_elemental && shadowrun_unit)
		{
			if (shadowrun_unit->summon_time + SUMMON_DISMISS_TICKS < game_time_get())
			{
				shadowrun_summon_cast(unit_index);
			}
			else
			{
				shadowrun_unit->spell_failed[spell] = TRUE;
			}
		}
		else
		{
			shadowrun_unit_cast_failed(unit_index, spell);
		}
	}
	else
	{
		switch (spell)
		{
		case _shadowrun_spell_teleport:
			shadowrun_teleport_cast(unit_index, NULL, 0.f);
			break;
		case _shadowrun_spell_gust:
			shadowrun_gust_cast(unit_index);
			break;
		case _shadowrun_spell_resurrection:
			shadowrun_resurrection_cast(unit_index);
			break;
		case _shadowrun_spell_strangle:
			shadowrun_strangle_cast(unit_index, NULL);
			break;
		case _shadowrun_spell_tree_of_life:
			/* (the castable's own test: the caster throws the grenade) */
			if (!shadowrun_tree_of_life_castable(unit_index))
			{
				shadowrun_unit_get(unit_index)->spell_failed[spell] = TRUE;
			}
			else
			{
				shadowrun_spell_spend(unit_index, _shadowrun_spell_tree_of_life);
				shadowrun_tree_of_life_throw(unit_index);
			}
			break;
		case _shadowrun_spell_spirit_animal:
			shadowrun_spirit_animal_cast(unit_index);
			break;
		case _shadowrun_spell_smoke:
			shadowrun_smoke_cast(unit_index);
			break;
		case _shadowrun_spell_summon_elemental:
			shadowrun_summon_cast(unit_index);
			break;
		default:
			/* (not played yet) */
			shadowrun_unit_cast_failed(unit_index, spell);
			break;
		}
	}
	return;
}

/* 0x2604A0's: what lasts, each tick */
void shadowrun_spells_update(
	long unit_index)
{
	shadowrun_teleport_update(unit_index);
	shadowrun_resurrection_update(unit_index);
	shadowrun_summons_update(unit_index);
	shadowrun_strangle_update(unit_index);
	shadowrun_trees_update(unit_index);
	shadowrun_smoke_update(unit_index);
	shadowrun_spirit_animal_update(unit_index);

	return;
}

/* 0x284260: one Resurrection raised. Once it is to die (a newer one took its
place, or its master died) it takes the definition's damage each tick until
it does; dead again, its body takes the definition's other damage each tick
(the definitions are its own unit's) */
void shadowrun_undead_update(
	long unit_index)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean dead = TEST_FLAG(unit->object.damage_flags, _object_dead_bit);

	/* (each test of the executable's is also that the game is more than 15
	ticks old: the time it is measured from is never set) */
	if (!magic || !shadowrun_unit || game_time_get() <= 15)
	{
		return;
	}
	if (!shadowrun_unit->doomed || dead)
	{
		if (dead && unit->unit.feign_death_timer == 0)
		{
			shadowrun_undead_damage(unit_index, magic->resurrection_dead_damage.index);
		}
	}
	else
	{
		shadowrun_undead_damage(unit_index, magic->resurrection_doomed_damage.index);
	}

	return;
}

/* 0x2846A0: a summon. Once its time is up, or its caster died, it takes the
definition's damage each tick until it dies (the definition is its own
unit's) */
void shadowrun_summoned_update(
	long unit_index)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean dead = TEST_FLAG(unit->object.damage_flags, _object_dead_bit);

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	if (!shadowrun_unit->summon_dismissed || dead)
	{
		if (!dead && --shadowrun_unit->summon_lifetime_ticks < 1)
		{
			shadowrun_undead_damage(unit_index, magic->summon_dying_damage.index);
		}
	}
	else if (game_time_get() > 15)
	{
		/* (that the game is more than 15 ticks old: the time it is measured
		from is never set) */
		shadowrun_undead_damage(unit_index, magic->summon_dying_damage.index);
	}

	return;
}

/* 0x27C350's: what a unit's spells leave once it is dead for good. Those it
raised are to die (0x27CA10), its resurrection ball goes, and one it raised
gives back to its master what it held */
void shadowrun_spells_unit_died(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (!shadowrun_unit)
	{
		return;
	}
	shadowrun_summons_release(unit_index, FALSE);
	shadowrun_resurrection_release(unit_index);
	shadowrun_unit_stop_spell_effect(unit_index);
	if (shadowrun_unit->resurrection_ball_index != NONE)
	{
		if (object_try_and_get_and_verify_type(shadowrun_unit->resurrection_ball_index, NONE))
		{
			object_delete_immediately(shadowrun_unit->resurrection_ball_index);
		}
		shadowrun_unit->resurrection_ball_index = NONE;
	}
	if (shadowrun_unit->undead)
	{
		struct player_datum *master_player = player_try_and_get(shadowrun_unit->master_player_index);
		long master_unit_index = master_player ? master_player->unit_index : NONE;
		struct shadowrun_unit *master = shadowrun_unit_get(master_unit_index);

		shadowrun_unit->doomed = FALSE;
		if (master)
		{
			struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(master_unit_index);
			boolean listed = FALSE;
			boolean none_left = TRUE;
			short index;

			for (index = 0; index < MAXIMUM_SHADOWRUN_RESURRECTED_ACTORS; index++)
			{
				if (master->resurrected_actor_indices[index] != NONE)
				{
					if (!actor_try_and_get(master->resurrected_actor_indices[index]))
					{
						master->resurrected_actor_indices[index] = NONE;
					}
					listed = TRUE;
				}
			}
			/* (the executable's looks at ten: the four after these are a team
			game's, and none in single player) */
			for (index = 0; index < MAXIMUM_SHADOWRUN_RESURRECTED_ACTORS; index++)
			{
				if (master->resurrected_actor_indices[index] != NONE)
				{
					none_left = FALSE;
				}
			}
			if (magic && listed && none_left)
			{
				shadowrun_unit_release_capacity(master_unit_index, magic->resurrection_capacity_reserved);
			}
		}
	}

	return;
}

/* ---------- private code */

/* 0x285560: the mana it costs, the capacity it holds and its waits */
static void shadowrun_spell_spend(
	long unit_index,
	short spell)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long mana_cost = 0;
	long capacity_reserved = 0;
	long cast_cooldown_ticks = 0;
	long spell_cooldown_ticks = 0;

	shadowrun_spell_get(unit_index, spell, &mana_cost, NULL, &capacity_reserved, &cast_cooldown_ticks,
		&spell_cooldown_ticks);
	shadowrun_unit_spend_mana(unit_index, (real)mana_cost, FALSE, FALSE);
	shadowrun_unit_reserve_capacity(unit_index, capacity_reserved);
	shadowrun_unit->cast_cooldown_ticks = cast_cooldown_ticks;
	shadowrun_unit->power_cooldown_ticks[spell] = spell_cooldown_ticks;
	shadowrun_unit->current_spell = spell + 1;
	shadowrun_unit->desired_spell = spell + 1;

	return;
}

/* 0x269150: teleport. The unit goes the way it moves (or its controls are
pushed, or down in the air), or to target, after delay seconds and the
definition's: when its wait is down to the definition's move ticks */
static void shadowrun_teleport_cast(
	long unit_index,
	real_point3d const *target,
	real delay)
{
	struct shadowrun_magic1_definition *magic = shadowrun_magic1_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long mana_cost = 0;

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	shadowrun_spell_get(unit_index, _shadowrun_spell_teleport, &mana_cost, NULL, NULL, NULL, NULL);
	if (!shadowrun_unit_has_mana(unit_index, (real)mana_cost))
	{
		shadowrun_unit_cast_failed(unit_index, _shadowrun_spell_teleport);
		return;
	}

	shadowrun_unit_end_tech_movement(unit_index);
	shadowrun_unit->teleport_distance = 0.f;
	shadowrun_unit->teleport_delay_ticks = (long)((delay + magic->unused1) * 30.f);
	shadowrun_unit->teleport_direction = unit->object.translational_velocity;
	if (target)
	{
		shadowrun_unit->teleport_target = *target;
	}
	else
	{
		shadowrun_unit->teleport_target = *global_origin3d;
	}
	if (magnitude3d(&unit->object.translational_velocity) >= TELEPORT_MINIMUM_SPEED)
	{
		normalize3d(&shadowrun_unit->teleport_direction);
	}
	else if (shadowrun_unit->biped_airborne_tech <= 0.f)
	{
		real_vector3d throttle;
		real_euler_angles2d throttle_angles;
		real_euler_angles2d facing_angles;
		real_euler_angles2d angles;

		if (magnitude_squared3d(&unit->unit.throttle) <= TELEPORT_MINIMUM_THROTTLE_SQUARED)
		{
			/* standing still: the cast fizzles, for free */
			shadowrun_unit_damage_self(unit_index, magic->teleport_windup_damage.index, NULL);
			shadowrun_unit_effect(unit_index, magic->teleport_windup_effect.index);
			/* BUG (original, preserved): the before ones are asked for, the
			after ones given */
			if (magic->teleport_before_damage.index != NONE)
				shadowrun_unit_damage_self(unit_index, magic->teleport_after_damage.index, NULL);
			if (magic->teleport_before_effect.index != NONE)
				shadowrun_unit_effect(unit_index, magic->teleport_after_effect.index);
			shadowrun_unit_damage_self(unit_index, magic->teleport_after_damage.index, NULL);
			shadowrun_unit_effect(unit_index, magic->teleport_after_effect.index);
			return;
		}
		throttle = unit->unit.throttle;
		normalize3d(&throttle);
		euler_angles2d_from_vector3d(&throttle_angles, &throttle);
		euler_angles2d_from_vector3d(&facing_angles, &unit->object.forward);
		angles.yaw = throttle_angles.yaw + facing_angles.yaw;
		angles.pitch = 0.f;
		vector3d_from_euler_angles2d(&shadowrun_unit->teleport_direction, &angles);
	}
	else
	{
		set_real_vector3d(&shadowrun_unit->teleport_direction, 0.f, 0.f, -1.f);
	}

	shadowrun_spell_spend(unit_index, _shadowrun_spell_teleport);
	shadowrun_unit->cast_cooldown_ticks = magic->teleport_cooldown_ticks;
	shadowrun_unit->current_spell = _shadowrun_spell_teleport + 1;
	shadowrun_unit->desired_spell = _shadowrun_spell_teleport + 1;
	shadowrun_unit->teleporting = TRUE;
	ai_handle_unit_effect(unit_index, 1, shadowrun_ai_volume(magic->teleport_ai_unit_effect));
	shadowrun_unit_damage_self(unit_index, magic->teleport_windup_damage.index, NULL);
	shadowrun_unit_effect(unit_index, magic->teleport_windup_effect.index);

	return;
}

/* 0x2699E0: a teleport, as its wait runs down */
static void shadowrun_teleport_update(
	long unit_index)
{
	struct shadowrun_magic1_definition *magic = shadowrun_magic1_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	boolean to_target;

	if (!magic || !shadowrun_unit || unit->object.type != _object_type_biped || !shadowrun_unit->teleporting)
	{
		return;
	}
	/* BUG (original, preserved): the target's x is tested twice, its z never */
	to_target =
		shadowrun_unit->teleport_target.x != 0.f &&
		shadowrun_unit->teleport_target.y != 0.f &&
		shadowrun_unit->teleport_target.x != 0.f;

	if (shadowrun_unit->cast_cooldown_ticks == magic->teleport_move_ticks + 1)
	{
		/* where it goes from */
		shadowrun_unit->teleport_origin = unit->object.bounding_sphere_center;
		shadowrun_unit->teleport_origin_forward = unit->object.forward;
		if (magic->teleport_before_damage.index != NONE)
		{
			real_vector3d direction;

			if (to_target)
				vector_from_points3d(&shadowrun_unit->teleport_target, &shadowrun_unit->teleport_origin, &direction);
			else
				negate_vector3d(&unit->unit.aiming_vector, &direction);
			shadowrun_unit_damage_self(unit_index, magic->teleport_before_damage.index, &direction);
		}
		shadowrun_unit_effect(unit_index, magic->teleport_before_effect.index);
	}
	if (shadowrun_unit->cast_cooldown_ticks == magic->teleport_move_ticks)
	{
		/* gone */
		shadowrun_unit->teleport_hidden = TRUE;
		shadowrun_unit->teleported = TRUE;
		if (unit->unit.player_index == NONE)
		{
			real_point3d hiding_place;
			real_vector3d forward;

			set_real_point3d(&hiding_place, TELEPORT_HIDING_COORDINATE, TELEPORT_HIDING_COORDINATE,
				TELEPORT_HIDING_COORDINATE);
			set_real_vector3d(&forward, 0.f, 0.f, 1.f);
			object_set_position(unit_index, &hiding_place, &forward, NULL);
			SET_FLAG(unit->unit.flags, _unit_suspended_bit, TRUE);
		}
		else
		{
			shadowrun_unit->teleport_delay_ticks = 0;
		}
	}
	if (shadowrun_unit->teleport_hidden && shadowrun_unit->teleport_delay_ticks > 0)
	{
		shadowrun_unit->teleport_delay_ticks--;
	}
	else if (shadowrun_unit->teleport_hidden)
	{
		/* arrives */
		real_point3d destination = shadowrun_unit->teleport_origin;
		real_vector3d forward = shadowrun_unit->teleport_origin_forward;
		real_matrix4x3 frame;
		real distance;
		boolean placed = FALSE;
		short index;

		if (unit->unit.actor_index != NONE)
		{
			forward = shadowrun_unit->teleport_actor_forward;
		}
		SET_FLAG(unit->unit.flags, _unit_suspended_bit, FALSE);
		shadowrun_unit->teleport_hidden = FALSE;
		shadowrun_unit->teleporting = FALSE;
		shadowrun_unit->teleported = TRUE;
		distance = shadowrun_unit->teleport_distance != 0.f ?
			shadowrun_unit->teleport_distance : magic->teleport_distance;
		if (to_target)
		{
			destination = shadowrun_unit->teleport_target;
		}
		else
		{
			point_from_line3d(&destination, &shadowrun_unit->teleport_direction, distance, &destination);
		}
		matrix4x3_from_point_and_vectors(&frame, &destination, &unit->object.forward, &unit->object.up);
		for (index = 0; index < NUMBER_OF_TELEPORT_OFFSETS; index++)
		{
			real_vector3d offset;
			real_point3d point;

			scale_vector3d(&teleport_offsets[index], TELEPORT_OFFSET_SCALE, &offset);
			if (unit->unit.actor_index == NONE)
			{
				matrix4x3_transform_point(&frame, (real_point3d const *)&offset, &point);
			}
			else
			{
				add_vectors3d((real_vector3d const *)&destination, &offset, (real_vector3d *)&point);
			}
			if (!collision_test_point(TELEPORT_COLLISION_FLAGS, &point, NONE))
			{
				struct collision_result collision;
				real_vector3d clearance;

				scale_vector3d(global_up3d, TELEPORT_CLEARANCE_HEIGHT, &clearance);
				if (!collision_test_pill(TELEPORT_COLLISION_FLAGS, &point, &clearance, TELEPORT_CLEARANCE_RADIUS,
					NONE, &collision))
				{
					object_set_position(unit_index, &point, &forward, NULL);
					placed = TRUE;
					break;
				}
			}
		}
		if (!placed)
		{
			object_set_position(unit_index, &shadowrun_unit->teleport_origin, &shadowrun_unit->teleport_origin_forward,
				NULL);
		}
		if (magic->teleport_after_damage.index != NONE)
		{
			real_vector3d direction;

			if (to_target)
				vector_from_points3d(&shadowrun_unit->teleport_target, &shadowrun_unit->teleport_origin, &direction);
			else
				negate_vector3d(&unit->unit.aiming_vector, &direction);
			shadowrun_unit_damage_self(unit_index, magic->teleport_after_damage.index, &direction);
		}
		shadowrun_unit_effect(unit_index, magic->teleport_after_effect.index);
	}

	return;
}

/* 0x26A6B0: gust. A cone of wind along the caster's aim pushes the units in
it away (bipeds by their race's scale, vehicles by the definition's) and
frees them from strangling */
static void shadowrun_gust_cast(
	long unit_index)
{
	struct shadowrun_magic1_definition *magic = shadowrun_magic1_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	long mana_cost = 0;
	real wall_distance = 0.f;
	struct object_marker markers[1];
	real_point3d origin;
	real_vector3d vector;
	struct collision_result collision;
	struct object_iterator iterator;
	struct object_datum *object;

	if (!magic)
	{
		return;
	}
	shadowrun_spell_get(unit_index, _shadowrun_spell_gust, &mana_cost, NULL, NULL, NULL, NULL);
	if (!shadowrun_unit_has_mana(unit_index, (real)mana_cost))
	{
		shadowrun_unit_cast_failed(unit_index, _shadowrun_spell_gust);
		return;
	}

	shadowrun_unit_fire_projectile(unit_index, magic->gust_projectile.index, NULL);
	shadowrun_unit_cast_feedback(unit_index, magic->gust_cast_damage.index, magic->gust_cast_effect.index);
	shadowrun_spell_spend(unit_index, _shadowrun_spell_gust);
	ai_handle_unit_effect(unit_index, 1, shadowrun_ai_volume(magic->gust_ai_unit_effect));

	if (object_get_marker_by_name(unit_index, "head", markers, NUMBEROF(markers)) > 0)
	{
		origin = markers[0].matrix.position;
	}
	else
	{
		unit_get_head_position(unit_index, &origin);
	}
	point_from_line3d(&origin, &unit->unit.aiming_vector, GUST_ORIGIN_OFFSET, &origin);
	scale_vector3d(&unit->unit.aiming_vector, magic->gust_range, &vector);
	if (collision_test_vector(_collision_test_for_projectiles_flags, &origin, &vector, unit_index, &collision))
	{
		wall_distance = distance3d(&origin, &collision.point);
		scale_vector3d(&unit->unit.aiming_vector, wall_distance, &vector);
	}

	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long object_index = iterator.index;
		real_point3d position = object->object.position;
		real distance;

		/* the caster itself, from a wall near enough or in the air */
		if (object_index == unit_index &&
			wall_distance > GUST_SELF_DISTANCE &&
			!TEST_FLAG(biped_get(object_index)->biped.flags, _biped_airborne_bit))
		{
			continue;
		}
		distance = point_to_line_distance3d(&position, &origin, &vector);
		if (distance < magic->gust_maximum_distance)
		{
			real force;
			real_vector3d direction;

			if (distance >= magic->gust_full_force_distance)
			{
				real fraction = (distance - magic->gust_full_force_distance) /
					(magic->gust_maximum_distance - magic->gust_full_force_distance);

				force = magic->gust_near_force - (magic->gust_near_force - magic->gust_far_force) * fraction;
			}
			else
			{
				force = magic->gust_near_force;
			}
			direction.i = position.x - origin.x;
			direction.j = position.y - origin.y;
			direction.k = 0.f;
			normalize3d(&direction);
			direction.k += GUST_LIFT;
			normalize3d(&direction);

			if (object->object.type == _object_type_biped)
			{
				struct biped_datum *biped = (struct biped_datum *)object;
				struct shadowrun_race *race = shadowrun_unit_race_get(object_index);
				struct shadowrun_unit *pushed = shadowrun_unit_get(object_index);
				real_vector3d impulse;
				real_vector3d damage_direction;

				if (race)
				{
					force *= TEST_FLAG(biped->biped.flags, _biped_airborne_bit) ?
						race->gust_airborne_scale : race->gust_scale;
					if (magic->gust_damage.index != NONE)
					{
						/* BUG (original, preserved): the pushed unit's aim, but
						the caster is damaged */
						negate_vector3d(&biped->unit.aiming_vector, &damage_direction);
						shadowrun_unit_damage_self(unit_index, magic->gust_damage.index, &damage_direction);
					}
				}
				shadowrun_unit_end_tech_movement(object_index);
				scale_vector3d(&direction, force, &impulse);
				shadowrun_biped_push(object_index, &impulse, TRUE);
				negate_vector3d(&biped->unit.aiming_vector, &damage_direction);
				ai_handle_damage(object_index, unit_index, magic->gust_ai_damage_category,
					magic->gust_ai_damage_fraction, &damage_direction, FALSE);
				if (pushed && pushed->strangle_immune_ticks < magic->gust_strangle_break_ticks)
				{
					pushed->strangle_immune_ticks = magic->gust_strangle_break_ticks;
					if (pushed->strangled && shadowrun_unit_local_player_index(unit_index) != NONE)
					{
						long sound = shadowrun_globals_strangle_broken_sound();

						if (sound != NONE)
						{
							unspatialized_impulse_sound_new(sound, 1.f);
						}
						if (pushed->strangle_looping_sound_index != NONE)
						{
							unattached_looping_sound_stop(pushed->strangle_looping_sound_index);
						}
						pushed->strangle_looping_sound_index = NONE;
					}
					if (pushed)
					{
						pushed->strangled = FALSE;
					}
				}
			}
			else if (object->object.type == _object_type_vehicle)
			{
				real_vector3d impulse;

				scale_vector3d(&direction, force * magic->gust_vehicle_force_scale, &impulse);
				shadowrun_vehicle_push(object_index, &impulse);
			}
		}
	}

	return;
}

/* 0x2668E0: strangle. Thorns grow where the caster aims (or at target): ten
places about a line across its aim, each looking for a surface below it
(above, for one; to either side, for two) within the definition's reach.
The oldest thorns go to keep the definition's most */
static void shadowrun_strangle_cast(
	long unit_index,
	real_point3d const *target)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long mana_cost = 0;

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	shadowrun_spell_get(unit_index, _shadowrun_spell_strangle, &mana_cost, NULL, NULL, NULL, NULL);
	if (!shadowrun_unit_has_mana(unit_index, (real)mana_cost))
	{
		shadowrun_unit_cast_failed(unit_index, _shadowrun_spell_strangle);
	}
	else
	{
		struct object_marker markers[1];
		struct collision_result collision;
		real_point3d origin;
		real_vector3d vector;
		boolean aimed;

		ai_handle_unit_effect(unit_index, 1, shadowrun_ai_volume(magic->strangle_ai_unit_effect));
		shadowrun_unit_fire_projectile(unit_index, magic->strangle_projectile.index, NULL);
		shadowrun_unit_cast_feedback(unit_index, magic->strangle_cast_damage.index, magic->strangle_cast_effect.index);
		shadowrun_unit->cast_cooldown_ticks = magic->strangle_cooldown_ticks;
		if (object_get_marker_by_name(unit_index, "head", markers, NUMBEROF(markers)) > 0)
		{
			origin = markers[0].matrix.position;
		}
		else
		{
			/* port: the executable assumes the unit has the marker */
			unit_get_head_position(unit_index, &origin);
		}
		point_from_line3d(&origin, &unit->unit.aiming_vector, STRANGLE_ORIGIN_OFFSET, &origin);
		scale_vector3d(&unit->unit.aiming_vector, magic->strangle_range, &vector);
		aimed = target || collision_test_vector(_collision_test_for_projectiles_flags, &origin, &vector, unit_index,
			&collision);
		if (aimed)
		{
			real_point3d center = target ? *target : collision.point;
			real_vector3d forward;
			real_vector3d right;
			real_vector3d left;
			real_point3d points[STRANGLE_PLACES];
			real_vector3d directions[STRANGLE_PLACES];
			real spacing = magic->strangle_spacing;
			short index;

			center.z += magic->strangle_height;
			forward.i = unit->unit.aiming_vector.i;
			forward.j = unit->unit.aiming_vector.j;
			forward.k = 0.f;
			normalize3d(&forward);
			cross_product3d(&forward, global_up3d, &right);
			negate_vector3d(&right, &left);
			/* the line: its middle twice (the floor, then the ceiling), out to
			either side three times the spacing, and its middle twice more
			(the walls either side) */
			for (index = 0; index < STRANGLE_PLACES; index++)
			{
				points[index] = center;
				directions[index] = *global_down3d;
			}
			directions[1] = *global_up3d;
			point_from_line3d(&center, &right, spacing, &points[2]);
			point_from_line3d(&center, &left, spacing, &points[3]);
			point_from_line3d(&center, &left, 2.f * spacing, &points[4]);
			point_from_line3d(&center, &right, 2.f * spacing, &points[5]);
			point_from_line3d(&center, &left, 3.f * spacing, &points[6]);
			point_from_line3d(&center, &right, 3.f * spacing, &points[7]);
			directions[8] = right;
			directions[9] = left;
			/* (each point's height is the line's: only x and y are moved) */
			for (index = 2; index < 8; index++)
			{
				points[index].z = center.z;
			}

			shadowrun_strangle_cull_thorns(magic->strangle_count, magic->strangle_maximum_count);
			shadowrun_spell_spend(unit_index, _shadowrun_spell_strangle);
			/* BUG (original, preserved): the definition's count of thorns is
			never counted against, so all ten places are tried */
			for (index = 0; index < STRANGLE_PLACES; index++)
			{
				real_vector3d reach;

				points[index].x += real_random_range(-magic->strangle_scatter, magic->strangle_scatter);
				points[index].y += real_random_range(-magic->strangle_scatter, magic->strangle_scatter);
				scale_vector3d(&directions[index], magic->strangle_reach, &reach);
				if (collision_test_vector(_collision_test_for_projectiles_flags, &points[index], &reach, unit_index,
					&collision))
				{
					real_vector3d up;
					long thorn_index;

					negate_vector3d(&directions[index], &up);
					thorn_index = shadowrun_strangle_new_thorn(unit_index, magic->strangle_scenery.index,
						&collision.point, &up);
					if (thorn_index != NONE)
					{
						struct object_datum *thorn = object_get(thorn_index);
						real scale = real_random_range(magic->strangle_minimum_scale, magic->strangle_maximum_scale);

						/* (it grows from a tenth of its scale) */
						if (shadowrun_unit->growing_thorn_count < MAXIMUM_SHADOWRUN_GROWING_THORNS)
						{
							struct shadowrun_growing_thorn *growing =
								&shadowrun_unit->growing_thorns[shadowrun_unit->growing_thorn_count++];

							growing->object_index = thorn_index;
							growing->scale = scale;
							growing->ticks = 0;
							growing->delay_ticks = 0;
							thorn->object.scale = MAX(scale * STRANGLE_STARTING_SCALE, STRANGLE_STARTING_SCALE);
						}
						else
						{
							/* port: the executable's list of thorns growing has no
							bound: one more grows at once */
							thorn->object.scale = scale * magic->strangle_scale;
						}
						thorn->object.owner_team_index = unit->object.owner_team_index;
						if (shadowrun_magic_globals->thorn_count < MAXIMUM_SHADOWRUN_THORNS)
						{
							shadowrun_magic_globals->thorn_indices[shadowrun_magic_globals->thorn_count++] = thorn_index;
						}
					}
				}
			}
		}
	}

	return;
}

/* 0x267700: a thorn on a surface, leaning at random, turned at random */
static long shadowrun_strangle_new_thorn(
	long unit_index,
	long definition_index,
	real_point3d const *position,
	real_vector3d const *up)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	long thorn_index = NONE;

	if (magic && definition_index != NONE)
	{
		struct object_placement_data placement;
		real turn = real_random_range(-STRANGLE_PI, STRANGLE_PI);
		real lean_i = real_random_range(-magic->strangle_lean_i, magic->strangle_lean_i);
		real lean_j = real_random_range(-magic->strangle_lean_j, magic->strangle_lean_j);

		object_placement_data_new(&placement, definition_index, unit_index);
		placement.position = *position;
		placement.up = *up;
		rotate_vector_about_axis(&placement.up, global_forward3d, sine(lean_i), cosine(lean_i));
		rotate_vector_about_axis(&placement.up, global_left3d, sine(lean_j), cosine(lean_j));
		perpendicular3d(&placement.up, &placement.forward);
		normalize3d(&placement.forward);
		rotate_vector_about_axis(&placement.forward, &placement.up, sine(turn), cosine(turn));
		thorn_index = object_new(&placement);
	}

	return thorn_index;
}

/* 0x2661C0: the thorns gone are forgotten, and the oldest go until count
more would make no more than the most kept */
static void shadowrun_strangle_cull_thorns(
	long count,
	long maximum_count)
{
	struct shadowrun_magic_globals *globals = shadowrun_magic_globals;
	long kept = 0;
	long index;

	for (index = 0; index < globals->thorn_count; index++)
	{
		if (object_try_and_get_and_verify_type(globals->thorn_indices[index], NONE))
		{
			globals->thorn_indices[kept++] = globals->thorn_indices[index];
		}
	}
	globals->thorn_count = kept;
	/* port: and to leave room for count more in this build's list */
	maximum_count = MIN(maximum_count, (long)MAXIMUM_SHADOWRUN_THORNS);
	while (globals->thorn_count > 0 && globals->thorn_count + count > maximum_count)
	{
		object_delete_immediately(globals->thorn_indices[0]);
		csmemmove(&globals->thorn_indices[0], &globals->thorn_indices[1],
			(globals->thorn_count - 1) * sizeof(globals->thorn_indices[0]));
		globals->thorn_count--;
	}

	return;
}

/* 0x267930: the unit's thorns grow, each over the definition's ticks after a
tick's wait, to their scale (0x267A70: those grown, or gone, are let be) */
static void shadowrun_strangle_update(
	long unit_index)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	short index;
	short kept = 0;

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	for (index = 0; index < shadowrun_unit->growing_thorn_count; index++)
	{
		struct shadowrun_growing_thorn *growing = &shadowrun_unit->growing_thorns[index];

		if (growing->delay_ticks < 0)
		{
			struct object_datum *thorn = object_try_and_get_and_verify_type(growing->object_index, NONE);

			if (thorn)
			{
				growing->ticks = (short)MIN(growing->ticks + 1, magic->strangle_growth_ticks);
				thorn->object.scale = ((real)growing->ticks / (real)magic->strangle_growth_ticks) *
					magic->strangle_scale * growing->scale;
				if (thorn->object.scale < STRANGLE_MINIMUM_SCALE)
				{
					thorn->object.scale = STRANGLE_MINIMUM_SCALE;
				}
			}
		}
		else
		{
			growing->delay_ticks--;
		}
	}
	for (index = 0; index < shadowrun_unit->growing_thorn_count; index++)
	{
		struct shadowrun_growing_thorn *growing = &shadowrun_unit->growing_thorns[index];

		if (object_try_and_get_and_verify_type(growing->object_index, NONE) &&
			growing->ticks < magic->strangle_growth_ticks)
		{
			shadowrun_unit->growing_thorns[kept++] = *growing;
		}
	}
	shadowrun_unit->growing_thorn_count = kept;

	return;
}

/* 0x267BD0: firewall. Fires burn on the surfaces about target (where the
caster aims, if NULL): ten places about it, each looking for a surface below
it (above, for one; to either side, for two) */
static void shadowrun_firewall_cast(
	long unit_index,
	real_point3d const *target)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long mana_cost = 0;

	if (!magic || !shadowrun_unit || (shadowrun_unit->cast_cooldown_ticks > 0 && unit->unit.actor_index == NONE))
	{
		return;
	}
	shadowrun_unit->current_spell = _shadowrun_spell_strangle + 1;
	shadowrun_unit->desired_spell = _shadowrun_spell_strangle + 1;
	shadowrun_spell_get(unit_index, _shadowrun_spell_strangle, &mana_cost, NULL, NULL, NULL, NULL);
	if (!shadowrun_unit_has_mana(unit_index, (real)mana_cost))
	{
		shadowrun_unit_cast_failed_sound(unit_index);
	}
	else
	{
		struct object_marker markers[1];
		struct collision_result collision;
		real_point3d origin;
		real_vector3d vector;
		real_point3d center;
		boolean aimed;

		shadowrun_unit_fire_projectile(unit_index, magic->firewall_projectile.index, target);
		shadowrun_unit_cast_feedback(unit_index, magic->firewall_cast_damage.index, magic->firewall_cast_effect.index);
		shadowrun_unit->spirit_animal_growth_ticks = 0;
		shadowrun_unit->cast_cooldown_ticks = magic->firewall_cooldown_ticks;
		if (object_get_marker_by_name(unit_index, "head", markers, NUMBEROF(markers)) > 0)
		{
			origin = markers[0].matrix.position;
		}
		else
		{
			/* port: the executable assumes the unit has the marker */
			unit_get_head_position(unit_index, &origin);
		}
		point_from_line3d(&origin, &unit->unit.aiming_vector, STRANGLE_ORIGIN_OFFSET, &origin);
		scale_vector3d(&unit->unit.aiming_vector, magic->firewall_range, &vector);
		aimed = target || collision_test_vector(FIREWALL_COLLISION_FLAGS, &origin, &vector, unit_index, &collision);
		if (aimed)
		{
			struct shadowrun_magic_globals *globals = shadowrun_magic_globals;
			real_point3d points[STRANGLE_PLACES];
			real_vector3d directions[STRANGLE_PLACES];
			real_vector3d forward;
			real_vector3d right;
			real_vector3d left;
			real_vector3d back;
			real spacing = magic->firewall_spacing;
			short index;
			long kept = 0;

			/* port: the executable reads target even when it has none, and
			aimed at a surface (it dereferenced NULL); where it aimed is used */
			center = target ? *target : collision.point;
			center.z += magic->firewall_height;
			vector_from_points3d(&unit->object.position, &center, &forward);
			forward.k = 0.f;
			normalize3d(&forward);
			cross_product3d(&forward, global_up3d, &right);
			negate_vector3d(&right, &left);
			negate_vector3d(&forward, &back);
			/* the places: the middle twice (the floor, then the ceiling), to
			either side, and a row before and behind it */
			for (index = 0; index < STRANGLE_PLACES; index++)
			{
				points[index] = center;
				directions[index] = *global_down3d;
			}
			directions[1] = *global_up3d;
			point_from_line3d(&center, &right, spacing, &points[2]);
			point_from_line3d(&center, &left, spacing, &points[3]);
			point_from_line3d(&center, &forward, spacing, &points[4]);
			point_from_line3d(&points[4], &right, spacing, &points[4]);
			point_from_line3d(&center, &forward, spacing, &points[5]);
			point_from_line3d(&center, &forward, spacing, &points[6]);
			point_from_line3d(&points[6], &left, spacing, &points[6]);
			point_from_line3d(&center, &back, spacing, &points[7]);
			point_from_line3d(&points[7], &right, spacing, &points[7]);
			point_from_line3d(&center, &back, spacing, &points[8]);
			point_from_line3d(&center, &back, spacing, &points[9]);
			point_from_line3d(&points[9], &left, spacing, &points[9]);
			directions[8] = right;
			directions[9] = left;
			for (index = 2; index < STRANGLE_PLACES; index++)
			{
				points[index].z = center.z;
			}

			/* the fires gone are forgotten */
			for (index = 0; index < globals->fire_count; index++)
			{
				if (object_try_and_get_and_verify_type(globals->fires[index].object_index, NONE))
				{
					globals->fires[kept++] = globals->fires[index];
				}
			}
			globals->fire_count = kept;
			shadowrun_unit_spend_mana(unit_index, (real)mana_cost, FALSE, FALSE);
			/* BUG (original, preserved): the definition's count of fires is
			never counted against, so all ten places are tried */
			for (index = 0; index < STRANGLE_PLACES; index++)
			{
				real_vector3d reach;

				points[index].x += real_random_range(-magic->firewall_scatter, magic->firewall_scatter);
				points[index].y += real_random_range(-magic->firewall_scatter, magic->firewall_scatter);
				scale_vector3d(&directions[index], magic->firewall_reach, &reach);
				if (magic->firewall_count > 0 &&
					collision_test_vector(FIREWALL_COLLISION_FLAGS, &points[index], &reach, unit_index, &collision) &&
					magic->firewall_scenery.index != NONE)
				{
					struct object_placement_data placement;
					long fire_index;

					/* 0x2678A0 */
					object_placement_data_new(&placement, magic->firewall_scenery.index, unit_index);
					placement.position = collision.point;
					placement.position.z += FIREWALL_LIFT;
					fire_index = object_new(&placement);
					if (fire_index != NONE)
					{
						struct object_datum *fire = object_get(fire_index);

						if (magic->firewall_effect.index != NONE)
						{
							effect_new_looping(magic->firewall_effect.index, fire_index, NONE, NONE,
								SMOKE_EFFECT_CHANGE_COLOR);
						}
						fire->object.scale = 1.f;
						fire->object.owner_team_index = unit->object.owner_team_index;
						if (globals->fire_count < MAXIMUM_SHADOWRUN_FIRES)
						{
							struct shadowrun_fire *burning = &globals->fires[globals->fire_count++];

							burning->object_index = fire_index;
							burning->end_time = game_time_get() + magic->firewall_lifetime_ticks;
							burning->wait_ticks = 0;
							burning->damage_definition_index = magic->firewall_damage.index;
							burning->damage_ticks = (real)magic->firewall_damage_ticks;
							burning->damage_radius = magic->firewall_damage_radius;
							burning->owner_team_index = unit->object.owner_team_index;
						}
					}
				}
			}
		}
	}

	return;
}

/* 0x266780: the fires burn till their time, and every other tick hurt those
in their reach */
static void shadowrun_fires_update(
	void)
{
	struct shadowrun_magic_globals *globals = shadowrun_magic_globals;
	long index;
	long kept = 0;

	for (index = 0; index < globals->fire_count; index++)
	{
		struct shadowrun_fire *fire = &globals->fires[index];
		struct object_datum *object = object_try_and_get_and_verify_type(fire->object_index, NONE);

		if (fire->end_time < game_time_get())
		{
			if (object)
			{
				object_delete_immediately(fire->object_index);
			}
			fire->object_index = NONE;
		}
		else if (object && fire->wait_ticks < 1)
		{
			long object_indices[FIREWALL_MAXIMUM_VICTIMS];
			short count = objects_in_sphere(0, 0, &object->object.location, &object->object.position,
				fire->damage_radius, object_indices, NUMBEROF(object_indices));
			short victim;

			for (victim = 0; victim < count; victim++)
			{
				shadowrun_fire_burn(object_indices[victim], fire);
			}
		}
		fire->wait_ticks = fire->wait_ticks < 1 ? 1 : fire->wait_ticks - 1;
		if (fire->object_index != NONE)
		{
			globals->fires[kept++] = *fire;
		}
	}
	globals->fire_count = kept;

	return;
}

/* 0x266630: a fire hurts a biped of another side, alive (the raised ten
times as much) */
static void shadowrun_fire_burn(
	long object_index,
	struct shadowrun_fire *fire)
{
	struct object_datum *object = object_get(object_index);
	struct object_datum *fire_object = object_get(fire->object_index);

	if (object->object.type == _object_type_biped &&
		object->object.owner_team_index != fire_object->object.owner_team_index &&
		!TEST_FLAG(object->object.damage_flags, _object_dead_bit) &&
		fire->damage_definition_index != NONE)
	{
		struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(object_index);
		struct damage_data damage;

		damage_data_new(&damage, fire->damage_definition_index);
		damage.origin = fire_object->object.position;
		damage.epicenter = fire_object->object.position;
		damage.direction = *global_up3d;
		damage.multiplier = 1.f / fire->damage_ticks;
		if (shadowrun_unit && shadowrun_unit->undead)
		{
			damage.multiplier *= FIREWALL_UNDEAD_DAMAGE_SCALE;
		}
		object_cause_damage(&damage, object_index, NONE, NONE, NONE, NULL);
	}

	return;
}

/* 0x2643F0: whether the unit may throw a tree of life now: not throwing
already, with its trees under their bounds, an actor without one, and the
mana */
static boolean shadowrun_tree_of_life_castable(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long mana_cost = 0;
	boolean result = FALSE;

	if (shadowrun_magic1_get(unit_index) && shadowrun_unit &&
		unit->unit.grenade_throw_state == _unit_grenade_throw_idle)
	{
		shadowrun_spell_get(unit_index, _shadowrun_spell_tree_of_life, &mana_cost, NULL, NULL, NULL, NULL);
		if (shadowrun_unit->old_tree_count < MAXIMUM_SHADOWRUN_OLD_TREES &&
			shadowrun_unit->tree_count < MAXIMUM_SHADOWRUN_TREES &&
			(shadowrun_unit->tree_count < 1 || unit->unit.actor_index == NONE) &&
			shadowrun_unit_has_mana(unit_index, (real)mana_cost))
		{
			result = TRUE;
		}
	}

	return result;
}

/* the caster throws the tree of life grenade (the executable's grenade
throw of the globals' fourth grenade: this build has two, so its projectile
goes from the caster at once); where it lands the tree grows
(shadowrun_projectile_detonated) */
static void shadowrun_tree_of_life_throw(
	long unit_index)
{
	struct game_globals *globals = scenario_get_game_globals();
	long projectile_definition_index = NONE;

	if (globals)
	{
		/* (the validator cut the globals' grenades to this build's two; the
		rest are where they were) */
		struct game_globals_grenade const *grenade = (struct game_globals_grenade const *)globals->grenades.address +
			TREE_OF_LIFE_GRENADE_INDEX;

		if (cache_file_tag_cache_contains(grenade, sizeof(*grenade)) &&
			cache_file_tag_index_group_tag(grenade->projectile.index) == PROJECTILE_DEFINITION_TAG)
		{
			projectile_definition_index = grenade->projectile.index;
		}
	}
	if (projectile_definition_index != NONE)
	{
		shadowrun_unit_fire_projectile(unit_index, projectile_definition_index, NULL);
	}
	else
	{
		/* port: without the grenade, the tree grows where the caster aims, as
		an actor's does */
		shadowrun_tree_of_life_grow(unit_index, NULL);
	}

	return;
}

/* 0x264520: a tree of life grows at position (where an actor aims, up to the
definition's range, if NULL). The caster keeps four growing; the oldest of
them is let be, among ten older ones, beyond which it casts no more; the
trees standing in all are the definition's most */
static boolean shadowrun_tree_of_life_grow(
	long unit_index,
	real_point3d const *position)
{
	struct shadowrun_magic1_definition *magic = shadowrun_magic1_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct shadowrun_magic_globals *globals = shadowrun_magic_globals;
	real_point3d target;
	boolean result = FALSE;
	long tree_index;

	if (!magic || !shadowrun_unit || shadowrun_unit->old_tree_count >= MAXIMUM_SHADOWRUN_OLD_TREES)
	{
		return FALSE;
	}
	if (shadowrun_unit->tree_count >= MAXIMUM_SHADOWRUN_TREES)
	{
		if (unit->unit.actor_index != NONE)
		{
			return FALSE;
		}
		/* the oldest growing is let be */
		shadowrun_unit->old_tree_indices[shadowrun_unit->old_tree_count++] = shadowrun_unit->tree_indices[0];
		csmemmove(&shadowrun_unit->tree_indices[0], &shadowrun_unit->tree_indices[1],
			(MAXIMUM_SHADOWRUN_TREES - 1) * sizeof(shadowrun_unit->tree_indices[0]));
		csmemmove(&shadowrun_unit->tree_scales[0], &shadowrun_unit->tree_scales[1],
			(MAXIMUM_SHADOWRUN_TREES - 1) * sizeof(shadowrun_unit->tree_scales[0]));
		csmemmove(&shadowrun_unit->tree_growth_ticks[0], &shadowrun_unit->tree_growth_ticks[1],
			(MAXIMUM_SHADOWRUN_TREES - 1) * sizeof(shadowrun_unit->tree_growth_ticks[0]));
		shadowrun_unit->tree_count--;
		shadowrun_unit->tree_indices[shadowrun_unit->tree_count] = NONE;
	}
	ai_handle_unit_effect(unit_index, 1, shadowrun_ai_volume(magic->tree_of_life_ai_unit_effect));
	shadowrun_unit_cast_feedback(unit_index, magic->tree_of_life_cast_damage.index, magic->tree_of_life_cast_effect.index);
	if (position)
	{
		target = *position;
	}
	else
	{
		struct object_marker markers[1];
		struct collision_result collision;
		real_point3d origin;
		real_vector3d vector;

		shadowrun_unit_fire_projectile(unit_index, magic->tree_projectile.index, NULL);
		if (object_get_marker_by_name(unit_index, "head", markers, NUMBEROF(markers)) > 0)
		{
			origin = markers[0].matrix.position;
		}
		else
		{
			/* port: the executable assumes the unit has the marker */
			unit_get_head_position(unit_index, &origin);
		}
		point_from_line3d(&origin, &unit->unit.aiming_vector, TREE_OF_LIFE_ORIGIN_OFFSET, &origin);
		scale_vector3d(&unit->unit.aiming_vector, magic->tree_of_life_range, &vector);
		if (!collision_test_vector(_collision_test_for_projectiles_flags, &origin, &vector, unit_index, &collision))
		{
			return FALSE;
		}
		target = collision.point;
	}

	/* the trees gone are forgotten, and the oldest go to keep the most */
	{
		long kept = 0;
		long index;
		long maximum_count = MIN(magic->tree_of_life_maximum_count, (long)MAXIMUM_SHADOWRUN_STANDING_TREES);

		for (index = 0; index < globals->tree_count; index++)
		{
			if (object_try_and_get_and_verify_type(globals->trees[index].object_index, NONE))
			{
				globals->trees[kept++] = globals->trees[index];
			}
		}
		globals->tree_count = kept;
		while (globals->tree_count > 0 && globals->tree_count + 1 > maximum_count)
		{
			object_delete_immediately(globals->trees[0].object_index);
			csmemmove(&globals->trees[0], &globals->trees[1], (globals->tree_count - 1) * sizeof(globals->trees[0]));
			globals->tree_count--;
		}
	}

	tree_index = NONE;
	if (magic->tree_of_life_scenery.index != NONE)
	{
		struct object_placement_data placement;

		object_placement_data_new(&placement, magic->tree_of_life_scenery.index, unit_index);
		placement.position = target;
		perpendicular3d(global_up3d, &placement.forward);
		normalize3d(&placement.forward);
		tree_index = object_new(&placement);
	}
	if (tree_index != NONE)
	{
		struct object_datum *tree = object_get(tree_index);
		real scale = real_random_range(magic->tree_of_life_minimum_scale, magic->tree_of_life_maximum_scale);

		shadowrun_unit->tree_indices[shadowrun_unit->tree_count] = tree_index;
		shadowrun_unit->tree_scales[shadowrun_unit->tree_count] = scale;
		shadowrun_unit->tree_growth_ticks[shadowrun_unit->tree_count] = magic->tree_of_life_growth_ticks;
		shadowrun_unit->tree_count++;
		tree->object.scale = MAX(scale * TREE_OF_LIFE_STARTING_SCALE, TREE_OF_LIFE_STARTING_SCALE);
		if (globals->tree_count < MAXIMUM_SHADOWRUN_STANDING_TREES)
		{
			struct shadowrun_standing_tree *standing = &globals->trees[globals->tree_count++];

			standing->object_index = tree_index;
			standing->heal_rate = magic->tree_of_life_heal_rate;
			standing->heal_radius = magic->tree_of_life_heal_radius;
			standing->heal_feedback_ticks = 0;
		}
		result = TRUE;
	}

	return result;
}

/* 0x265C90: the unit's trees grow, and all of them wither, the older
faster; those gone are forgotten */
static void shadowrun_trees_update(
	long unit_index)
{
	struct shadowrun_magic1_definition *magic = shadowrun_magic1_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long index;

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	for (index = 0; index < shadowrun_unit->tree_count; index++)
	{
		struct object_datum *tree = object_try_and_get_and_verify_type(shadowrun_unit->tree_indices[index], NONE);

		if (tree)
		{
			if (shadowrun_unit->tree_growth_ticks[index] != 0)
			{
				shadowrun_unit->tree_growth_ticks[index]--;
				tree->object.scale = ((real)(magic->tree_of_life_growth_ticks - shadowrun_unit->tree_growth_ticks[index]) /
					(real)magic->tree_of_life_growth_ticks) * shadowrun_unit->tree_scales[index];
				if (tree->object.scale < TREE_OF_LIFE_STARTING_SCALE)
				{
					tree->object.scale = TREE_OF_LIFE_STARTING_SCALE;
				}
			}
			tree->object.body_vitality -= magic->tree_of_life_decay;
			object_port_check_body(shadowrun_unit->tree_indices[index]);
		}
		else
		{
			csmemmove(&shadowrun_unit->tree_indices[index], &shadowrun_unit->tree_indices[index + 1],
				(MAXIMUM_SHADOWRUN_TREES - index - 1) * sizeof(shadowrun_unit->tree_indices[0]));
			csmemmove(&shadowrun_unit->tree_scales[index], &shadowrun_unit->tree_scales[index + 1],
				(MAXIMUM_SHADOWRUN_TREES - index - 1) * sizeof(shadowrun_unit->tree_scales[0]));
			csmemmove(&shadowrun_unit->tree_growth_ticks[index], &shadowrun_unit->tree_growth_ticks[index + 1],
				(MAXIMUM_SHADOWRUN_TREES - index - 1) * sizeof(shadowrun_unit->tree_growth_ticks[0]));
			shadowrun_unit->tree_count--;
			shadowrun_unit->tree_indices[shadowrun_unit->tree_count] = NONE;
			/* BUG (original, preserved): the next tree, moved into this one's
			place, waits for the next tick */
		}
	}
	for (index = 0; index < shadowrun_unit->old_tree_count; index++)
	{
		struct object_datum *tree = object_try_and_get_and_verify_type(shadowrun_unit->old_tree_indices[index], NONE);

		if (tree)
		{
			tree->object.body_vitality -= magic->tree_of_life_old_decay;
			object_port_check_body(shadowrun_unit->old_tree_indices[index]);
		}
		else
		{
			csmemmove(&shadowrun_unit->old_tree_indices[index], &shadowrun_unit->old_tree_indices[index + 1],
				(MAXIMUM_SHADOWRUN_OLD_TREES - index - 1) * sizeof(shadowrun_unit->old_tree_indices[0]));
			shadowrun_unit->old_tree_count--;
			shadowrun_unit->old_tree_indices[shadowrun_unit->old_tree_count] = NONE;
		}
	}

	return;
}

/* port: the map's actor variant of that name, else the first whose name
starts as it does (the map's tags are walked: a tag_loaded takes an exact
name, and a protected map's tags have no names to find it by). NONE if the
map has neither. The result is kept in `cache` (a map's tags do not change) */
/* port: the map's actor variant of that name, else the first whose name
starts as it does (the map's tags are walked: a tag_loaded takes an exact
name, and a protected map's tags have no names to find it by). NONE if the
map has neither. `cache` holds NONE until it is looked for once, then the
index found, or SHADOWRUN_NAMED_VARIANT_NOT_FOUND (-2: a value no index is)
if there was none, so that a map is walked once */
#define SHADOWRUN_NAMED_VARIANT_NOT_FOUND (-2)

static long shadowrun_actor_variant_named(
	char const *name,
	char const *prefix,
	long *cache)
{
	struct tag_iterator iterator;
	long found = NONE;
	long tag_index;

	if (*cache != NONE)
	{
		return *cache == SHADOWRUN_NAMED_VARIANT_NOT_FOUND ? NONE : *cache;
	}
	tag_iterator_new(&iterator, SHADOWRUN_ACTOR_VARIANT_TAG);
	while (found == NONE && (tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		/* (a tag's name is its path without the group: a map may keep the
		path whole, so either form is taken) */
		char const *tag_name = tag_get_name(tag_index);

		if (!_stricmp(tag_name, name) || !_stricmp(tag_name_strip_path(tag_name), name))
		{
			found = tag_index;
		}
	}
	if (found == NONE && prefix)
	{
		tag_iterator_new(&iterator, SHADOWRUN_ACTOR_VARIANT_TAG);
		while (found == NONE && (tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			if (!_strnicmp(tag_name_strip_path(tag_get_name(tag_index)), prefix, strlen(prefix)))
			{
				found = tag_index;
			}
		}
	}
	if (found == NONE)
	{
		error(_error_silent, "shadowrun: the map has no %s", name);
		*cache = SHADOWRUN_NAMED_VARIANT_NOT_FOUND;
	}
	else
	{
		*cache = found;
	}

	return found;
}

/* port: the map's actor variant Summon Elemental summons, the elite commander
with the energy sword, and one of its name starting so. NONE if the map has
neither, and the spell summons the definition's encounter's actor as it did.
Found once a map: the first time it is asked for */
static long shadowrun_summon_elemental_variant(
	void)
{
	return shadowrun_actor_variant_named(SHADOWRUN_SUMMONED_ACTOR_VARIANT,
		SHADOWRUN_SUMMONED_ACTOR_VARIANT_PREFIX, &shadowrun_summoned_variant);
}

/* port: one actor of that variant, at `position`, facing the caster's way. It
is placed as ai.c places an actor of its own (no encounter's: the variant
need not be one of the map's encounters'): the placement csmemset gives, the
states it zeros being none */
static long shadowrun_summon_elemental_place(
	long variant,
	long unit_index,
	real_point3d const *position)
{
	struct object_datum *caster = object_get(unit_index);
	struct actor_starting_location starting_location;

	csmemset(&starting_location, 0, sizeof(starting_location));
	starting_location.command_list_index = NONE;
	starting_location.position = *position;
	starting_location.facing = arctangent(caster->object.forward.j, caster->object.forward.i);

	return actor_place(variant, NONE, 0, &starting_location, FALSE, 0);
}

/* port: the map's actor variant the Hologram tech makes (shadowrun_tech.c),
the hologram with the assault rifle. NONE if the map has none. Found once a
map: the first time it is asked for */
long shadowrun_hologram_variant(
	void)
{
	return shadowrun_actor_variant_named(SHADOWRUN_HOLOGRAM_ACTOR_VARIANT, NULL, &shadowrun_hologram_variant_index);
}

/* port: a hologram of the map's, at `position`, facing the caster's way, on
its side: an actor of its variant, placed as a summon's is. NONE if the map
has no hologram, or none could be placed */
long shadowrun_hologram_place(
	long unit_index,
	real_point3d const *position)
{
	long variant = shadowrun_hologram_variant();
	long actor_index = variant != NONE ? shadowrun_summon_elemental_place(variant, unit_index, position) : NONE;
	struct actor_datum *actor = actor_index != NONE ? actor_try_and_get(actor_index) : NULL;
	struct unit_datum *caster = unit_try_and_get(unit_index);

	if (actor && caster)
	{
		struct unit_datum *hologram = unit_get(actor->meta.unit_index);
		struct player_datum *player = player_try_and_get(caster->unit.player_index);

		/* port: a hologram plays at fighting, and its attacks do no damage
		(ai_adjust_damage, shadowrun_hologram_active) */
		actor->external_orders.playfighting = TRUE;

		/* (a hologram is the caster's: its side, as a summon's is its caster's) */
		if (player)
		{
			actor->meta.team_index = (short)player->team_index;
			hologram->object.owner_team_index = (short)player->team_index;
		}
		else if (caster->unit.actor_index != NONE)
		{
			struct actor_datum *caster_actor = actor_try_and_get(caster->unit.actor_index);

			if (caster_actor)
			{
				actor->meta.team_index = caster_actor->meta.team_index;
				hologram->object.owner_team_index = caster_actor->meta.team_index;
			}
		}
	}

	return actor_index;
}

/* 0x2657E0: each tree heals the players' units, and the actors' but those
summoned, raised or in a spirit animal's form, near it */
static void shadowrun_trees_heal(
	void)
{
	struct shadowrun_magic_globals *globals = shadowrun_magic_globals;
	long index;

	for (index = 0; index < globals->tree_count; index++)
	{
		struct shadowrun_standing_tree *tree = &globals->trees[index];

		if (object_try_and_get_and_verify_type(tree->object_index, NONE))
		{
			struct data_iterator iterator;
			struct player_datum *player;
			struct actor_datum *actor;

			data_iterator_new(&iterator, player_data);
			while ((player = data_iterator_next(&iterator)) != NULL)
			{
				if (player->unit_index != NONE)
				{
					shadowrun_tree_heal_unit(player->unit_index, tree);
				}
			}
			data_iterator_new(&iterator, actor_data);
			while ((actor = data_iterator_next(&iterator)) != NULL)
			{
				struct shadowrun_unit *healed = shadowrun_unit_get(actor->meta.unit_index);

				if (actor->meta.unit_index != NONE && healed &&
					!healed->summoned && !healed->undead && !healed->spirit_animal_active)
				{
					shadowrun_tree_heal_unit(actor->meta.unit_index, tree);
				}
			}
		}
		if (tree->heal_feedback_ticks == 0)
		{
			tree->heal_feedback_ticks = TREE_OF_LIFE_HEAL_FEEDBACK_TICKS;
		}
		else
		{
			tree->heal_feedback_ticks--;
		}
	}

	return;
}

/* 0x265950: the tree heals the unit, if it is near and hurt, by its rate
(an actor's thrice), and withers by as much of its own vitality; each second
the unit takes the tree's heal's damage and effect (the definitions are the
unit's own) */
static void shadowrun_tree_heal_unit(
	long unit_index,
	struct shadowrun_standing_tree *tree)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct object_datum *tree_object = object_try_and_get_and_verify_type(tree->object_index, NONE);

	if (unit && tree_object && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit) &&
		distance3d(&tree_object->object.position, &unit->object.position) < tree->heal_radius &&
		unit->object.body_vitality < 1.f)
	{
		struct shadowrun_magic1_definition *magic = shadowrun_magic1_get(unit_index);
		real heal = MIN(1.f - unit->object.body_vitality, tree->heal_rate);

		if (unit->unit.actor_index != NONE)
		{
			heal *= TREE_OF_LIFE_ACTOR_HEAL_SCALE;
		}
		unit->object.body_vitality += shadowrun_unit_heal_scale(unit_index) * heal;
		if (tree_object->object.maximum_body_vitality > 0.f)
		{
			tree_object->object.body_vitality -= heal * unit->object.maximum_body_vitality /
				tree_object->object.maximum_body_vitality;
		}
		object_port_check_body(tree->object_index);
		if (tree->heal_feedback_ticks == 0 && magic)
		{
			if (magic->tree_of_life_heal_damage.index != NONE)
			{
				struct damage_data damage;

				damage_data_new(&damage, magic->tree_of_life_heal_damage.index);
				SET_FLAG(damage.flags, _damage_area_of_effect_bit, TRUE);
				damage.owner_object_index = tree->object_index;
				damage.owner_team_index = tree_object->object.owner_team_index;
				damage.location = tree_object->object.location;
				damage.origin = tree_object->object.bounding_sphere_center;
				damage.epicenter = tree_object->object.bounding_sphere_center;
				vector_from_points3d(&tree_object->object.bounding_sphere_center, &unit->object.bounding_sphere_center,
					&damage.direction);
				damage.material_type = NONE;
				object_cause_damage(&damage, unit_index, NONE, NONE, NONE, NULL);
			}
			if (magic->tree_of_life_heal_effect.index != NONE)
			{
				effect_new_from_object(magic->tree_of_life_heal_effect.index, unit_index, unit_index, NONE, 0.f, 0.f,
					NULL, NULL);
			}
		}
	}

	return;
}

/* 0x268A10: smoke, cast, hides the caster (it cannot be damaged, passes
through others and casts no shadow) and camouflages it, while its mana lasts;
cast again it ends */
static void shadowrun_smoke_cast(
	long unit_index)
{
	struct shadowrun_magic3_definition *magic = shadowrun_magic3_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	if (shadowrun_unit->smoke_active)
	{
		shadowrun_smoke_end(unit_index);
	}
	else if (game_time_get() < shadowrun_unit->smoke_end_time + SMOKE_RECAST_WAIT)
	{
		shadowrun_unit->spell_failed[_shadowrun_spell_smoke] = TRUE;
	}
	else
	{
		struct biped_datum *biped = biped_get(unit_index);

		shadowrun_spell_spend(unit_index, _shadowrun_spell_smoke);
		shadowrun_unit->smoke_active = TRUE;
		SET_FLAG(unit->unit.flags, _unit_active_camouflaged_bit, TRUE);
		/* (it ends its smartlink, tech's, and its tech's movement: tech is not
		played yet) */
		shadowrun_unit_end_tech_movement(unit_index);
		unit_unzoom(unit_index);
		/* hidden */
		shadowrun_unit->smoke_hidden = TRUE;
		SET_FLAG(biped->biped.flags, _biped_movement_passes_through_bipeds_bit, TRUE);
		SET_FLAG(unit->unit.flags, _unit_no_falling_damage_bit, TRUE);
		SET_FLAG(unit->object.flags, _object_shadowless_bit, TRUE);
		ai_handle_unit_effect(unit_index, 1, shadowrun_ai_volume(magic->smoke_ai_unit_effect));
		shadowrun_unit_cast_feedback(unit_index, magic->smoke_cast_damage.index, magic->smoke_cast_effect.index);
		shadowrun_unit->smoke_shield_vitality = unit->object.shield_vitality;
		shadowrun_unit->smoke_maximum_shield_vitality = unit->object.maximum_shield_vitality;
		shadowrun_unit_stop_spell_effect(unit_index);
		if (magic->smoke_effect.index != NONE)
		{
			shadowrun_unit->spell_effect_index = effect_new_looping(magic->smoke_effect.index, unit_index, NONE, NONE,
				SMOKE_EFFECT_CHANGE_COLOR);
		}
	}

	return;
}

/* 0x268C30: smoke takes its mana each tick (it ends without), keeps the
caster's weapon unzoomed, hides its race's head once it is camouflaged, and
ends as the caster attacks, fires or chooses another spell */
static void shadowrun_smoke_update(
	long unit_index)
{
	struct shadowrun_magic3_definition *magic = shadowrun_magic3_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	if (shadowrun_unit->smoke_active)
	{
		real mana_cost_per_second = 0.f;

		shadowrun_spell_get(unit_index, _shadowrun_spell_smoke, NULL, &mana_cost_per_second, NULL, NULL, NULL);
		unit_unzoom(unit_index);
		if (!shadowrun_unit_spend_mana(unit_index, mana_cost_per_second / (real)TICKS_PER_SECOND, TRUE, FALSE))
		{
			shadowrun_unit_cast_failed_sound(unit_index);
			shadowrun_smoke_end(unit_index);
			return;
		}
	}
	if (unit->unit.active_camouflage > 0.f)
	{
		shadowrun_unit_show_head(unit_index, unit->unit.active_camouflage != magic->smoke_camouflage);
		if (TEST_FLAG(unit->unit.control_flags, _unit_control_use_equipment_bit))
		{
			shadowrun_smoke_end(unit_index);
			SET_FLAG(unit->unit.control_flags, _unit_control_use_equipment_bit, FALSE);
		}
		if (TEST_FLAG(unit->unit.control_flags, _unit_control_weapon_primary_trigger_bit))
		{
			shadowrun_smoke_end(unit_index);
			SET_FLAG(unit->unit.control_flags, _unit_control_weapon_primary_trigger_bit, FALSE);
		}
		if (shadowrun_unit->current_spell != _shadowrun_spell_smoke + 1)
		{
			/* (the executable clears its cast and grenade controls here: this
			build casts with the right bumper) */
			shadowrun_smoke_end(unit_index);
		}
	}

	return;
}

/* 0x268E90: smoke ends: the caster is seen again, has back the shields it
had, and its effect stops */
static void shadowrun_smoke_end(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit && shadowrun_unit->smoke_active)
	{
		struct biped_datum *biped = biped_get(unit_index);

		shadowrun_unit->smoke_active = FALSE;
		SET_FLAG(unit->unit.flags, _unit_active_camouflaged_bit, FALSE);
		shadowrun_unit_show_head(unit_index, TRUE);
		unit->object.shield_vitality = shadowrun_unit->smoke_shield_vitality;
		unit->object.maximum_shield_vitality = shadowrun_unit->smoke_maximum_shield_vitality;
		shadowrun_unit->smoke_end_time = game_time_get();
		if (shadowrun_unit->smoke_hidden)
		{
			shadowrun_unit->smoke_hidden = FALSE;
			SET_FLAG(biped->biped.flags, _biped_movement_passes_through_bipeds_bit, FALSE);
			SET_FLAG(unit->unit.flags, _unit_no_falling_damage_bit, FALSE);
			SET_FLAG(unit->object.flags, _object_shadowless_bit, FALSE);
		}
		shadowrun_unit_stop_spell_effect(unit_index);
	}

	return;
}

/* 0x26E030: spirit animal: the caster grows into its animal's form, its
effect on it, while its mana lasts; cast again it ends */
static void shadowrun_spirit_animal_cast(
	long unit_index)
{
	struct shadowrun_magic3_definition *magic = shadowrun_magic3_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	if (!shadowrun_unit->spirit_animal_active)
	{
		long mana_cost = 0;

		shadowrun_spell_get(unit_index, _shadowrun_spell_spirit_animal, &mana_cost, NULL, NULL, NULL, NULL);
		if (!shadowrun_unit_has_mana(unit_index, (real)mana_cost))
		{
			shadowrun_unit_cast_failed_sound(unit_index);
		}
		else
		{
			shadowrun_spell_spend(unit_index, _shadowrun_spell_spirit_animal);
			shadowrun_unit_cast_feedback(unit_index, magic->spirit_animal_cast_damage.index,
				magic->spirit_animal_cast_effect.index);
			shadowrun_unit->spirit_animal_growth_ticks = 0;
			if (unit->unit.actor_index != NONE)
			{
				shadowrun_unit->cast_cooldown_ticks = magic->spirit_animal_cooldown_ticks;
			}
			shadowrun_unit->spirit_animal_active = TRUE;
			shadowrun_unit_stop_spell_effect(unit_index);
			if (magic->spirit_animal_effect.index != NONE)
			{
				shadowrun_unit->spell_effect_index = effect_new_looping(magic->spirit_animal_effect.index, unit_index,
					NONE, NONE, NONE);
			}
		}
	}
	else
	{
		shadowrun_spirit_animal_end(unit_index);
	}

	return;
}

/* 0x26E1F0: spirit animal takes its mana each tick (it ends without); the
caster grows over its first second, while it casts */
static void shadowrun_spirit_animal_update(
	long unit_index)
{
	struct shadowrun_magic3_definition *magic = shadowrun_magic3_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	/* (the executable's is the unit's vehicle's, for a seat that shows its
	rider: a biped has none) */
	if (!magic || !shadowrun_unit)
	{
		return;
	}
	if (shadowrun_unit->spirit_animal_active)
	{
		real mana_cost_per_second = 0.f;

		shadowrun_spell_get(unit_index, _shadowrun_spell_spirit_animal, NULL, &mana_cost_per_second, NULL, NULL, NULL);
		if (!shadowrun_unit_spend_mana(unit_index, mana_cost_per_second / (real)TICKS_PER_SECOND, TRUE, FALSE))
		{
			shadowrun_unit_cast_failed_sound(unit_index);
			shadowrun_spirit_animal_end(unit_index);
			return;
		}
		if (shadowrun_unit->current_spell != _shadowrun_spell_spirit_animal + 1)
		{
			/* BUG (original, preserved): another spell chosen ends smoke, not
			the spirit animal */
			shadowrun_smoke_end(unit_index);
		}
	}
	if (shadowrun_unit->current_spell == _shadowrun_spell_spirit_animal + 1 && shadowrun_unit->cast_cooldown_ticks > 0)
	{
		real scale;

		shadowrun_unit->spirit_animal_growth_ticks = MIN(shadowrun_unit->spirit_animal_growth_ticks + 1,
			SPIRIT_ANIMAL_GROWTH_TICKS);
		scale = ((real)shadowrun_unit->spirit_animal_growth_ticks / (real)SPIRIT_ANIMAL_GROWTH_TICKS) * SPIRIT_ANIMAL_GROWTH;
		/* (its scale stays when it ends, until its race is set again) */
		unit->object.scale = scale + (shadowrun_unit->race_scale > 0.f ? shadowrun_unit->race_scale : 1.f);
	}

	return;
}

/* 0x26E430 */
static void shadowrun_spirit_animal_end(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit)
	{
		shadowrun_unit->spirit_animal_active = FALSE;
		shadowrun_unit_stop_spell_effect(unit_index);
	}

	return;
}

/* the spirit animal's death (0x277760's): the unit's body becomes its
animal's, which dies too */
void shadowrun_spirit_animal_died(
	long unit_index)
{
	struct shadowrun_magic3_definition *magic = shadowrun_magic3_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (magic && shadowrun_unit && shadowrun_unit->spirit_animal_active &&
		magic->spirit_animal_biped.index != NONE)
	{
		struct object_placement_data placement;
		long animal_index;

		object_placement_data_new(&placement, magic->spirit_animal_biped.index, NONE);
		placement.position = unit->object.position;
		placement.forward = unit->object.forward;
		animal_index = object_new(&placement);
		if (animal_index != NONE)
		{
			struct unit_datum *animal = unit_get(animal_index);
			struct shadowrun_magic2_definition *animal_magic;
			short index;

			animal->object.scale = SPIRIT_ANIMAL_DEATH_SCALE;
			animal->object.owner_team_index = unit->object.owner_team_index;
			for (index = 0; index < NUMBER_OF_OBJECT_CHANGE_COLORS; index++)
			{
				animal->object.outgoing_change_colors[index] = unit->object.outgoing_change_colors[index];
			}
			for (index = 0; index < SPIRIT_ANIMAL_DEATH_SETTLING_TICKS; index++)
			{
				object_port_update(animal_index);
			}
			/* it dies of its summon's dying damage (its own definition's) */
			animal_magic = shadowrun_magic2_get(animal_index);
			if (animal_magic)
			{
				shadowrun_undead_damage(animal_index, animal_magic->summon_dying_damage.index);
			}
		}
		/* port: deleted after this tick, not in the middle of its death */
		object_delete(unit_index);
	}

	return;
}

/* the looping effect of smoke or spirit animal stops */
static void shadowrun_unit_stop_spell_effect(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);

	if (shadowrun_unit && shadowrun_unit->spell_effect_index != NONE)
	{
		effect_stop(shadowrun_unit->spell_effect_index, TRUE);
		shadowrun_unit->spell_effect_index = NONE;
	}

	return;
}

/* 0x26B940: summon elemental. An actor of the definition's encounter comes
where the caster aims (up to the definition's range), on the caster's side,
in its colour, and lives the definition's time. Cast while its summons live,
it dismisses them instead */
static void shadowrun_summon_cast(
	long unit_index)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct object_marker markers[1];
	real_point3d origin;
	real_vector3d vector;
	long mana_cost = 0;
	long capacity_reserved = 0;
	short living = 0;
	short index;

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	if (object_get_marker_by_name(unit_index, "head", markers, NUMBEROF(markers)) > 0)
	{
		origin = markers[0].matrix.position;
	}
	else
	{
		/* port: the executable assumes the unit has the marker */
		unit_get_head_position(unit_index, &origin);
	}
	point_from_line3d(&origin, &unit->unit.aiming_vector, SUMMON_ORIGIN_OFFSET, &origin);
	scale_vector3d(&unit->unit.aiming_vector, magic->summon_range, &vector);
	shadowrun_spell_get(unit_index, _shadowrun_spell_summon_elemental, &mana_cost, NULL, &capacity_reserved, NULL, NULL);

	/* the summons gone or dead give back what they held */
	for (index = 0; index < MAXIMUM_SHADOWRUN_SUMMONS; index++)
	{
		long actor_index = shadowrun_unit->summoned_actor_indices[index];

		if (actor_index != NONE)
		{
			struct actor_datum *actor = actor_try_and_get(actor_index);
			struct unit_datum *summon = actor ? unit_try_and_get(actor->meta.unit_index) : NULL;

			if (summon && !TEST_FLAG(summon->object.damage_flags, _object_dead_bit))
			{
				living++;
			}
			else
			{
				shadowrun_unit_release_capacity(unit_index, capacity_reserved);
				shadowrun_unit->summoned_actor_indices[index] = NONE;
			}
		}
	}

	if (living > 0)
	{
		/* dismissed */
		ai_handle_unit_effect(unit_index, 1, shadowrun_ai_volume(magic->summon_ai_unit_effect));
		for (index = 0; index < MAXIMUM_SHADOWRUN_SUMMONS; index++)
		{
			long actor_index = shadowrun_unit->summoned_actor_indices[index];

			if (actor_index != NONE && actor_try_and_get(actor_index))
			{
				actor_kill(actor_index, TRUE, FALSE);
				shadowrun_unit_release_capacity(unit_index, capacity_reserved);
				shadowrun_unit->summoned_actor_indices[index] = NONE;
				shadowrun_unit->cast_cooldown_ticks = magic->summon_cooldown_ticks;
			}
		}
		shadowrun_unit_cast_failed_sound(unit_index);
	}
	else if (shadowrun_unit->power_cooldown_ticks[_shadowrun_spell_summon_elemental] == 0)
	{
		struct collision_result collision;
		real_point3d target;
		char encounter_name[sizeof(magic->summon_encounter) + 1];
		long ai_reference = NONE;
		long actor_variant_index;

		if (collision_test_vector(_collision_test_for_projectiles_flags, &origin, &vector, unit_index, &collision))
		{
			target = collision.point;
		}
		else
		{
			add_vectors3d((real_vector3d const *)&origin, &vector, (real_vector3d *)&target);
		}
		shadowrun_unit->cast_cooldown_ticks = magic->summon_cooldown_ticks;
		csmemcpy(encounter_name, magic->summon_encounter, sizeof(magic->summon_encounter));
		encounter_name[sizeof(magic->summon_encounter)] = 0;
		ai_index_from_string(global_scenario_get(), encounter_name, &ai_reference);
		/* port: the map's elite commander, where it has one, comes in the
		encounter's actor's place (the prototype's elemental) */
		actor_variant_index = shadowrun_summon_elemental_variant();
		/* port: said, so that a summon's actor is known to have been found */
		if (actor_variant_index != NONE)
		{
			error(_error_silent, "shadowrun: summon elemental summons %s", tag_get_name(actor_variant_index));
		}
		if (!shadowrun_unit_has_mana(unit_index, (real)mana_cost) ||
			!shadowrun_unit_can_reserve_capacity(unit_index, capacity_reserved))
		{
			shadowrun_unit_cast_failed(unit_index, _shadowrun_spell_summon_elemental);
		}
		else
		{
			shadowrun_unit_cast_feedback(unit_index, magic->summon_cast_damage.index, magic->summon_cast_effect.index);
			if (ai_reference != NONE || actor_variant_index != NONE)
			{
				/* (the map's elite commander comes where the caster aimed; one of
				the encounter's comes in the encounter's place, and is moved there
				below) */
				long actor_index = actor_variant_index != NONE ?
					shadowrun_summon_elemental_place(actor_variant_index, unit_index, &target) :
					encounter_port_create_actor(ai_reference & UNSIGNED_SHORT_MAX);
				struct actor_datum *actor = actor_index != NONE ? actor_try_and_get(actor_index) : NULL;

				/* port: said, as a summon that does not come is otherwise silent */
				if (!actor)
				{
					error(_error_silent, "shadowrun: summon elemental placed no actor (%ld)", actor_variant_index);
				}

				shadowrun_unit_fire_projectile(unit_index, magic->summon_projectile.index, NULL);
				shadowrun_unit->summon_time = game_time_get();
				if (actor)
				{
					struct player_datum *player = player_try_and_get(unit->unit.player_index);
					long summon_index = actor->meta.unit_index;
					struct unit_datum *summon = unit_get(summon_index);
					struct shadowrun_unit *shadowrun_summon = shadowrun_unit_get(summon_index);

					for (index = 0; index < MAXIMUM_SHADOWRUN_SUMMONS; index++)
					{
						if (shadowrun_unit->summoned_actor_indices[index] == NONE)
						{
							shadowrun_unit->summoned_actor_indices[index] = actor_index;
							break;
						}
					}
					/* (the executable marks the summon's encounter here, for its
					own AI) */
					if (player)
					{
						real_rgb_color color;

						actor->meta.team_index = (short)player->team_index;
						summon->object.owner_team_index = (short)player->team_index;
						if (shadowrun_summon)
						{
							shadowrun_summon->summoned = TRUE;
							shadowrun_summon->summon_lifetime_ticks = (long)(magic->summon_lifetime * TICKS_PER_SECOND);
							shadowrun_summon->master_player_index = unit->unit.player_index;
							shadowrun_summon->master_unit_index = unit_index;
						}
						game_engine_player_get_change_color(&color, unit->unit.player_index);
						if (shadowrun_summon && magic->summon_names.index != NONE)
						{
							struct string_list *names = unicode_string_list_definition_get(magic->summon_names.index);

							shadowrun_summon->summon_name_index = names->strings.count > 0 ?
								random_range(0, (short)names->strings.count) : 0;
						}
						for (index = 0; index < NUMBER_OF_OBJECT_CHANGE_COLORS; index++)
						{
							summon->object.outgoing_change_colors[index] = color;
						}
						/* port: one of the encounter's is moved where the caster aimed; the
						elite commander was placed there */
						if (actor_variant_index == NONE)
						{
							object_set_position(summon_index, &target, NULL, NULL);
						}
						if (magic->summon_birth_damage.index != NONE)
						{
							struct damage_data damage;

							damage_data_new(&damage, magic->summon_birth_damage.index);
							SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
							negate_vector3d(&unit->unit.aiming_vector, &damage.direction);
							object_cause_damage(&damage, unit_index, NONE, NONE, NONE, NULL);
						}
						if (magic->summon_birth_effect.index != NONE)
						{
							effect_new_from_object(magic->summon_birth_effect.index, summon_index, summon_index, NONE,
								0.f, 0.f, NULL, NULL);
						}
					}
					else if (shadowrun_summon)
					{
						/* BUG (original, preserved): an actor's summon keeps the
						encounter's side and colour, is never marked a summon and
						lives on */
						shadowrun_summon->master_unit_index = unit_index;
					}
					shadowrun_spell_spend(unit_index, _shadowrun_spell_summon_elemental);
				}
			}
		}
	}
	else
	{
		shadowrun_unit->spell_failed[_shadowrun_spell_summon_elemental] = TRUE;
	}

	return;
}

/* 0x26B1A0: the summons gone or dead give back what they held */
static void shadowrun_summons_update(
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long capacity_reserved = 0;
	short index;

	if (!shadowrun_unit || !shadowrun_magic2_get(unit_index))
	{
		return;
	}
	shadowrun_spell_get(unit_index, _shadowrun_spell_summon_elemental, NULL, NULL, &capacity_reserved, NULL, NULL);
	for (index = 0; index < MAXIMUM_SHADOWRUN_SUMMONS; index++)
	{
		long actor_index = shadowrun_unit->summoned_actor_indices[index];

		if (actor_index != NONE)
		{
			struct actor_datum *actor = actor_try_and_get(actor_index);
			struct unit_datum *summon = actor ? unit_try_and_get(actor->meta.unit_index) : NULL;

			if (!summon || TEST_FLAG(summon->object.damage_flags, _object_dead_bit))
			{
				shadowrun_unit_release_capacity(unit_index, capacity_reserved);
				shadowrun_unit->summoned_actor_indices[index] = NONE;
			}
		}
	}

	return;
}

/* 0x27C8D0: the unit's summons are let go: a player's die in turn (as
dismissed), an actor's are killed (or all, if kill) */
static void shadowrun_summons_release(
	long unit_index,
	boolean kill)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	short index;

	for (index = 0; index < MAXIMUM_SHADOWRUN_SUMMONS; index++)
	{
		long actor_index = shadowrun_unit->summoned_actor_indices[index];

		if (actor_index != NONE)
		{
			struct actor_datum *actor = actor_try_and_get(actor_index);
			long capacity_reserved = 0;

			shadowrun_spell_get(unit_index, _shadowrun_spell_summon_elemental, NULL, NULL, &capacity_reserved, NULL, NULL);
			shadowrun_unit_release_capacity(unit_index, capacity_reserved);
			if (actor)
			{
				struct shadowrun_unit *summon = shadowrun_unit_get(actor->meta.unit_index);

				/* (a caster dead for good has no actor any more: its summons
				are dismissed, as a player's are) */
				if (!kill && unit->unit.actor_index == NONE && summon)
				{
					summon->summon_dismissed = TRUE;
				}
				else
				{
					actor_kill(actor_index, TRUE, FALSE);
				}
			}
			shadowrun_unit->summoned_actor_indices[index] = NONE;
		}
	}

	return;
}

/* 0x26C700: resurrection. A ball grows before the caster for the
definition's ticks; the bodies it reaches in front of the caster rise on the
caster's side (shadowrun_resurrection_update). Its mana is spent as they
rise. Team games' resurrection (a dead player rises as a zombie of the
caster's) is not played */
static boolean shadowrun_resurrection_cast(
	long unit_index)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long mana_cost = 0;
	long spell_cooldown_ticks = 0;
	short index;

	if (!magic || !shadowrun_unit || shadowrun_unit->resurrecting)
	{
		return FALSE;
	}
	shadowrun_spell_get(unit_index, _shadowrun_spell_resurrection, &mana_cost, NULL, NULL, NULL,
		&spell_cooldown_ticks);
	if (!shadowrun_unit_can_reserve_capacity(unit_index, magic->resurrection_capacity_reserved))
	{
		shadowrun_unit_cast_failed(unit_index, _shadowrun_spell_resurrection);
		return FALSE;
	}
	/* the actors it raised that are gone, or dead, are forgotten */
	for (index = 0; index < MAXIMUM_SHADOWRUN_RESURRECTED_ACTORS; index++)
	{
		long actor_index = shadowrun_unit->resurrected_actor_indices[index];

		if (actor_index != NONE)
		{
			struct actor_datum *actor = actor_try_and_get(actor_index);
			struct unit_datum *raised = actor ? unit_try_and_get(actor->meta.unit_index) : NULL;

			if (!raised || TEST_FLAG(raised->object.damage_flags, _object_dead_bit))
			{
				shadowrun_unit->resurrected_actor_indices[index] = NONE;
			}
		}
	}
	if (!shadowrun_unit_has_mana(unit_index, (real)mana_cost))
	{
		shadowrun_unit_cast_failed(unit_index, _shadowrun_spell_resurrection);
		return FALSE;
	}

	ai_handle_unit_effect(unit_index, 1, shadowrun_ai_volume(magic->resurrection_ai_unit_effect));
	shadowrun_unit->cast_cooldown_ticks = unit->unit.actor_index == NONE ?
		magic->resurrection_cooldown_ticks : magic->resurrection_actor_cooldown_ticks;
	shadowrun_unit->power_cooldown_ticks[_shadowrun_spell_resurrection] = spell_cooldown_ticks;
	shadowrun_unit->resurrecting = TRUE;
	shadowrun_unit->resurrection_ball_index = NONE;
	if (magic->resurrection_ball.index != NONE)
	{
		struct object_placement_data placement;

		object_placement_data_new(&placement, magic->resurrection_ball.index, unit_index);
		placement.position = unit->object.position;
		perpendicular3d(global_up3d, &placement.forward);
		normalize3d(&placement.forward);
		shadowrun_unit->resurrection_ball_index = object_new(&placement);
		/* port: the executable assumes the ball was made */
		if (shadowrun_unit->resurrection_ball_index != NONE)
		{
			object_get(shadowrun_unit->resurrection_ball_index)->object.scale = RESURRECTION_BALL_STARTING_SCALE;
		}
	}

	return TRUE;
}

/* 0x26CB80: the ball grows before the caster (a player's moves with its
aim) and the bodies it reaches rise, while it grows (in single player) */
static void shadowrun_resurrection_update(
	long unit_index)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct object_datum *ball;
	real ball_scale = 0.f;

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
	{
		shadowrun_unit->resurrecting = FALSE;
		shadowrun_unit->cast_cooldown_ticks = 0;
		if (shadowrun_unit->resurrection_ball_index != NONE)
		{
			if (object_try_and_get_and_verify_type(shadowrun_unit->resurrection_ball_index, NONE))
			{
				object_delete_immediately(shadowrun_unit->resurrection_ball_index);
			}
			/* port: the executable keeps the deleted ball's index */
			shadowrun_unit->resurrection_ball_index = NONE;
		}
		return;
	}
	if (!shadowrun_unit->resurrecting)
	{
		return;
	}

	shadowrun_unit->cast_cooldown_ticks--;
	ball = shadowrun_unit->resurrection_ball_index != NONE ?
		object_try_and_get_and_verify_type(shadowrun_unit->resurrection_ball_index, NONE) : NULL;
	if (ball)
	{
		if (unit->unit.actor_index == NONE)
		{
			real_vector3d aim;
			real_point3d position = unit->object.position;

			ball->object.scale = (real)(magic->resurrection_cooldown_ticks - shadowrun_unit->cast_cooldown_ticks) /
				(real)magic->resurrection_cooldown_ticks;
			unit_get_aiming_vector(unit_index, &aim);
			aim.k = 0.f;
			normalize3d(&aim);
			scale_vector3d(&aim, ball->object.scale, &aim);
			position.x += aim.i;
			position.y += aim.j;
			position.z += aim.k + RESURRECTION_BALL_LIFT;
			object_set_position(shadowrun_unit->resurrection_ball_index, &position, &unit->object.forward,
				&unit->object.up);
		}
		else
		{
			/* BUG (original, preserved): an actor's ball grows over its own ticks,
			measured against a player's */
			ball->object.scale = (real)(magic->resurrection_actor_cooldown_ticks - shadowrun_unit->cast_cooldown_ticks) /
				(real)magic->resurrection_cooldown_ticks;
		}
		ball_scale = ball->object.scale;
	}

	/* (in single player the bodies rise as the ball grows, and in a team game
	once it is grown) */
	{
		long mana_cost = 0;

		if (shadowrun_unit->cast_cooldown_ticks < 1)
		{
			shadowrun_unit->resurrecting = FALSE;
			if (shadowrun_unit->resurrection_ball_index != NONE)
			{
				if (ball)
				{
					object_delete_immediately(shadowrun_unit->resurrection_ball_index);
				}
				shadowrun_unit->resurrection_ball_index = NONE;
			}
			shadowrun_unit_cast_feedback(unit_index, magic->resurrection_finish_damage.index,
				magic->resurrection_finish_effect.index);
		}
		if (unit->unit.actor_index != NONE)
		{
			unit->object.shield_vitality = RESURRECTION_ACTOR_SHIELD_VITALITY;
		}
		shadowrun_spell_get(unit_index, _shadowrun_spell_resurrection, &mana_cost, NULL, NULL, NULL, NULL);
		if (shadowrun_unit_can_reserve_capacity(unit_index, magic->resurrection_capacity_reserved))
		{
			long object_indices[RESURRECTION_MAXIMUM_BODIES];
			real_point3d center = unit->object.bounding_sphere_center;
			real reach = ball_scale * magic->resurrection_radius + unit->object.bounding_sphere_radius;
			boolean raised = FALSE;
			short count;
			short index;

			count = objects_in_sphere(0, _object_mask_biped, &unit->object.location, &center,
				magic->resurrection_radius, object_indices, NUMBEROF(object_indices));
			for (index = 0; index < count; index++)
			{
				struct biped_datum *body = biped_get(object_indices[index]);
				struct shadowrun_unit *shadowrun_body = shadowrun_unit_get(object_indices[index]);

				/* (in single player any dead body; a team game's a teammate's) */
				if (shadowrun_body && !shadowrun_body->undead &&
					TEST_FLAG(body->object.damage_flags, _object_dead_bit) &&
					distance_squared3d(&body->object.bounding_sphere_center, &center) <= reach * reach)
				{
					real_matrix4x3 matrix;
					real_vector3d to_body;
					real_vector3d local;
					real angle;

					/* the bodies before the caster */
					vector_from_points3d(&center, &body->object.bounding_sphere_center, &to_body);
					object_get_world_matrix(unit_index, &matrix);
					matrix4x3_inverse_transform_vector(&matrix, &to_body, &local);
					angle = arctangent(local.j, local.i);
					if (angle >= -(real)(M_PI / 2.) && angle <= (real)(M_PI / 2.))
					{
						raised = TRUE;
						if (!shadowrun_body->summoned)
						{
							shadowrun_resurrection_raise(unit_index, &body->object.bounding_sphere_center,
								shadowrun_body->race_index);
						}
						object_delete_immediately(object_indices[index]);
					}
				}
			}
			if (raised)
			{
				shadowrun_unit_spend_mana(unit_index, (real)mana_cost, FALSE, FALSE);
			}
		}
	}

	return;
}

/* 0x26D800: a body rises where it lay, as an actor of the definition's
encounter on the caster's side, in the caster's colour: it lies there and
gets up, as one feigning death does. The caster keeps six; the seventh takes
the place of the one furthest from it, which is to die */
static void shadowrun_resurrection_raise(
	long unit_index,
	real_point3d const *position,
	long race_index)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct unit_datum *unit = unit_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	char encounter_name[sizeof(magic->resurrection_encounter) + 1];
	long ai_reference = NONE;
	short slot = NONE;
	short furthest = 0;
	real furthest_distance_squared = 0.f;
	short index;

	for (index = 0; index < MAXIMUM_SHADOWRUN_RESURRECTED_ACTORS; index++)
	{
		long actor_index = shadowrun_unit->resurrected_actor_indices[index];
		struct actor_datum *actor = actor_index != NONE ? actor_try_and_get(actor_index) : NULL;
		struct unit_datum *raised;
		real distance_squared;

		if (!actor)
		{
			shadowrun_unit->resurrected_actor_indices[index] = NONE;
			slot = index;
			break;
		}
		raised = unit_try_and_get(actor->meta.unit_index);
		/* port: the executable assumes the actor has its unit */
		distance_squared = raised ? distance_squared3d(&raised->object.position, &unit->object.position) : 0.f;
		if (furthest_distance_squared < distance_squared)
		{
			furthest = index;
			furthest_distance_squared = distance_squared;
		}
	}
	if (slot == NONE)
	{
		struct actor_datum *actor = actor_try_and_get(shadowrun_unit->resurrected_actor_indices[furthest]);
		struct shadowrun_unit *replaced = actor ? shadowrun_unit_get(actor->meta.unit_index) : NULL;

		if (replaced)
		{
			replaced->doomed = TRUE;
		}
		slot = furthest;
		shadowrun_unit->resurrected_actor_indices[furthest] = NONE;
	}

	csmemcpy(encounter_name, magic->resurrection_encounter, sizeof(magic->resurrection_encounter));
	encounter_name[sizeof(magic->resurrection_encounter)] = 0;
	ai_index_from_string(global_scenario_get(), encounter_name, &ai_reference);
	if (ai_reference != NONE)
	{
		long actor_index = encounter_port_create_actor(ai_reference & UNSIGNED_SHORT_MAX);
		struct actor_datum *actor = actor_index != NONE ? actor_try_and_get(actor_index) : NULL;

		if (actor)
		{
			long raised_index = actor->meta.unit_index;
			struct unit_datum *raised = unit_get(raised_index);
			struct shadowrun_unit *shadowrun_raised = shadowrun_unit_get(raised_index);
			struct player_datum *player = player_try_and_get(unit->unit.player_index);
			struct actor_datum *caster_actor = unit->unit.actor_index != NONE ?
				actor_try_and_get(unit->unit.actor_index) : NULL;
			real_rgb_color color;
			short team_index = NONE;
			short tick;

			shadowrun_unit->resurrected_actor_indices[slot] = actor_index;
			/* BUG (original, preserved): only the first of them holds back the
			capacity */
			if (slot == 0)
			{
				shadowrun_unit_reserve_capacity(unit_index, magic->resurrection_capacity_reserved);
			}
			if (player)
			{
				team_index = (short)player->team_index;
			}
			if (caster_actor)
			{
				team_index = caster_actor->meta.team_index;
			}
			actor->meta.team_index = team_index;
			raised->object.owner_team_index = team_index;
			if (shadowrun_raised)
			{
				shadowrun_raised->undead = TRUE;
				shadowrun_raised->master_player_index = unit->unit.player_index;
				shadowrun_raised->master_unit_index = unit_index;
			}
			shadowrun_unit_set_race(raised_index, race_index);
			if (player)
			{
				game_engine_player_get_change_color(&color, unit->unit.player_index);
			}
			else
			{
				color = shadowrun_globals_undead_color();
			}
			for (index = 0; index < NUMBER_OF_OBJECT_CHANGE_COLORS; index++)
			{
				raised->object.outgoing_change_colors[index] = color;
			}
			object_set_position(raised_index, position, NULL, NULL);
			/* it lies dead, and gets up */
			SET_FLAG(raised->object.damage_flags, _object_dead_bit, TRUE);
			raised->unit.feign_death_timer = random_range(1, 15) + 30;
			match_assert("c:\\halo\\SOURCE\\units\\units.c", 7612, raised->unit.feign_death_timer > 0);
			unit_died(raised_index, TRUE);
			for (tick = 0; tick < RESURRECTION_SETTLING_TICKS; tick++)
			{
				object_port_update(raised_index);
			}
		}
	}

	return;
}

/* 0x27CA10: those the unit raised are to die, and it has back what they
held */
static void shadowrun_resurrection_release(
	long unit_index)
{
	struct shadowrun_magic2_definition *magic = shadowrun_magic2_get(unit_index);
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long capacity_reserved = 0;
	boolean released = FALSE;
	short index;

	if (!magic || !shadowrun_unit)
	{
		return;
	}
	/* BUG (original, preserved): in single player it gives back the level's
	capacity, which raising never held back (the definition's was) */
	shadowrun_spell_get(unit_index, _shadowrun_spell_resurrection, NULL, NULL, &capacity_reserved, NULL, NULL);
	for (index = 0; index < MAXIMUM_SHADOWRUN_RESURRECTED_ACTORS; index++)
	{
		long actor_index = shadowrun_unit->resurrected_actor_indices[index];

		if (actor_index != NONE)
		{
			struct actor_datum *actor = actor_try_and_get(actor_index);

			if (actor)
			{
				struct shadowrun_unit *raised = shadowrun_unit_get(actor->meta.unit_index);

				if (raised)
				{
					raised->doomed = TRUE;
				}
				released = TRUE;
			}
			shadowrun_unit->resurrected_actor_indices[index] = NONE;
		}
	}
	if (released)
	{
		shadowrun_unit_release_capacity(unit_index, capacity_reserved);
	}

	return;
}

/* the damage one raised takes (as 0x284260 gives it): from all about it */
static void shadowrun_undead_damage(
	long unit_index,
	long damage_definition_index)
{
	struct unit_datum *unit = unit_get(unit_index);

	if (damage_definition_index != NONE)
	{
		struct damage_data damage;

		damage_data_new(&damage, damage_definition_index);
		SET_FLAG(damage.flags, _damage_area_of_effect_bit, TRUE);
		damage.owner_team_index = unit->object.owner_team_index;
		damage.origin = unit->object.bounding_sphere_center;
		damage.epicenter = unit->object.bounding_sphere_center;
		damage.direction.i = 1.f - 2.f * real_random_range(0.f, 1.f);
		damage.direction.j = 1.f - 2.f * real_random_range(0.f, 1.f);
		damage.direction.k = 1.f - 2.f * real_random_range(0.f, 1.f);
		normalize3d(&damage.direction);
		object_cause_damage(&damage, unit_index, NONE, NONE, NONE, NULL);
	}

	return;
}

/* 0x26AF60: a projectile from the unit's body, along its aim or at target.
The ticks it takes to get there, at its final velocity */
static long shadowrun_unit_fire_projectile(
	long unit_index,
	long projectile_definition_index,
	real_point3d const *target)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct object_marker markers[1];
	real_point3d origin;
	real_vector3d vector;
	real distance = 0.f;
	long ticks = 0;

	if (object_get_marker_by_name(unit_index, "body", markers, NUMBEROF(markers)) > 0)
	{
		origin = markers[0].matrix.position;
	}
	else
	{
		origin = unit ? unit->object.position : *global_origin3d;
	}
	if (target)
	{
		vector_from_points3d(&origin, target, &vector);
		distance = magnitude3d(&vector);
		normalize3d(&vector);
	}
	else
	{
		unit_get_aiming_vector(unit_index, &vector);
	}
	if (projectile_definition_index != NONE)
	{
		struct projectile_definition *definition =
			tag_get(PROJECTILE_DEFINITION_TAG, projectile_definition_index);
		struct object_placement_data placement;
		boolean player_owned = unit && unit->unit.player_index != NONE;
		long projectile_index;

		if (definition->projectile.final_velocity != 0.f)
		{
			ticks = (long)(distance / definition->projectile.final_velocity);
		}
		object_placement_data_new(&placement, projectile_definition_index, unit_index);
		placement.position = origin;
		placement.forward = vector;
		perpendicular3d(&vector, &placement.up);
		normalize3d(&placement.up);
		placement.translational_velocity = *global_zero_vector3d;
		if (player_owned)
		{
			SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
		}
		projectile_index = object_new(&placement);
		if (projectile_index != NONE && player_owned)
		{
			real_point3d camera;

			unit_get_camera_position(unit_index, &camera);
			object_force_inside_bsp(projectile_index, &camera);
		}
	}

	return ticks;
}

/* 0x252120: a biped pushed (half as hard alive), into the air, spinning if
dead or so made, and facing the push if face */
static void shadowrun_biped_push(
	long biped_index,
	real_vector3d *impulse,
	boolean face)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	if (!TEST_FLAG(definition->unit.flags, _unit_is_special_bit))
	{
		boolean dead = TEST_FLAG(biped->object.damage_flags, _object_dead_bit);

		if (!dead)
		{
			scale_vector3d(impulse, 0.5f, impulse);
		}
		else
		{
			biped_start_limp_body_physics(biped_index);
		}
		add_vectors3d(&biped->object.translational_velocity, impulse, &biped->object.translational_velocity);
		SET_FLAG(biped->object.flags, _object_at_rest_bit, FALSE);
		SET_FLAG(biped->biped.flags, _biped_slipping_bit, TRUE);
		SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
		if (dead ||
			TEST_FLAG(definition->biped.flags, _biped_flying_bit) ||
			TEST_FLAG(definition->biped.flags, _biped_climbs_anything_bit))
		{
			real_vector3d axis;

			cross_product3d(global_up3d, impulse, &axis);
			normalize3d(&axis);
			point_from_line3d((real_point3d const *)&biped->object.angular_velocity, &axis,
				magnitude3d(impulse) * real_random_range(0.f, 1.f) * (real)(M_PI / 2.),
				(real_point3d *)&biped->object.angular_velocity);
		}
		if (biped->object.parent_object_index == NONE)
		{
			real_vector3d direction = *impulse;

			if (normalize3d(&direction) > 0.f && face)
			{
				biped->object.forward = direction;
				biped_port_snap_facing(biped_index);
			}
		}
	}

	return;
}

/* 0x287780: a vehicle pushed, and spun about the push */
static void shadowrun_vehicle_push(
	long vehicle_index,
	real_vector3d const *impulse)
{
	struct vehicle_datum *vehicle = vehicle_datum_get(vehicle_index);
	struct unit_definition *definition = vehicle_definition_get(vehicle->definition_index);

	if (definition->object.physics.index != NONE)
	{
		real_vector3d axis;
		real magnitude;

		add_vectors3d(&vehicle->object.translational_velocity, impulse, &vehicle->object.translational_velocity);
		cross_product3d(global_up3d, impulse, &axis);
		magnitude = normalize3d(&axis);
		if (magnitude > 0.f)
		{
			scale_vector3d(&axis, magnitude * (real)M_PI, &axis);
			add_vectors3d(&axis, &vehicle->object.angular_velocity, &vehicle->object.angular_velocity);
		}
		SET_FLAG(vehicle->object.flags, _object_at_rest_bit, FALSE);
	}

	return;
}

/* 0x25B650: the unit stops its tech's movement (gecko, grapple, glider).
Tech is not played yet, so no unit has any to stop */
static void shadowrun_unit_end_tech_movement(
	long unit_index)
{
	(void)unit_index;

	return;
}

/* the unit's own damage (as 0x26AEC0 gives it), from direction (its aim's
back when NULL) */
static void shadowrun_unit_damage_self(
	long unit_index,
	long damage_definition_index,
	real_vector3d const *direction)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);

	if (unit && damage_definition_index != NONE)
	{
		struct damage_data damage;

		damage_data_new(&damage, damage_definition_index);
		SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
		if (direction)
			damage.direction = *direction;
		else
			negate_vector3d(&unit->unit.aiming_vector, &damage.direction);
		object_cause_damage(&damage, unit_index, NONE, NONE, NONE, NULL);
	}

	return;
}

static void shadowrun_unit_effect(
	long unit_index,
	long effect_definition_index)
{
	if (effect_definition_index != NONE)
	{
		effect_new_from_object(effect_definition_index, unit_index, unit_index, NONE, 0.f, 0.f, NULL, NULL);
	}

	return;
}

/* a definition's volume for ai_handle_unit_effect, in its range */
short shadowrun_ai_volume(
	short volume)
{
	return (short)PIN(volume, 0, SHADOWRUN_AI_SOUND_VOLUME_COUNT - 1);
}

/* the globals' Shadowrun block's colour of those an actor raised */
static real_rgb_color shadowrun_globals_undead_color(
	void)
{
	struct game_globals *globals = scenario_get_game_globals();
	real_rgb_color result = *global_real_rgb_white;

	if (globals)
	{
		struct tag_block const *block =
			(struct tag_block const *)((byte const *)globals + SHADOWRUN_GLOBALS_BLOCK_OFFSET);

		if (block->count > 0 &&
			cache_file_tag_cache_contains(block->address, SHADOWRUN_GLOBALS_UNDEAD_COLOR_OFFSET + sizeof(result)))
		{
			result = *(real_rgb_color const *)((byte const *)block->address + SHADOWRUN_GLOBALS_UNDEAD_COLOR_OFFSET);
		}
	}

	return result;
}

/* the globals' Shadowrun block's sound of a strangling broken, or NONE */
static long shadowrun_globals_strangle_broken_sound(
	void)
{
	struct game_globals *globals = scenario_get_game_globals();
	long result = NONE;

	if (globals)
	{
		struct tag_block const *block =
			(struct tag_block const *)((byte const *)globals + SHADOWRUN_GLOBALS_BLOCK_OFFSET);
		long const *sound;

		if (block->count > 0 &&
			cache_file_tag_cache_contains(block->address, SHADOWRUN_GLOBALS_STRANGLE_BROKEN_SOUND_OFFSET + 4))
		{
			sound = (long const *)((byte const *)block->address + SHADOWRUN_GLOBALS_STRANGLE_BROKEN_SOUND_OFFSET);
			if (cache_file_tag_index_group_tag(*sound) == 'snd!')
			{
				result = *sound;
			}
		}
	}

	return result;
}
