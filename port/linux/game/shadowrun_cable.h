/*
SHADOWRUN_CABLE.H

The Shadowrun prototype's cable, as the executable keeps it: the grapple
(fired level, 30 m, the first thing it hits attached to, the unit reeled in
over the tag's ticks), the glider, and the heal of the undead, which are one
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
	/* the cable's state, as bipeds.c FUN_002541D0 numbers it (0x10B8) */
	_shadowrun_cable_state_none = 0,
	_shadowrun_cable_state_attached_to_world = 1,
	_shadowrun_cable_state_attached_to_unit = 2,
	_shadowrun_cable_state_reeling = 3,
	_shadowrun_cable_state_released = 4,
	_shadowrun_cable_state_gliding = 5,
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
/* FUN_002541D0: each tick. TRUE when the unit is held */
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
