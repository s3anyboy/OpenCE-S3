/*
SHADOWRUN.H

The Shadowrun prototype's own game, played on its maps (docs/shadowrun_caches.md):
what its executable (cachesym.xbe, build 01.12.07.0132) adds to Halo's units.
This is its magic: mana, the spells and casting them, and the scripts that
give them. It runs only while one of its maps is loaded.

A unit on such a map carries the state Shadowrun adds to a unit after its own
datum, in the object's memory (so checkpoints keep it): its datum is made
larger by shadowrun_object_extra_size. A player's lasts across its units, in
the game state.
*/

#ifndef __SHADOWRUN_H
#define __SHADOWRUN_H
#pragma once

/* ---------- prototypes/SHADOWRUN_UNITS.C */

/* the game state's, at startup (game_initialize) */
void shadowrun_initialize(
	void);
/* at each map (game_initialize_for_new_map) */
void shadowrun_initialize_for_new_map(
	void);

/* the tags just loaded and checked: whether they are a Shadowrun prototype
map's (cache_files.c) */
void shadowrun_tags_loaded(
	boolean shadowrun);

/* whether the map loaded is the Shadowrun prototype's */
boolean shadowrun_map_loaded(
	void);

/* the bytes an object of the type carries after its datum (objects.c,
object_new): Shadowrun's state of a unit, on its maps; 0 otherwise */
short shadowrun_object_extra_size(
	short object_type);

/* a unit made (unit_new) */
void shadowrun_unit_new(
	long unit_index);

/* each tick of a unit, before its grenade is thrown (unit_update): its mana,
waits and spells; and the right bumper casts its spell, if it has one */
void shadowrun_unit_update(
	long unit_index);
/* a unit dead for good (unit_died), and deleted (unit_delete) */
void shadowrun_unit_died(
	long unit_index);
void shadowrun_unit_delete(
	long unit_index);

/* a player spawned with a new unit (players.c, player_spawn): its race, the
magic it knows, a full pool */
void shadowrun_player_spawned(
	long player_index);

/* ---------- prototypes/SHADOWRUN_BUY_MENU.C */

/* a local player's controls pressed this tick (player_control.c): the
Back control opens and closes the buy menu, and while it is open the others
work it. TRUE while it is open: the player does nothing else */
boolean shadowrun_buy_menu_input(
	short local_player_index,
	boolean toggle,
	boolean buy,
	boolean back,
	boolean magazine,
	real forward,
	real strafe);

/* ---------- prototypes/SHADOWRUN_CAMERA.C */

/* whether a local player's unit is seen from behind (director.c): holding
a melee weapon, or in its smoke. The view only: aiming is as ever */
boolean shadowrun_unit_third_person(
	long unit_index);
/* the unit camera track a unit is followed on, of its count
(following_camera.c): a melee weapon's holder's third */
short shadowrun_unit_camera_track(
	long unit_index,
	short track_index,
	long track_count);

/* ---------- prototypes/SHADOWRUN_HUD.C */

/* the unit's magic on the HUD, the local player's view's (hud.c) */
void shadowrun_hud_draw(
	short local_player_index,
	long unit_index);

/* ---------- prototypes/SHADOWRUN_RACES.C */

struct damage_data;
struct damage_definition;

/* the damage an object takes, of total_damage (damage.c, as it is reckoned):
as its race takes it, and its rage; the damage makes it rage */
real shadowrun_damage_scale(
	struct damage_data const *damage,
	struct damage_definition const *definition,
	long object_index,
	real total_damage);
/* the material the damage strikes, of the object (damage.c): a troll's rage
looks as a jackal's shield */
void shadowrun_damage_material(
	struct damage_data *damage,
	long object_index);
/* a player's biped's speed and acceleration (bipeds.c): its race's with its
weapon, and its rage's */
void shadowrun_biped_movement_scales(
	long biped_index,
	real *movement_scale,
	real *acceleration_scale);
/* a unit hurt (units.c, unit_damage_aftermath): its body waits to heal */
void shadowrun_unit_damaged(
	long unit_index);
/* how fast a player's unit turns (player_control.c): its rage's */
real shadowrun_unit_turning_scale(
	long unit_index);
/* an actor attached to its unit (actors.c, actor_attach_unit): the unit
takes its variant's race */
void shadowrun_actor_attached(
	long actor_variant_definition_index,
	long unit_index);
/* an actor's unit given its variant's change colour (actors.c) */
void shadowrun_actor_change_color(
	long unit_index,
	short change_color_index);

/* ---------- prototypes/SHADOWRUN_MAGIC.C */

/* whether damage of the owner reaches the object (damage.c): not from a unit
hidden in smoke, nor to one camouflaged in it or taking a spirit animal's
form */
boolean shadowrun_damage_allowed(
	long owner_object_index,
	long object_index);

/* each tick of the game, after the objects' (game.c): the trees of life heal */
void shadowrun_game_tick(
	void);

/* a projectile detonated (projectiles.c): its spell's (a tree of life grows) */
void shadowrun_projectile_detonate(
	long projectile_index,
	real_point3d const *position);

/* a unit's camouflage this tick (units.c): a unit of the prototype's with its
smoke's definition comes and goes at the smoke's rates, up to the smoke's
camouflage. FALSE for others, whose camouflage is this build's */
boolean shadowrun_unit_camouflage_update(
	long unit_index);

/* the control that chooses (player_control.c: the left bumper, on these
maps): it chooses the spell the right bumper casts, then the grenade. TRUE
when it was taken for a spell */
boolean shadowrun_unit_rotate_selection(
	long unit_index,
	short desired_grenade_index);

/* the scripts' player_set_magic, player_change_magic and
game_set_assignment_scheme */
void shadowrun_players_set_magic(
	short spell);
void shadowrun_players_change_magic(
	short spell);
void shadowrun_game_set_assignment_scheme(
	short scheme);

/* port: the techs the players have, for the console's shadowrun_tech (a
map's tech tags have nothing that grants one: docs/shadowrun_caches.md,
"Tech"): FALSE if the map has no tag of the group the tech is of, or the
players' units are not on a Shadowrun map. The index is the map's tag */
boolean shadowrun_players_grant_tech(
	short tech,
	long tag_index);
/* ... and the techs they have, in the console's form */
void shadowrun_players_techs(
	char *text,
	unsigned long size);

#endif
