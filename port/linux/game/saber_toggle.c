/*
SABER_TOGGLE.C

The Back button switching the remastered models and level on and off in the
game (saber_toggle.h). A press of it (let go of within five seconds) switches
between the map's own models and Halo: Combat Evolved Anniversary's; held
five seconds, it switches the remastered structure and sky
(display.remastered_bsp) on or off instead. The models' tags are
written as the press says: each template's geometries are written into the
map's model and taken out again (saber.h's saber_models_anniversary_set), so
that everything that draws the model reads either the package's geometry or
the map's own, its shaders, regions and nodes as the map's. The level's
structure is only made when display.remastered_bsp asks for it; Back says
whether it is drawn (saber_models_level_drawn). The package's models are made as the map
loads when display.anniversary_models asks for them, so the switch only says
which are drawn.

The press is read as the game draws (render_objects.c).
*/

/* ---------- headers */

#include "saber_toggle.h"

#include "cseries/cseries.h"
#include "input/input.h"
#include "halo_keyboard.h"

#include <xtl.h>

/* ---------- the models (port/linux/game/saber_models.c) */

long saber_models_anniversary_set(int shown);
long saber_models_geometry_count(void);

/* ---------- the platform layer's (port/linux/src/port_config.c) */

int config_boolean(const char *name);
void platform_log(const char *format, ...);

/* ---------- constants */

/* how long Back is held to switch the remastered structure (milliseconds) */
#define SABER_TOGGLE_LEVEL_HOLD 5000UL

/* ---------- globals */

static struct
{
	/* Back was held when the game was last looked at, since when, and
	whether this hold has switched the structure already */
	boolean held;
	unsigned long held_since;
	boolean hold_switched;
	/* the package's models drawn, and its structure and sky */
	boolean models;
	boolean level;
} saber_toggle;

/* ---------- private code */

/* whether Back is held: the first controller's (the keyboard's F1 presses
it and Back, port/linux/src/xinput_sdl.c), or the keyboard's scoreboard key */
static boolean saber_toggle_held(
	void)
{
	struct gamepad_state const *pad = input_get_gamepad_state(0);

	if (pad && pad->buttons[_gamepad_binary_button_back] > 0)
		return TRUE;

	return (halo_keyboard_actions(0) & (1UL << HALO_KEYBOARD_SCOREBOARD)) != 0;
}

/* ---------- public code */

void saber_toggle_initialize(
	void)
{
	csmemset(&saber_toggle, 0, sizeof(saber_toggle));
	/* (as the settings ask, until Back is pressed) */
	saber_toggle.models = config_boolean("display.anniversary_models") ? TRUE : FALSE;
	saber_toggle.level = config_boolean("display.remastered_bsp") ? TRUE : FALSE;
}

void saber_toggle_update(
	void)
{
	boolean held = saber_toggle_held();
	unsigned long now = GetTickCount();

	if (held && !saber_toggle.held)
	{
		/* (pressed: timed from now) */
		saber_toggle.held_since = now;
		saber_toggle.hold_switched = FALSE;
	}
	else if (held && !saber_toggle.hold_switched && now - saber_toggle.held_since >= SABER_TOGGLE_LEVEL_HOLD)
	{
		/* (held five seconds: the structure and sky switched, once a hold) */
		saber_toggle.hold_switched = TRUE;
		saber_toggle.level = !saber_toggle.level;
		platform_log("anniversary: Back held: the %s structure and sky", saber_toggle.level ? "remastered" : "classic");
	}
	else if (!held && saber_toggle.held && !saber_toggle.hold_switched)
	{
		/* (let go of before then: the models switched) */
		long models;
		long geometries;

		saber_toggle.models = !saber_toggle.models;
		/* (the tags written over, before the next frame reads them) */
		models = saber_models_anniversary_set(saber_toggle.models);
		geometries = saber_models_geometry_count();
		platform_log("anniversary: Back pressed: the %s models: %ld models of the map's %ld geometries",
			saber_toggle.models ? "remastered" : "classic", models, geometries);
	}
	saber_toggle.held = held;
}

int saber_toggle_models_wanted(
	void)
{
	return config_boolean("display.anniversary_models");
}

int saber_toggle_classic_models(
	void)
{
	return !saber_toggle.models;
}

int saber_toggle_classic_level(
	void)
{
	return !saber_toggle.level;
}
