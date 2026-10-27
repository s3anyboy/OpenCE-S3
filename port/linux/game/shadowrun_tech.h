/* shadowrun_tech.h: the Shadowrun prototype's tech (port/linux/game/shadowrun_tech.c)

A unit's known_powers holds its magic and its tech together: the spells
first, then the tech. Buying tech works as buying a spell does. Of the five
items only Enhanced Vision does anything yet: it marks enemies with nav
points. */

#ifndef HALO_SHADOWRUN_TECH_H
#define HALO_SHADOWRUN_TECH_H

/* the known_powers place of a tech item (its index in the buy menu's tech
items), or NONE past them */
short shadowrun_tech_power(short tech);

/* the tech the unit has been given, used (the left bumper; only Enhanced
Vision does anything, and it draws each tick) */
void shadowrun_tech_use(long unit_index);

/* Enhanced Vision: a nav point over every enemy within 60 m of the local
player's unit (game_engine.c's game_engine_render_nav_points) */
void shadowrun_tech_render_nav_points(short local_player_index);

#endif
