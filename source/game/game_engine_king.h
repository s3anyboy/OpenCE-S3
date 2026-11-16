/*
GAME_ENGINE_KING.H
*/

#ifndef __GAME_ENGINE_KING_H
#define __GAME_ENGINE_KING_H
#pragma once

/* ---------- headers */

#include "game/game_engine.h"

/* ---------- constants */

enum king_message
{
	king_message_enemy_on_the_hill = 0x1E,
	king_message_ally_on_the_hill,
	king_message_you_are_on_the_hill,
};

enum king_hill_state
{
	king_hill_uncontrolled = 0,
	king_hill_controlled,
	king_hill_controlled_red,
	king_hill_controlled_blue,
	king_hill_contested,
};

/* ---------- structures */

struct model_vertex_uncompressed;
struct render_animation;
struct render_lighting;

struct king_globals
{
	/* port: score and score_tick are indexed by team (free for all gives
	every player a team), on_the_hill by absolute player index, so all three
	follow the session player limit (halo_port_limits.h) */
	long score[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long score_tick[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	boolean on_the_hill[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long hill_point_count;
	real_point3d hill_points[12];
	real_point2d convex_hull[12];
	real_point3d hill_center;
	long hill_state;
	long hill_controlled_count;
	long hill_previous_controller;
	real hill_top;
	real hill_bottom;
	long hill_id;
	long hill_timer;
};

/* January's layout; the port's per-player arrays are larger */

/* ---------- prototypes/GAME_ENGINE_KING.C */

void render_dynamic_quad_initialize(
	void);

void render_dynamic_quad(
	struct model_vertex_uncompressed *vertices,
	long shader_index,
	struct render_lighting const *lighting,
	struct render_animation const *animation,
	real u_scale,
	real v_scale);

/* port: Warzone (port/linux/game/warzone.c), whose points are the team
scores: King leaves them to it, and its clients make no sound of them */
void game_engine_king_set_warzone(
	boolean warzone);
/* the hills in play, and the team whose players alone are on one (NONE) */
short game_engine_king_hill_count(
	void);
long game_engine_king_hill_holder(
	short hill_index);
/* a team's score (the host's, which it sends its clients), in ticks */
long game_engine_king_team_score(
	long team_index);
void game_engine_king_add_score(
	long team_index,
	long score);

/* ---------- globals */

extern struct game_engine king_engine;

#endif // __GAME_ENGINE_KING_H
