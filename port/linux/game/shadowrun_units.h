/*
SHADOWRUN_UNITS.H

What the Shadowrun prototype adds to a unit and a player (shadowrun.h), and
its magic, shared by shadowrun_units.c and shadowrun_magic.c.

Each field names where the executable keeps it: a unit's at its offset in
the unit's datum (its datum is laid out otherwise than this build's: it is
Halo's of December 2001 with Shadowrun's fields after it, from 0x440, and
the biped's after those, from 0x101C), a player's in the player's datum.
*/

#ifndef __SHADOWRUN_UNITS_H
#define __SHADOWRUN_UNITS_H
#pragma once

/* ---------- headers */

#include "shadowrun.h"
#include "shadowrun_definitions.h"

/* ---------- constants */

/* port: the units a vision marks at once (shadowrun_tech.c) */
#define MAXIMUM_SHADOWRUN_VISION_TARGETS 32

/* port: the techs, as FUN_0027d4e0 numbers them (shadowrun_tech.h's) */
#define MAXIMUM_SHADOWRUN_TECHS 15

/* port: the spells and the techs a player may have at once (no more are bought:
its buy menu says so by not selling them) */
#define MAXIMUM_SHADOWRUN_PLAYER_SPELLS 2
#define MAXIMUM_SHADOWRUN_PLAYER_TECHS 2

enum
{
	/* a grenade type the executable has beyond this build's two (the
	globals' grenades: frag, plasma, antimagic, tree of life, smoke) */
	SHADOWRUN_TREE_OF_LIFE_GRENADE_TYPE = 3,

	/* smoke may be cast again so long after it ended (ticks) */
	SMOKE_RECAST_WAIT = 15,

	/* the actors a unit's Resurrection keeps raised at once, and the summons
	it keeps */
	MAXIMUM_SHADOWRUN_RESURRECTED_ACTORS = 6,
	MAXIMUM_SHADOWRUN_SUMMONS = 5,

	/* strangle's thorns growing at once, a unit's (the executable's list has
	no bound), and the thorns kept in all (the definitions' bound is 100) */
	MAXIMUM_SHADOWRUN_GROWING_THORNS = 32,
	MAXIMUM_SHADOWRUN_THORNS = 128,

	/* a unit's trees of life: its last four, which grow, and the ten
	before; and the trees standing in all */
	MAXIMUM_SHADOWRUN_TREES = 4,
	MAXIMUM_SHADOWRUN_OLD_TREES = 10,
	MAXIMUM_SHADOWRUN_STANDING_TREES = 64,

	/* rage counts the damage of the last half second, a tick each */
	SHADOWRUN_RAGE_DAMAGE_TICKS = 15,
};

/* ---------- structures */

/* a thorn growing (strangle's): to its scale over the definition's ticks */
struct shadowrun_growing_thorn
{
	long object_index;
	real scale;
	short ticks;
	short delay_ticks;
};

/* a unit's (after its datum) */
struct shadowrun_unit
{
	/* its race: its srud's race and each spell's level (0x794); the scale
it gives (0x798), and the scenery it wears on its head (0x10EC) and its
definition (0x10F0) */
	long race_index;
	real race_scale;
	long head_object_index;
	long head_definition_index;

	/* the spell the right bumper casts, and the one it would have
	(spell + 1, 0 for none: 0x564, 0x568) */
	long current_spell;
	long desired_spell;
	/* the magic and tech it knows (0x4B0) and how many (0x550) */
	long known_powers[MAXIMUM_SHADOWRUN_POWERS];
	long known_power_count;

	/* no spell is cast until this is down (0x440); each power's own wait
	(0xAEC) */
	long cast_cooldown_ticks;
	long power_cooldown_ticks[MAXIMUM_SHADOWRUN_POWERS];
	/* a spell that could not be cast (0xB34) */
	boolean spell_failed[NUMBER_OF_SHADOWRUN_SPELLS];
	byte pad0[3];

	/* port: the tech the left bumper uses (_shadowrun_tech_none for none), the ticks its look
	lasts, and its output: the number of the navpoint it marks it against
	(-1 for none), the units it marked and how many (shadowrun_tech.c) */
	long tech;
	long tech_ticks;
	/* the cable's tech: the unit has it (0x1084's), and a cable holds it
	(0x10EC: another unit's bolt attached to it) (shadowrun_cable.c) */
	boolean cable_tech;
	boolean cable_held;
	byte pad_cable[2];
	long vision_nav_index;
	long vision_targets[MAXIMUM_SHADOWRUN_VISION_TARGETS];
	short vision_target_count;
	byte pad_tech[2];

	/* mana (0xAD8), and how long since it was spent (0xADC); what tech and
	magic hold back of the capacity (0xAD0, 0xAE0) */
	real mana;
	long mana_regeneration_ticks;
	long tech_capacity_reserved;
	long magic_capacity_reserved;

	/* teleport: where it moves (0x444, 0x450: 0 for the definition's
	distance) or the point it moves to (0x45C, zero for none) */
	real_vector3d teleport_direction;
	real teleport_distance;
	real_point3d teleport_target;
	/* casting (0x454), moved this tick (0x455), gone from where it was
	(0x456) and the ticks until it arrives (0x458) */
	boolean teleporting;
	boolean teleported;
	boolean teleport_hidden;
	byte pad1;
	long teleport_delay_ticks;
	/* where it was when it went (0x468, 0x474) */
	real_point3d teleport_origin;
	real_vector3d teleport_origin_forward;
	/* an actor's facing when it arrives (0x480) */
	real_vector3d teleport_actor_forward;

	/* smoke is on (0x780) */
	boolean smoke_active;
	byte pad3[3];

	/* resurrection: its ball is growing (0x4A8), the ball (0x4AC) and the
	actors it raised (0x664) */
	boolean resurrecting;
	byte pad4[3];
	long resurrection_ball_index;
	long resurrected_actor_indices[MAXIMUM_SHADOWRUN_RESURRECTED_ACTORS];

	/* a summon's (0x5D9: Resurrection does not raise its body); one raised by
	Resurrection (0x5DA), which is to die (0x5DD: a newer one took its
	place, or its master died); its master, the player (0x644) and the unit
	(0x648) */
	boolean summoned;
	boolean undead;
	boolean doomed;
	/* a summon whose caster died (0x5DF): it dies */
	boolean summon_dismissed;
	long master_player_index;
	long master_unit_index;

	/* summon elemental: the actors summoned (0x650), when (0x5D4); a summon's
	ticks left to live (0x5E4) and the name it is given, of the definition's
	(0x64C) */
	long summoned_actor_indices[MAXIMUM_SHADOWRUN_SUMMONS];
	long summon_time;
	long summon_lifetime_ticks;
	long summon_name_index;

	/* tree of life: the unit's last trees (0x574), their scale grown
	(0x5AC) and their ticks left to grow (0x5BC), and its older ones
	(0x584) */
	long tree_indices[MAXIMUM_SHADOWRUN_TREES];
	real tree_scales[MAXIMUM_SHADOWRUN_TREES];
	long tree_growth_ticks[MAXIMUM_SHADOWRUN_TREES];
	long tree_count;
	long old_tree_indices[MAXIMUM_SHADOWRUN_OLD_TREES];
	long old_tree_count;

	/* smoke: when it last ended (0x48C), whether it hides the unit (0x5E1),
	the shields it had (0xAE8, 0xAE4); the looping effect of smoke or spirit
	animal (0x748) */
	long smoke_end_time;
	boolean smoke_hidden;
	byte pad7[3];
	real smoke_shield_vitality;
	real smoke_maximum_shield_vitality;
	long spell_effect_index;

	/* spirit animal: on (0x5D8), and how far it has grown (0x570) */
	boolean spirit_animal_active;
	byte pad8[3];
	long spirit_animal_growth_ticks;

	/* strangle's thorns growing (0x56C) */
	struct shadowrun_growing_thorn growing_thorns[MAXIMUM_SHADOWRUN_GROWING_THORNS];
	short growing_thorn_count;
	short pad6;

	/* the biped's: how long it cannot be strangled (0x10E0), whether it is
	being strangled (0x10E4) and the strangling's looping sound (0x1060) */
	long strangle_immune_ticks;
	boolean strangled;
	byte pad2[3];
	long strangle_looping_sound_index;
	/* (0x1068) more than 0 in the air: Shadowrun's tech sets it; until it
	is played, it stays 0 */
	real biped_airborne_tech;

	/* rage (a race that rages: the troll's): how far it rages, 0 to 1
	(0x7A4), and what that makes of the damage it takes (0x7A8), its
	movement (0x7AC) and its turning (0x7B0); the damage it took each of the
	last fifteen ticks (0x7BC), this tick's (0x7B4), and the ticks it waits
	before its rage goes down (0x7B8) */
	real rage;
	real rage_damage_scale;
	real rage_movement_scale;
	real rage_turning_scale;
	real rage_damage[SHADOWRUN_RAGE_DAMAGE_TICKS];
	char rage_damage_tick;
	byte pad9[3];
	long rage_wait_ticks;
	/* regeneration: the ticks before its body heals again (0x79C), and
	those it waits after it was hurt (0x7A0) */
	long regeneration_ticks;
	long regeneration_delay_ticks;
	/* an actor's change colours, as its variant gave them (0x614) */
	real_rgb_color actor_change_colors[NUMBER_OF_OBJECT_CHANGE_COLORS];
};

/* a player's (in its datum) */
struct shadowrun_player
{
	/* given a spell by the scripts (0x228), and which (spell + 1: 0x22C) */
	boolean magic_assigned;
	byte pad0[3];
	long assigned_spell;
	/* the magic and tech it has bought (0x134) */
	long known_powers[MAXIMUM_SHADOWRUN_POWERS];
	/* mana capacity it has bought (0x21A) */
	byte mana_capacity_bonus;
	byte pad1[3];
	/* its race, which its units take (0x1F8) */
	long race_index;
	/* its money (0x1D0), given it as it first spawns */
	long money;
	boolean given_money;
	byte pad2[3];
	/* port: the techs it has bought, in the order it bought them, and how
	many (its units take the one it has chosen: shadowrun_tech.c) */
	long known_techs[MAXIMUM_SHADOWRUN_PLAYER_TECHS];
	short known_tech_count;
	/* the one it has chosen of them (its units take that) */
	short chosen_tech;
	byte pad3[2];
};

/* ---------- prototypes/SHADOWRUN_UNITS.C */

/* a unit's Shadowrun state, or NULL (not a unit of a Shadowrun map) */
struct shadowrun_unit *shadowrun_unit_get(
	long unit_index);
/* a player's, or NULL */
struct shadowrun_player *shadowrun_player_get(
	long player_index);

/* the unit's definitions, or NULL: a biped's srud, which its bipd names,
and what that names */
struct shadowrun_unit_definition *shadowrun_unit_definition_get(
	long unit_index);
/* port: the srud a map sells by for a player whose unit names none (recomp.map:
its player biped names no srud), or NULL (the map has none) */
struct shadowrun_unit_definition *shadowrun_unit_definition_for_shopping(
	void);
struct shadowrun_race *shadowrun_unit_race_get(
	long unit_index);
struct shadowrun_magic1_definition *shadowrun_magic1_get(
	long unit_index);
struct shadowrun_magic2_definition *shadowrun_magic2_get(
	long unit_index);
struct shadowrun_magic3_definition *shadowrun_magic3_get(
	long unit_index);
/* how much a heal heals the unit (its rage's damage scale) */
real shadowrun_unit_heal_scale(
	long unit_index);
/* the srwd of the unit's weapon (else its srud's), or NULL */
struct shadowrun_weapon_data *shadowrun_unit_weapon_data_get(
	long unit_index);
/* the unit's race's head seen or hidden */
void shadowrun_unit_show_head(
	long unit_index,
	boolean show);
struct shadowrun_buy_menu_definition *shadowrun_buy_menu_get(
	long unit_index);

/* a spell's level for the unit: the mana it costs, what it costs a second,
what it holds back of the capacity, its wait after cast (all the units' and
only its own); NULL arguments are skipped. An actor's spells cost nothing */
void shadowrun_spell_get(
	long unit_index,
	short spell,
	long *mana_cost,
	real *mana_cost_per_second,
	long *capacity_reserved,
	long *cast_cooldown_ticks,
	long *spell_cooldown_ticks);

/* mana: the most a unit has (its race's and its player's bought), what it
regains a second and how long it waits */
real shadowrun_unit_mana_capacity(
	long unit_index);
real shadowrun_unit_mana_regeneration(
	long unit_index);
real shadowrun_unit_mana_regeneration_delay(
	long unit_index);
/* whether it has the mana, and spends it (all it has if it has not, and
take_all), keeping its regeneration's wait if keep_waiting */
boolean shadowrun_unit_has_mana(
	long unit_index,
	real mana);
boolean shadowrun_unit_spend_mana(
	long unit_index,
	real mana,
	boolean take_all,
	boolean keep_waiting);
/* whether magic may hold back so much of the capacity more, and holds it */
boolean shadowrun_unit_can_reserve_capacity(
	long unit_index,
	long capacity);
boolean shadowrun_unit_reserve_capacity(
	long unit_index,
	long capacity);
void shadowrun_unit_release_capacity(
	long unit_index,
	long capacity);

/* the unit takes a race of its srud (its first if it has not that one) */
void shadowrun_unit_set_race(
	long unit_index,
	long race_index);
/* port: the spells a player has (of the powers it knows), and its
player_change_tech's techs */
short shadowrun_player_known_spell_count(
	struct shadowrun_player *shadowrun_player);
/* port: the unit forgets the power, its player too (the buy menu's removal) */
boolean shadowrun_unit_forget_power(
	long unit_index,
	short power);

/* the unit learns the power (a spell, or tech): FALSE if it knew it, or the
player already has its two (shadowrun_units.c) */
boolean shadowrun_unit_learn_power(
	long unit_index,
	short power);

/* the unit's player's local player index, or NONE */
short shadowrun_unit_local_player_index(
	long unit_index);
/* the buy menu's sound for a spell that fails, on the unit's player's
machine */
void shadowrun_unit_cast_failed(
	long unit_index,
	short spell);
/* ... the sound alone (a spell dismissed) */
void shadowrun_unit_cast_failed_sound(
	long unit_index);

/* a unit's own damage (its feedback) with the damage, and the effect on it */
void shadowrun_unit_cast_feedback(
	long unit_index,
	long damage_definition_index,
	long effect_definition_index);

/* ---------- prototypes/SHADOWRUN_BUY_MENU.C */

/* the local players' menus closed, at each map */
void shadowrun_buy_menu_initialize_for_new_map(
	void);
/* a player's money, as it first spawns */
void shadowrun_buy_menu_player_spawned(
	long player_index);
/* the local player's open menu over its view */
void shadowrun_buy_menu_draw(
	short local_player_index);

/* ---------- prototypes/SHADOWRUN_RACES.C */

/* each tick of a unit, after its mana's: its body heals, and its rage */
void shadowrun_unit_regeneration_update(
	long unit_index);
void shadowrun_unit_rage_update(
	long unit_index);

/* ---------- prototypes/SHADOWRUN_MAGIC.C */

/* the spells' state of the world, in the game state (shadowrun_initialize),
and at each map */
void shadowrun_magic_initialize(
	void);
void shadowrun_magic_initialize_for_new_map(
	void);
/* each tick of the game, after the objects' (game_tick): the trees heal */
void shadowrun_magic_update(
	void);
/* a projectile detonated where it is (projectiles.c): a tree of life
grenade's tree grows */
void shadowrun_projectile_detonated(
	long projectile_index,
	real_point3d const *position);
/* the map's projectiles' Shadowrun flags, kept from their definitions'
(where this build has others) as the map is loaded */
void shadowrun_projectiles_loaded(
	void);
/* whether a smoke hides the unit: nothing damages it */
boolean shadowrun_unit_intangible(
	long unit_index);
/* whether the object is one of strangle's thorns */
boolean shadowrun_object_is_thorn(
	long object_index);

/* a definition's volume for ai_handle_unit_effect, in its range */
short shadowrun_ai_volume(
	short volume);

/* whether the unit can cast the spell now, and casts it (the grenade
control, the scripts) */
boolean shadowrun_spell_castable(
	long unit_index,
	short spell);
void shadowrun_spell_cast(
	long unit_index,
	short spell);
/* each tick of a unit of a Shadowrun map: its spells that last */
void shadowrun_spells_update(
	long unit_index);
/* each tick of a unit Resurrection raised, before its own */
void shadowrun_undead_update(
	long unit_index);
/* each tick of a summon, before its own */
void shadowrun_summoned_update(
	long unit_index);
/* a unit dead for good: what its spells leave */
void shadowrun_spells_unit_died(
	long unit_index);
/* ... and a spirit animal's body */
void shadowrun_spirit_animal_died(
	long unit_index);

#endif
