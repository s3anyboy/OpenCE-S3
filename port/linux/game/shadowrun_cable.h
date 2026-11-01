/*
SHADOWRUN_CABLE.H

The Shadowrun prototype's cable, as the executable keeps it: the grapple
(its end travels from the unit the way it aims; the world it holds reels the
unit in to hang there, a biped it holds is pulled to the unit), which is a
tag of the map's (t2ed, its srud's tech2: shadowrun_definitions.h).

Ported from the prototype's executable (cachesym.xbe, build 01.12.07.0132):
units.c FUN_0025E860 gives a unit its srud's tech2; bipeds.c FUN_002541D0
holds the cable (0x10B8), units.c FUN_00284EA0's collision fires it, and
FUN_0025B650 lets it go.
*/

#ifndef __SHADOWRUN_CABLE_H
#define __SHADOWRUN_CABLE_H
#pragma once

/* ---------- headers */

#include "cseries.h"
#include "shadowrun_definitions.h"

/* ---------- constants */

enum
{
	/* the cable's state, as bipeds.c FUN_002541D0 numbers it (0x10B8): its
	end travelling, the unit reeled in to where it holds the world, hanging
	there, pulling a biped to it, and (the biped's) pulled */
	_shadowrun_cable_state_none = 0,
	_shadowrun_cable_state_fired = 1,
	_shadowrun_cable_state_reeling = 2,
	_shadowrun_cable_state_hanging = 3,
	_shadowrun_cable_state_pulling = 4,
	_shadowrun_cable_state_pulled = 5,
};

/* ---------- prototypes/SHADOWRUN_CABLE.C */

/* a unit's cable state and its own fields, kept by the unit's index */
void shadowrun_cable_initialize_for_new_map(
	void);
void shadowrun_cable_new(
	long unit_index);
void shadowrun_cable_delete(
	long unit_index);

/* the cable's tech taken up or let go (units.c FUN_0027D4E0, FUN_0027D760) */
void shadowrun_cable_begin(
	long unit_index);
void shadowrun_cable_end(
	long unit_index);
/* the cable released by itself (FUN_0025B5E0) */
void shadowrun_cable_force_release(
	long unit_index);

/* FUN_0025B7D0: fires it, and reels the unit in. TRUE when it fired */
boolean shadowrun_cable_fire(
	long unit_index);
/* FUN_002541D0: each tick of a biped's physics (bipeds.c): TRUE when the
cable moves it, its velocity and movement set, and it flies */
boolean shadowrun_cable_physics(
	long biped_index,
	real_vector3d *velocity,
	real_vector3d *movement_desired);
/* each tick of a unit: the cable's waits, its hook and its line (FUN_00251840).
TRUE while its cable is out */
boolean shadowrun_cable_update(
	long unit_index);
/* whether the unit is reeling in, or held by a cable (shadowrun_camera.c) */
boolean shadowrun_cable_reeling(
	long unit_index);
/* port: whether a cable holds the unit now, to the world or to a unit: it is
not hurt by falling while one does (bipeds.c's falling damage) */
boolean shadowrun_cable_holds(
	long unit_index);

#endif
