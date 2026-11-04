/*
SABER_TOGGLE.C

The Back button switching the remastered models on and off in the game
(saber_toggle.h). Held, it shows Halo: Combat Evolved Anniversary's graphics
where display.anniversary_models and display.remastered_bsp turned them off,
or the map's own where they turned them on, as Anniversary's Back does; let
go of, the map is drawn as the settings say again. The package's models are
made as the map loads when either setting asks for them, so the switch only
says which are drawn.

Read by whichever thread draws (in a browser, the worker the game runs in).
*/

/* ---------- headers */

#include "saber_toggle.h"

#include "cseries/cseries.h"
#include "input/input.h"
#include "halo_keyboard.h"

#include <xtl.h>

/* ---------- the platform layer's (port/linux/src/port_config.c) */

int config_boolean(const char *name);
void platform_log(const char *format, ...);

/* ---------- the models and the level (saber_toggle.h, which says what
saber_models.c and models.c ask) */

int saber_toggle_classic_models(void);
int saber_toggle_level_wanted(void);

/* ---------- globals */

static struct
{
	/* Back was held when the game was last looked at */
	boolean held;
	/* the map is drawn the other way to its settings while it is */
	boolean other;
	/* the settings draw it remastered already */
	boolean remastered;
} saber_toggle;

/* ---------- private code */

/* whether Back is held: the first controller's (the keyboard's F1 presses
it, port/linux/src/xinput_sdl.c), or the keyboard's scoreboard key (Tab) */
static boolean saber_toggle_held(
	void)
{
	struct gamepad_state const *pad = input_get_gamepad_state(0);

	if (pad && pad->buttons[_gamepad_binary_button_back] > 0)
		return TRUE;

	return (halo_keyboard_actions(0) & (1UL << HALO_KEYBOARD_SCOREBOARD)) != 0;
}

/* ---------- public code */

void saber_toggle_update(
	void)
{
	boolean held = saber_toggle_held();

	saber_toggle.remastered = (config_boolean("display.anniversary_models") ||
		config_boolean("display.remastered_bsp")) ? TRUE : FALSE;
	/* (each press of Back is a toggle, so that the key held while a chat's
	fingerprint is typed gives the frame's models back when it is let go
	of; a setting changed in between is taken up first) */
	if (held != saber_toggle.held)
	{
		saber_toggle.other = saber_toggle.other ? FALSE : TRUE;
		saber_toggle.held = held;
		platform_log("anniversary: Back %s: the map is drawn %s", held ? "held" : "let go of",
			saber_toggle.other != saber_toggle.remastered ? "the other way" : "as its settings say");
	}
}

int saber_toggle_classic_models(
	void)
{
	return saber_toggle.other != saber_toggle.remastered ? TRUE : FALSE;
}

int saber_toggle_level_wanted(
	void)
{
	return saber_toggle.other != saber_toggle.remastered ? TRUE : FALSE;
}
