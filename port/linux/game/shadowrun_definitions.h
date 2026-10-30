/*
SHADOWRUN_DEFINITIONS.H

The Shadowrun prototype's own tags, as its executable (cachesym.xbe, build
01.12.07.0132) reads them: the unit's (srud), which a biped names, and its
magic's (m1ed, m2ed, m3ed). Only what the game reads is named; the rest is
padding of the right size. The offsets are the executable's, and each
structure is checked against them below. The tag validator checks these
groups as it checks this build's (tag_schema_shadowrun.c).

Each spell has a block of five levels, chosen by the unit's race (a unit's
srud has a race block of the same length): what it costs, what it holds back
while it lasts and how long it waits before it can be cast again.
*/

#ifndef __SHADOWRUN_DEFINITIONS_H
#define __SHADOWRUN_DEFINITIONS_H
#pragma once

/* ---------- headers */

#include "tag_files/tag_groups.h"

#include <stddef.h>

/* ---------- constants */

/* port: the srud the prototype's multiplayer players bought by, which a map
whose player biped names none (recomp.map) sells by (shadowrun_units.c,
shadowrun_unit_definition_for_shopping) */
#define SHADOWRUN_GENERIC_UNIT_DEFINITION "shadowrunstuff\\generic_unit"

/* port: whether a map's group word is `group`'s. A map stores a group as the
bytes of its name, which this build reads as a word of the four, one way or
the other ('srud' is 0x64757273 from its first byte or 0x73727564 from its
last); a map of another build need not be this build's way (recomp's holds
'scnr' as 0x73636e72). Either is the group (shadowrun_units.c,
shadowrun_cache_tag_groups_normalize) */
#define SHADOWRUN_GROUP_TAG_IS(stored, group) \
	((unsigned long)(stored) == (unsigned long)(group) || \
		(unsigned long)(stored) == ((((unsigned long)(group) & 0xFF) << 24) | \
			(((unsigned long)(group) & 0xFF00) << 8) | \
			(((unsigned long)(group) >> 8) & 0xFF00) | \
			(((unsigned long)(group) >> 24) & 0xFF)))

enum
{
	SHADOWRUN_UNIT_DEFINITION_TAG = 'srud',
	SHADOWRUN_MAGIC1_DEFINITION_TAG = 'm1ed',
	SHADOWRUN_MAGIC2_DEFINITION_TAG = 'm2ed',
	SHADOWRUN_MAGIC3_DEFINITION_TAG = 'm3ed',
	SHADOWRUN_TECH2_DEFINITION_TAG = 't2ed',
	SHADOWRUN_TECH3_DEFINITION_TAG = 't3ed',
	SHADOWRUN_BUY_MENU_DEFINITION_TAG = 'buym',
	SHADOWRUN_WEAPON_DATA_TAG = 'srwd',

	/* the races a unit definition has (and the levels of each spell) */
	MAXIMUM_SHADOWRUN_RACES = 5,

	/* (a marker past any index, so that the group tags stay longs) */
	SHADOWRUN_GROUP_TAG_LONG = 0x7FFFFFFF,

	/* the spells, as the executable numbers them (its names, at 0x4865e8) */
	_shadowrun_spell_teleport = 0,
	_shadowrun_spell_gust,
	_shadowrun_spell_tree_of_life,
	_shadowrun_spell_resurrection,
	_shadowrun_spell_strangle,
	_shadowrun_spell_summon_elemental,
	_shadowrun_spell_spirit_animal,
	_shadowrun_spell_pillar_of_salt,
	_shadowrun_spell_smoke,
	NUMBER_OF_SHADOWRUN_SPELLS,

	/* the magic and tech a unit may have, each with its own wait */
	MAXIMUM_SHADOWRUN_POWERS = 18,

	/* the weapons a unit's buy menu sells (its srud's, in this order), and
	its prices */
	_shadowrun_shop_weapon_assault_rifle = 0,
	_shadowrun_shop_weapon_sniper_rifle,
	_shadowrun_shop_weapon_minigun,
	_shadowrun_shop_weapon_rocket_launcher,
	_shadowrun_shop_weapon_flechette_pistol,
	_shadowrun_shop_weapon_shotgun,
	_shadowrun_shop_weapon_razorclaws,
	_shadowrun_shop_weapon_katana,
	_shadowrun_shop_weapon_rifle,
	NUMBER_OF_SHADOWRUN_SHOP_WEAPONS,
	NUMBER_OF_SHADOWRUN_PRICES = 48,

	/* a race's flags (those the game reads): it rages (the troll), and its
	rage keeps off only the damage of those before it (within two radians
	of where it looks; the executable truncates the angle) */
	_shadowrun_race_regenerates_bit = 0,
	_shadowrun_race_rages_bit = 2,
	_shadowrun_race_rage_guards_front_bit = 5,
};

/* ---------- structures */

/* a spell's level: what one race pays for it (0x200 bytes) */
struct shadowrun_spell_level
{
	byte unused0[0x100];
	/* the mana it takes */
	long mana_cost;
	/* the mana it takes each second while it lasts (smoke) */
	real mana_cost_per_second;
	/* the capacity it holds back while it lasts (resurrection's zombies) */
	long capacity_reserved;
	/* how long it is before this spell can be cast again (ticks) */
	long cooldown_ticks;
	byte unused1[0xF0];
};

/* m1ed: teleport, gust and tree of life (0x700 bytes) */
struct shadowrun_magic1_definition
{
	byte unused0[0x80];

	/* teleport */
	long teleport_cooldown_ticks;
	/* the unit moves when the cooldown is down to this */
	long teleport_move_ticks;
	long unused1;
	real teleport_distance;
	struct tag_reference teleport_windup_effect;
	struct tag_reference teleport_windup_damage;
	struct tag_reference teleport_before_effect;
	struct tag_reference teleport_before_damage;
	struct tag_reference teleport_after_effect;
	struct tag_reference teleport_after_damage;
	struct tag_block teleport_levels;
	short teleport_ai_unit_effect;
	short unused2;
	byte unused3[0x80];

	/* gust */
	struct tag_reference gust_cast_damage;
	struct tag_reference gust_cast_effect;
	struct tag_reference gust_projectile;
	struct tag_reference gust_effect;
	long gust_cooldown_ticks;
	long unused4;
	/* how far it reaches, and how far from its line it pushes */
	real gust_range;
	real gust_maximum_distance;
	/* within this of its line it pushes as hard as it can */
	real gust_full_force_distance;
	real gust_near_force;
	real gust_far_force;
	real gust_vehicle_force_scale;
	struct tag_block gust_levels;
	struct tag_reference gust_damage;
	/* how hard the AI takes the push */
	real gust_ai_damage_fraction;
	short gust_ai_damage_category;
	short gust_ai_unit_effect;
	/* how long a unit it pushes cannot be strangled (ticks) */
	long gust_strangle_break_ticks;
	byte unused5[0x78];

	/* tree of life: a tree grows where the caster's grenade lands (an
	actor's, where it aims) and heals those near it, withering as it does */
	struct tag_reference tree_of_life_scenery;
	long tree_of_life_cooldown_ticks;
	long tree_of_life_growth_ticks;
	long unused6;
	/* how far an actor's aim reaches */
	real tree_of_life_range;
	real unused7;
	/* a tree's scale grown, at random between these */
	real tree_of_life_minimum_scale;
	real tree_of_life_maximum_scale;
	/* the most trees standing */
	long tree_of_life_maximum_count;
	/* how far a tree heals, and how much of a body's vitality a tick */
	real tree_of_life_heal_radius;
	real tree_of_life_heal_rate;
	struct tag_reference tree_projectile;
	/* what one healed takes (each second) and the effect on it */
	struct tag_reference tree_of_life_heal_damage;
	struct tag_reference tree_of_life_heal_effect;
	struct tag_reference tree_of_life_cast_damage;
	struct tag_reference tree_of_life_cast_effect;
	long unused8;
	struct tag_block tree_of_life_levels;
	short tree_of_life_ai_unit_effect;
	short unused9;
	/* the vitality a tree loses each tick, the caster's last four and the
	older */
	real tree_of_life_decay;
	real tree_of_life_old_decay;
	byte unused10[0x3DC];
};

/* m2ed: resurrection, strangle and summon elemental (0x700 bytes) */
struct shadowrun_magic2_definition
{
	byte unused0[0x80];

	/* resurrection: a ball grows before the caster, and the bodies it
	reaches in front of the caster rise, as actors of the encounter named
	here on the caster's side */
	struct tag_reference resurrection_finish_damage;
	struct tag_reference resurrection_finish_effect;
	/* how long the ball grows (ticks), and its radius grown */
	long resurrection_cooldown_ticks;
	real resurrection_radius;
	char resurrection_encounter[32];
	byte unused1[0x8];
	struct tag_reference resurrection_effect;
	struct tag_reference resurrection_damage;
	real unused2;
	struct tag_reference resurrection_projectile;
	long unused10;
	struct tag_block resurrection_levels;
	short resurrection_ai_unit_effect;
	short unused3;
	/* the ball (scenery) */
	struct tag_reference resurrection_ball;
	/* what a raised body takes each tick once it is to die (a newer one
	took its place), and a raised player's corpse in a team game */
	struct tag_reference resurrection_doomed_damage;
	struct tag_reference resurrection_corpse_damage;
	/* how long an actor's ball grows (ticks) */
	long resurrection_actor_cooldown_ticks;
	/* the capacity the raised hold back (single player) */
	long resurrection_capacity_reserved;
	/* what a raised body takes each tick once it is dead again */
	struct tag_reference resurrection_dead_damage;
	byte unused4[0x20];

	/* strangle: thorns grow where the caster aims, about a line across its
	aim, on the floor, the ceiling and the walls */
	struct tag_reference strangle_scenery;
	long strangle_cooldown_ticks;
	long strangle_growth_ticks;
	long unused13;
	real strangle_range;
	/* how high above where it aims the line is, and the thorns' spacing on
	it and scatter about it (world units) */
	real strangle_height;
	real unused14;
	real strangle_spacing;
	real strangle_scatter;
	/* how far a thorn may lean (radians), about either axis */
	real strangle_lean_i;
	real strangle_lean_j;
	/* a thorn's scale grown, of a random scale between these */
	real strangle_scale;
	real strangle_minimum_scale;
	real strangle_maximum_scale;
	/* how far from its point a thorn looks for a surface */
	real strangle_reach;
	/* the thorns a cast makes, and the most kept */
	long strangle_count;
	long strangle_maximum_count;
	byte unused15[0x8];
	struct tag_reference strangle_damage;
	struct tag_reference strangle_projectile;
	struct tag_reference strangle_cast_damage;
	struct tag_reference strangle_cast_effect;
	struct tag_block strangle_levels;
	short strangle_ai_unit_effect;
	short unused6;
	byte unused7[0x58];

	/* summon elemental: an actor of the encounter named here, on the
	caster's side */
	long summon_cooldown_ticks;
	real summon_range;
	char summon_encounter[32];
	byte unused8[0x8];
	struct tag_reference summon_birth_effect;
	struct tag_reference summon_birth_damage;
	real summon_lifetime;
	struct tag_reference summon_projectile;
	struct tag_reference summon_cast_damage;
	struct tag_reference summon_cast_effect;
	long unused11;
	struct tag_block summon_levels;
	/* the names a summon is given (one at random) */
	struct tag_reference summon_names;
	short summon_ai_unit_effect;
	short unused12;
	/* what a summon takes each tick once its time is up, or its caster is
	dead */
	struct tag_reference summon_dying_damage;
	byte unused9[0x48];

	/* firewall (strangle's other form, which the fire flag of a projectile
	brings about where it detonates): fires burn on the surfaces about where
	the caster aims, for the definition's time, hurting those of the other
	sides in their reach */
	struct tag_reference firewall_scenery;
	long firewall_cooldown_ticks;
	long firewall_lifetime_ticks;
	real firewall_range;
	real firewall_height;
	real unused16;
	real firewall_spacing;
	real firewall_scatter;
	real unused17[2];
	real firewall_reach;
	long firewall_count;
	/* how far a fire hurts, and over how many ticks its damage is dealt */
	real firewall_damage_radius;
	long firewall_damage_ticks;
	struct tag_reference firewall_damage;
	struct tag_reference firewall_projectile;
	struct tag_reference firewall_cast_damage;
	struct tag_reference firewall_cast_effect;
	struct tag_reference firewall_effect;
	byte unused18[0x2EC];
};

/* m3ed: spirit animal and smoke (0x700 bytes) */
struct shadowrun_magic3_definition
{
	byte unused0[0x80];

	/* spirit animal: the caster takes its animal's form, growing, while its
	mana lasts; dead, it becomes the biped here */
	long spirit_animal_cooldown_ticks;
	struct tag_reference spirit_animal_damage;
	struct tag_reference spirit_animal_effect;
	real unused1;
	struct tag_reference spirit_animal_cast_damage;
	struct tag_reference spirit_animal_cast_effect;
	long unused2;
	struct tag_block spirit_animal_levels;
	struct tag_reference spirit_animal_biped;
	struct tag_reference spirit_animal_cast_sound;
	struct tag_reference spirit_animal_sound;
	char spirit_animal_encounter[32];
	byte unused3[0x158];

	/* smoke: the caster is hidden and camouflaged, while its mana lasts */
	long smoke_cooldown_ticks;
	byte unused4[0x8];
	/* how long the caster takes to be camouflaged, and to be seen again
	(seconds), and how camouflaged it is (its race's head hidden too) */
	real smoke_camouflage_time;
	real smoke_uncamouflage_time;
	real smoke_camouflage;
	struct tag_reference smoke_cast_damage;
	struct tag_reference smoke_effect;
	struct tag_reference smoke_cast_effect;
	struct tag_block smoke_levels;
	short smoke_ai_unit_effect;
	short unused5;
	byte unused6[0x428];
};

/* t2ed: the cable (0x1200 bytes): grapple, glider, and the heal of the
undead. Its fields are the executable's offsets (units.c FUN_0025e860 gives
a unit the srud's tech2; bipeds.c FUN_002541d0 holds its cable, which reads
0x1ac, 0x1b0, 0x1b4, 0x1b8, 0x1bc, 0x380 and 0x3a4/0x3ac), and their names
follow the explorer's dumps of the map (custom_tag_dumps/t2ed.txt).

Its tag references are the Xbox's 0x10 bytes, as this build's are on the
64-bit targets (Windows, web); on the 32-bit ones (Linux, Android) they are
0xC, each 4 bytes short. The padding before a field is then made longer by
what the references before it took less, and that before a tag block's
fields by that block's too (the tag cache's pointer, which is 8 bytes on
either), so every field keeps the executable's offset on both */
#define TECH2_REFERENCE_PADDING ((long)(0x10 - sizeof(struct tag_reference)))

struct shadowrun_tech2_definition
{
	byte unused0[0x18C - 2 * TECH2_REFERENCE_PADDING];
	/* the bolt it fires, the cable scenery it draws between the unit and the
	bolt, and how fast it fires, hits, is attached, and how long its reel
	takes (ticks), with how far away the cable snaps */
	struct tag_reference projectile;
	struct tag_reference scenery;
	real fire_distance;
	real hit_distance;
	real attach_distance;
	real snap_distance;
	long reel_ticks;
	/* the ten effects of the cable's use, as the executable plays them
	(bipeds.c FUN_002541d0, FUN_0025b7d0, FUN_0027c350) */
	struct tag_reference fire_effect;
	struct tag_reference hit_success_effect;
	struct tag_reference hit_success_effect_2;
	struct tag_reference hit_bounce_effect;
	struct tag_reference hit_bounce_effect_2;
	struct tag_reference travel_effect;
	struct tag_reference player_travel_effect;
	struct tag_reference player_attach_effect;
	struct tag_reference player_release_effect;
	struct tag_reference cable_release_effect;
	struct tag_reference cable_snap_effect;
	byte unused1[0x14 + TECH2_REFERENCE_PADDING];
	/* the glider's scale (bipeds.c FUN_00251c00's 0x1a4), and its scenery;
	its races' blocks follow, and its turn (0x2a8) after them */
	real glider_scale;
	struct tag_reference glider_scenery;
	struct tag_block glider_races;
	/* (the glider's turn, 0x2A8, as the executable's FUN_002541D0 reads it,
is not named: it is a tag block's, which this build's differ from the
Xbox's in size) */
	byte unused2[0xDC - TECH2_REFERENCE_PADDING];
	/* the heal of the undead (bipeds.c FUN_002541d0, the cable's state 5):
	how many points, and the effects that heal them */
	long undead_heal_points;
	struct tag_reference undead_heal_effect;
	struct tag_reference undead_heal_effect_2;
	/* how fast it heals, and the scale of its other one */
	real undead_heal_scale;
	byte unused3[4];
	real undead_heal_scale_2;
	byte unused4[0x1D8 + 4 * TECH2_REFERENCE_PADDING];
	/* the name it is given (the map's "zombie"), how many ticks it lasts and
	how many points it heals */
	real undead_name_scale;
	char undead_name[0x20];
	/* (the executable reads the name's short at 0x594, which this build's 0x20
	bytes reach) */
	long undead_ticks;
	long undead_points;
	struct tag_reference undead_effect;
	struct tag_reference undead_damage;
	struct tag_reference undead_projectile;
	struct tag_reference undead_damage_2;
	struct tag_reference undead_effect_2;
	struct tag_block undead_races;
	byte unused5[0xBF0 - 9 * TECH2_REFERENCE_PADDING];
};

/* t3ed: anti-magic (0x1200 bytes). Its fields are the executable's offsets
(units.c, FUN_0026EA00 reads 0x38C and 0x39C; FUN_0026FCA0's zone holds
these), and their names follow the explorer's dumps of the map */
struct shadowrun_tech3_definition
{
	byte unused0[0x380];
	/* the scenery that stands for the zone, and the ball it fills with */
	struct tag_reference antimagic_scenery;
	struct tag_reference antimagic_ball_scenery;
	/* what it does to those inside, and the effect */
	struct tag_reference antimagic_damage;
	struct tag_reference antimagic_effect;
	/* how many it is (0x3C0), how long it stands in ticks (0x3C4, times each
	race's level), its starting radius, how it drifts and grows, and how far
	it reaches */
	long antimagic_count;
	long antimagic_duration_ticks;
	real antimagic_radius;
	real antimagic_drift;
	real antimagic_drift_scale;
	real antimagic_grow;
	real antimagic_grow_scale;
	real antimagic_grow_scale2;
	real antimagic_reach;
	struct tag_reference antimagic_spirit_damage;
	/* the levels (a spell's: the magic's, in the same place) */
	struct tag_block antimagic_levels;
	real antimagic_level_scale;
	/* what it drains (essence; 0x404), its scale (0x414) */
	struct tag_reference antimagic_drain_damage;
	real antimagic_drain_scale;
	/* the explosion it makes (0x418) */
	struct tag_reference antimagic_explosion_effect;
	/* what those inside it lose (their anti-magic: a grenade's, 0x428 and
	0x438, with its scale at 0x448 and its ticks at 0x44C) */
	struct tag_reference antimagic_lost_damage;
	struct tag_reference antimagic_lost_effect;
	real antimagic_lost_scale;
	long antimagic_lost_ticks;
	/* the electricity it plays (0x450), and its scale (0x460) */
	struct tag_reference antimagic_electricity;
	real antimagic_electricity_scale;
	byte unused4[0x1200 - 0x464];
};

/* srud: a Shadowrun unit (0x600 bytes) */
struct shadowrun_unit_definition
{
	byte unused0[0x100];
	struct tag_reference magic1;
	struct tag_reference magic2;
	struct tag_reference magic3;
	struct tag_reference tech1;
	struct tag_reference tech2;
	struct tag_reference tech3;
	struct tag_block races;
	byte unused1[0xB4];
	struct tag_reference default_weapon_data;
	byte unused2[0x1F0];
	/* the buy menu's: its bitmap (which nothing draws), the weapons it
	sells, the menu, and its prices (SHADOWRUN_PRICE_*: of the menu's
	items, and a magazine of each weapon) */
	struct tag_reference buy_menu_bitmap;
	struct tag_reference shop_weapons[NUMBER_OF_SHADOWRUN_SHOP_WEAPONS];
	byte unused4[0x30];
	struct tag_reference buy_menu;
	long prices[NUMBER_OF_SHADOWRUN_PRICES];
	byte unused3[0x40];
};

/* a race of a Shadowrun unit (0xA00 bytes) */
struct shadowrun_race
{
	byte unused0[0xFC];
	unsigned long flags;
	/* the unit's scale, and the scenery it wears on its head */
	real scale;
	struct tag_reference head;
	byte unused3[0x6];
	/* the sound the AI hears as it heals itself (an AI unit effect) */
	short regeneration_ai_unit_effect;
	/* the damage it takes (of all but melee) */
	real damage_scale;
	/* regeneration (its flags' regeneration): its body heals the points
	each second, waiting the ticks after it was last hurt, while it is hurt
	and less than the threshold; each costs it the mana, and its own damage
	(its feedback) and the effect on it */
	real regeneration_points;
	real regeneration_delay_ticks;
	/* its body's vitality (a player's: an actor's is its actor's) */
	real maximum_body_vitality;
	struct tag_reference regeneration_damage;
	struct tag_reference regeneration_effect;
	real regeneration_mana_cost;
	byte unused4[0xC];
	/* how hard a gust pushes it, in the air and on the ground */
	real gust_airborne_scale;
	real gust_scale;
	/* the melee damage it takes, and deals */
	real melee_damage_scale;
	real melee_scale;
	real regeneration_threshold;
	byte unused6[0x90];
	/* rage (its flags' rage): the damage it takes (each damage's
	definition's active camouflage damage, as the executable reads it) over
	the last half second (15 ticks), more than the threshold, makes it rage,
	and it waits the ticks before its rage goes down by the decay each tick.
	As it rages it takes the damage, moves and turns as the raging scales,
	as the calm ones calm, and grows to the raging scale at the speed (each
	second). Its rage costs it mana (the cost a point); past the material
	threshold the damage it takes looks as it does on a jackal's shield */
	real rage_threshold;
	real raging_damage_scale;
	real calm_damage_scale;
	real raging_movement_scale;
	real calm_movement_scale;
	real rage_decay;
	real rage_wait_ticks;
	real raging_turning_scale;
	real calm_turning_scale;
	real rage_mana_cost;
	real raging_scale;
	real rage_scale_speed;
	real rage_material_threshold;
	byte unused1[0x1CC];
	/* the most mana it has, which magic and tech held back take from */
	long mana_capacity;
	/* mana it regains each second, after it has waited this long (seconds)
	since it last spent any */
	real mana_regeneration_per_second;
	real mana_regeneration_delay;
	byte unused2[0x5F4];
};

/* a race's speed with a weapon (0x600 bytes) */
struct shadowrun_weapon_data_race
{
	byte unused0[0x100];
	/* its movement's speed, and how fast it gets there (as a player: 0 or
	less leaves it be) */
	real movement_scale;
	real acceleration_scale;
	byte unused1[0x4F8];
};

/* srwd: Shadowrun's weapon data, which a weapon names (and a unit's srud, for
when it has none of its own): what each race's movement is with it (0x600
bytes, its races' blocks after it) */
struct shadowrun_weapon_data
{
	byte unused0[0x500];
	struct tag_block races;
	/* the data a smartlinked unit uses instead (Shadowrun's tech: not
	played, so it is not chosen) */
	struct tag_reference smartlinked;
	byte unused1[0xE4];
};

/* buym: the buy menu's sounds, and the sound of a spell that fails (0x7D0
bytes) */
struct shadowrun_buy_menu_definition
{
	struct tag_reference open_sound;
	struct tag_reference close_sound;
	struct tag_reference page_sound;
	struct tag_reference bought_sound;
	struct tag_reference learned_sound;
	struct tag_reference tech_sound;
	struct tag_reference cannot_afford_sound;
	struct tag_reference cursor_sound;
	struct tag_reference cannot_buy_sound;
	struct tag_reference tech_owned_sound;
	struct tag_reference known_sound;
	struct tag_reference ammunition_sound;
	struct tag_reference ammunition_failed_sound;
	byte unused0[0x620];
	struct tag_reference cast_failed_sound;
	byte unused1[0xD0];
};

/* ---------- the executable's offsets */

typedef char shadowrun_spell_level_size_assert[sizeof(struct shadowrun_spell_level) == 0x200 ? 1 : -1];
typedef char shadowrun_spell_level_cost_assert[offsetof(struct shadowrun_spell_level, mana_cost) == 0x100 ? 1 : -1];
typedef char shadowrun_spell_level_cooldown_assert[offsetof(struct shadowrun_spell_level, cooldown_ticks) == 0x10C ? 1 : -1];

typedef char shadowrun_magic1_size_assert[sizeof(struct shadowrun_magic1_definition) == 0x700 ? 1 : -1];
typedef char shadowrun_magic1_teleport_damage_assert[
	offsetof(struct shadowrun_magic1_definition, teleport_after_damage.index) == 0xEC ? 1 : -1];
typedef char shadowrun_magic1_teleport_levels_assert[
	offsetof(struct shadowrun_magic1_definition, teleport_levels) == 0xF0 ? 1 : -1];
typedef char shadowrun_magic1_teleport_ai_assert[
	offsetof(struct shadowrun_magic1_definition, teleport_ai_unit_effect) == 0xFC ? 1 : -1];
typedef char shadowrun_magic1_gust_projectile_assert[
	offsetof(struct shadowrun_magic1_definition, gust_projectile.index) == 0x1AC ? 1 : -1];
typedef char shadowrun_magic1_gust_cooldown_assert[
	offsetof(struct shadowrun_magic1_definition, gust_cooldown_ticks) == 0x1C0 ? 1 : -1];
typedef char shadowrun_magic1_gust_levels_assert[
	offsetof(struct shadowrun_magic1_definition, gust_levels) == 0x1E0 ? 1 : -1];
typedef char shadowrun_magic1_gust_damage_assert[
	offsetof(struct shadowrun_magic1_definition, gust_damage.index) == 0x1F8 ? 1 : -1];
typedef char shadowrun_magic1_gust_strangle_assert[
	offsetof(struct shadowrun_magic1_definition, gust_strangle_break_ticks) == 0x204 ? 1 : -1];
typedef char shadowrun_magic1_tree_scenery_assert[
	offsetof(struct shadowrun_magic1_definition, tree_of_life_scenery) == 0x280 ? 1 : -1];
typedef char shadowrun_magic1_tree_cooldown_assert[
	offsetof(struct shadowrun_magic1_definition, tree_of_life_cooldown_ticks) == 0x290 ? 1 : -1];
typedef char shadowrun_magic1_tree_projectile_assert[
	offsetof(struct shadowrun_magic1_definition, tree_projectile) == 0x2B8 ? 1 : -1];
typedef char shadowrun_magic1_tree_heal_assert[
	offsetof(struct shadowrun_magic1_definition, tree_of_life_heal_rate) == 0x2B4 ? 1 : -1];
typedef char shadowrun_magic1_tree_heal_damage_assert[
	offsetof(struct shadowrun_magic1_definition, tree_of_life_heal_damage.index) == 0x2D4 ? 1 : -1];
typedef char shadowrun_magic1_tree_cast_effect_assert[
	offsetof(struct shadowrun_magic1_definition, tree_of_life_cast_effect.index) == 0x304 ? 1 : -1];
typedef char shadowrun_magic1_tree_levels_assert[
	offsetof(struct shadowrun_magic1_definition, tree_of_life_levels) == 0x30C ? 1 : -1];
typedef char shadowrun_magic1_tree_shrink_assert[
	offsetof(struct shadowrun_magic1_definition, tree_of_life_old_decay) == 0x320 ? 1 : -1];

typedef char shadowrun_magic2_size_assert[sizeof(struct shadowrun_magic2_definition) == 0x700 ? 1 : -1];
typedef char shadowrun_magic2_resurrection_finish_assert[
	offsetof(struct shadowrun_magic2_definition, resurrection_finish_effect.index) == 0x9C ? 1 : -1];
typedef char shadowrun_magic2_resurrection_encounter_assert[
	offsetof(struct shadowrun_magic2_definition, resurrection_encounter) == 0xA8 ? 1 : -1];
typedef char shadowrun_magic2_resurrection_levels_assert[
	offsetof(struct shadowrun_magic2_definition, resurrection_levels) == 0x108 ? 1 : -1];
typedef char shadowrun_magic2_resurrection_ball_assert[
	offsetof(struct shadowrun_magic2_definition, resurrection_ball.index) == 0x124 ? 1 : -1];
typedef char shadowrun_magic2_resurrection_capacity_assert[
	offsetof(struct shadowrun_magic2_definition, resurrection_capacity_reserved) == 0x14C ? 1 : -1];
typedef char shadowrun_magic2_resurrection_dead_assert[
	offsetof(struct shadowrun_magic2_definition, resurrection_dead_damage.index) == 0x15C ? 1 : -1];
typedef char shadowrun_magic2_strangle_cooldown_assert[
	offsetof(struct shadowrun_magic2_definition, strangle_cooldown_ticks) == 0x190 ? 1 : -1];
typedef char shadowrun_magic2_strangle_range_assert[
	offsetof(struct shadowrun_magic2_definition, strangle_range) == 0x19C ? 1 : -1];
typedef char shadowrun_magic2_strangle_reach_assert[
	offsetof(struct shadowrun_magic2_definition, strangle_reach) == 0x1C4 ? 1 : -1];
typedef char shadowrun_magic2_strangle_maximum_assert[
	offsetof(struct shadowrun_magic2_definition, strangle_maximum_count) == 0x1CC ? 1 : -1];
typedef char shadowrun_magic2_strangle_cast_effect_assert[
	offsetof(struct shadowrun_magic2_definition, strangle_cast_effect.index) == 0x214 ? 1 : -1];
typedef char shadowrun_magic2_strangle_levels_assert[
	offsetof(struct shadowrun_magic2_definition, strangle_levels) == 0x218 ? 1 : -1];
typedef char shadowrun_magic2_summon_cooldown_assert[
	offsetof(struct shadowrun_magic2_definition, summon_cooldown_ticks) == 0x280 ? 1 : -1];
typedef char shadowrun_magic2_summon_encounter_assert[
	offsetof(struct shadowrun_magic2_definition, summon_encounter) == 0x288 ? 1 : -1];
typedef char shadowrun_magic2_summon_cast_effect_assert[
	offsetof(struct shadowrun_magic2_definition, summon_cast_effect.index) == 0x300 ? 1 : -1];
typedef char shadowrun_magic2_summon_names_assert[
	offsetof(struct shadowrun_magic2_definition, summon_names.index) == 0x320 ? 1 : -1];
typedef char shadowrun_magic2_summon_ai_assert[
	offsetof(struct shadowrun_magic2_definition, summon_ai_unit_effect) == 0x324 ? 1 : -1];
typedef char shadowrun_magic2_summon_dying_assert[
	offsetof(struct shadowrun_magic2_definition, summon_dying_damage.index) == 0x334 ? 1 : -1];
typedef char shadowrun_magic2_firewall_scenery_assert[
	offsetof(struct shadowrun_magic2_definition, firewall_scenery.index) == 0x38C ? 1 : -1];
typedef char shadowrun_magic2_firewall_reach_assert[
	offsetof(struct shadowrun_magic2_definition, firewall_reach) == 0x3B4 ? 1 : -1];
typedef char shadowrun_magic2_firewall_damage_assert[
	offsetof(struct shadowrun_magic2_definition, firewall_damage.index) == 0x3D0 ? 1 : -1];
typedef char shadowrun_magic2_firewall_effect_assert[
	offsetof(struct shadowrun_magic2_definition, firewall_effect.index) == 0x410 ? 1 : -1];
typedef char shadowrun_magic2_summon_levels_assert[
	offsetof(struct shadowrun_magic2_definition, summon_levels) == 0x308 ? 1 : -1];

typedef char shadowrun_magic3_size_assert[sizeof(struct shadowrun_magic3_definition) == 0x700 ? 1 : -1];
typedef char shadowrun_magic3_spirit_animal_effect_assert[
	offsetof(struct shadowrun_magic3_definition, spirit_animal_effect.index) == 0xA0 ? 1 : -1];
typedef char shadowrun_magic3_spirit_animal_levels_assert[
	offsetof(struct shadowrun_magic3_definition, spirit_animal_levels) == 0xCC ? 1 : -1];
typedef char shadowrun_magic3_spirit_animal_biped_assert[
	offsetof(struct shadowrun_magic3_definition, spirit_animal_biped.index) == 0xE4 ? 1 : -1];
typedef char shadowrun_magic3_spirit_animal_encounter_assert[
	offsetof(struct shadowrun_magic3_definition, spirit_animal_encounter) == 0x108 ? 1 : -1];
typedef char shadowrun_magic3_smoke_cooldown_assert[
	offsetof(struct shadowrun_magic3_definition, smoke_cooldown_ticks) == 0x280 ? 1 : -1];
typedef char shadowrun_magic3_smoke_camouflage_assert[
	offsetof(struct shadowrun_magic3_definition, smoke_camouflage) == 0x294 ? 1 : -1];
typedef char shadowrun_magic3_smoke_effect_assert[
	offsetof(struct shadowrun_magic3_definition, smoke_effect.index) == 0x2B4 ? 1 : -1];
typedef char shadowrun_magic3_smoke_levels_assert[
	offsetof(struct shadowrun_magic3_definition, smoke_levels) == 0x2C8 ? 1 : -1];
typedef char shadowrun_magic3_smoke_ai_assert[
	offsetof(struct shadowrun_magic3_definition, smoke_ai_unit_effect) == 0x2D4 ? 1 : -1];

/* (t2ed's are checked by size alone: the map's fields are its name's offset and
its two tag blocks' pointers, which differ between the 32-bit pointers of the
Linux and Android guests and the 64-bit ones of the Windows and web builds) */
typedef char shadowrun_tech2_size_assert[sizeof(struct shadowrun_tech2_definition) == 0x1200 ? 1 : -1];

typedef char shadowrun_tech3_size_assert[sizeof(struct shadowrun_tech3_definition) == 0x1200 ? 1 : -1];
typedef char shadowrun_tech3_scenery_assert[
	offsetof(struct shadowrun_tech3_definition, antimagic_scenery) == 0x380 ? 1 : -1];
typedef char shadowrun_tech3_count_assert[
	offsetof(struct shadowrun_tech3_definition, antimagic_count) == 0x3C0 ? 1 : -1];
typedef char shadowrun_tech3_radius_assert[
	offsetof(struct shadowrun_tech3_definition, antimagic_radius) == 0x3C8 ? 1 : -1];
typedef char shadowrun_tech3_reach_assert[
	offsetof(struct shadowrun_tech3_definition, antimagic_reach) == 0x3E0 ? 1 : -1];
typedef char shadowrun_tech3_levels_assert[
	offsetof(struct shadowrun_tech3_definition, antimagic_levels) == 0x3F4 ? 1 : -1];
typedef char shadowrun_tech3_drain_assert[
	offsetof(struct shadowrun_tech3_definition, antimagic_drain_damage) == 0x404 ? 1 : -1];
typedef char shadowrun_tech3_explosion_assert[
	offsetof(struct shadowrun_tech3_definition, antimagic_explosion_effect) == 0x418 ? 1 : -1];
typedef char shadowrun_tech3_lost_assert[
	offsetof(struct shadowrun_tech3_definition, antimagic_lost_damage) == 0x428 ? 1 : -1];
typedef char shadowrun_tech3_electricity_assert[
	offsetof(struct shadowrun_tech3_definition, antimagic_electricity) == 0x450 ? 1 : -1];

typedef char shadowrun_unit_size_assert[sizeof(struct shadowrun_unit_definition) == 0x600 ? 1 : -1];
typedef char shadowrun_unit_magic1_assert[
	offsetof(struct shadowrun_unit_definition, magic1.index) == 0x10C ? 1 : -1];
typedef char shadowrun_unit_tech3_assert[
	offsetof(struct shadowrun_unit_definition, tech3.index) == 0x15C ? 1 : -1];
typedef char shadowrun_unit_races_assert[offsetof(struct shadowrun_unit_definition, races) == 0x160 ? 1 : -1];
typedef char shadowrun_unit_weapon_assert[
	offsetof(struct shadowrun_unit_definition, default_weapon_data.index) == 0x22C ? 1 : -1];
typedef char shadowrun_unit_buy_menu_assert[
	offsetof(struct shadowrun_unit_definition, buy_menu.index) == 0x4FC ? 1 : -1];

typedef char shadowrun_race_size_assert[sizeof(struct shadowrun_race) == 0xA00 ? 1 : -1];
typedef char shadowrun_race_scale_assert[offsetof(struct shadowrun_race, scale) == 0x100 ? 1 : -1];
typedef char shadowrun_race_head_assert[offsetof(struct shadowrun_race, head.index) == 0x110 ? 1 : -1];
typedef char shadowrun_race_vitality_assert[offsetof(struct shadowrun_race, maximum_body_vitality) == 0x128 ? 1 : -1];
typedef char shadowrun_race_gust_assert[offsetof(struct shadowrun_race, gust_scale) == 0x160 ? 1 : -1];
typedef char shadowrun_race_damage_assert[offsetof(struct shadowrun_race, damage_scale) == 0x11C ? 1 : -1];
typedef char shadowrun_race_melee_assert[offsetof(struct shadowrun_race, melee_damage_scale) == 0x164 ? 1 : -1];
typedef char shadowrun_weapon_data_race_size_assert[sizeof(struct shadowrun_weapon_data_race) == 0x600 ? 1 : -1];
typedef char shadowrun_weapon_data_race_movement_assert[
	offsetof(struct shadowrun_weapon_data_race, movement_scale) == 0x100 ? 1 : -1];
typedef char shadowrun_weapon_data_size_assert[sizeof(struct shadowrun_weapon_data) == 0x600 ? 1 : -1];
typedef char shadowrun_weapon_data_races_assert[offsetof(struct shadowrun_weapon_data, races) == 0x500 ? 1 : -1];
typedef char shadowrun_weapon_data_smartlinked_assert[
	offsetof(struct shadowrun_weapon_data, smartlinked.index) == 0x518 ? 1 : -1];
typedef char shadowrun_unit_shop_weapons_assert[
	offsetof(struct shadowrun_unit_definition, shop_weapons) == 0x430 ? 1 : -1];
typedef char shadowrun_unit_buy_menu_assert[offsetof(struct shadowrun_unit_definition, buy_menu.index) == 0x4FC ? 1 : -1];
typedef char shadowrun_unit_prices_assert[offsetof(struct shadowrun_unit_definition, prices) == 0x500 ? 1 : -1];
typedef char shadowrun_buy_menu_ammunition_assert[
	offsetof(struct shadowrun_buy_menu_definition, ammunition_failed_sound.index) == 0xCC ? 1 : -1];
typedef char shadowrun_race_regeneration_assert[offsetof(struct shadowrun_race, regeneration_points) == 0x120 ? 1 : -1];
typedef char shadowrun_race_regeneration_damage_assert[
	offsetof(struct shadowrun_race, regeneration_damage.index) == 0x138 ? 1 : -1];
typedef char shadowrun_race_regeneration_effect_assert[
	offsetof(struct shadowrun_race, regeneration_effect.index) == 0x148 ? 1 : -1];
typedef char shadowrun_race_regeneration_threshold_assert[
	offsetof(struct shadowrun_race, regeneration_threshold) == 0x16C ? 1 : -1];
typedef char shadowrun_race_rage_assert[offsetof(struct shadowrun_race, rage_threshold) == 0x200 ? 1 : -1];
typedef char shadowrun_race_rage_material_assert[
	offsetof(struct shadowrun_race, rage_material_threshold) == 0x230 ? 1 : -1];
typedef char shadowrun_race_mana_assert[offsetof(struct shadowrun_race, mana_capacity) == 0x400 ? 1 : -1];
typedef char shadowrun_race_delay_assert[offsetof(struct shadowrun_race, mana_regeneration_delay) == 0x408 ? 1 : -1];

typedef char shadowrun_buy_menu_size_assert[sizeof(struct shadowrun_buy_menu_definition) == 0x7D0 ? 1 : -1];
typedef char shadowrun_buy_menu_sound_assert[
	offsetof(struct shadowrun_buy_menu_definition, cast_failed_sound.index) == 0x6FC ? 1 : -1];

#endif
