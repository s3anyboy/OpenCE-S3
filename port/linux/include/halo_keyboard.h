/*
HALO_KEYBOARD.H

The keyboard and mouse's own controls (port/linux/src/xinput_sdl.c): in the
game they do not drive the first controller, as they do in the menus, but
the player's actions themselves, by the bindings in config.toml's
[controls] (Settings > Controls Setup changes them). The game takes them
with the first controller's (source/input/input_abstraction.c), so the
profile's button layout is the controller's alone, and reloading has a key
of its own (source/game/player_control.c).
*/

#ifndef HALO_KEYBOARD_H
#define HALO_KEYBOARD_H

enum halo_keyboard_action
{
	HALO_KEYBOARD_MOVE_FORWARD,
	HALO_KEYBOARD_MOVE_BACKWARD,
	HALO_KEYBOARD_STRAFE_LEFT,
	HALO_KEYBOARD_STRAFE_RIGHT,
	HALO_KEYBOARD_JUMP,
	HALO_KEYBOARD_CROUCH,
	HALO_KEYBOARD_FIRE,
	HALO_KEYBOARD_THROW_GRENADE,
	HALO_KEYBOARD_MELEE,
	HALO_KEYBOARD_RELOAD,
	HALO_KEYBOARD_ZOOM,
	HALO_KEYBOARD_SWITCH_WEAPON,
	HALO_KEYBOARD_SWITCH_GRENADE,
	HALO_KEYBOARD_ACTION,
	HALO_KEYBOARD_FLASHLIGHT,
	/* (these two press the controller's Back and Start, which the menus
	read from it) */
	HALO_KEYBOARD_SCOREBOARD,
	HALO_KEYBOARD_PAUSE,
	HALO_KEYBOARD_SCREENSHOT,
	/* (voice chat's, which the game itself does not read:
	halo_push_to_talk_held) */
	HALO_KEYBOARD_PUSH_TO_TALK,
	/* (the text chat's, which the game itself does not read: a line to
	everyone, or to the team: port/linux/game/network_chat.c) */
	HALO_KEYBOARD_CHAT,
	HALO_KEYBOARD_TEAM_CHAT,
	NUMBER_OF_HALO_KEYBOARD_ACTIONS
};

/* the actions the keyboard and mouse hold for the player on the controller
(a bit for each halo_keyboard_action): theirs is the first's; none while a
menu or the console is up, or a chat line is typed */
unsigned long halo_keyboard_actions(short controller_index);
/* whether voice chat's push to talk is held (controls.push_to_talk), in the
game and in the menus alike */
int halo_push_to_talk_held(void);

/* the first controller's buttons (with the touch controls'), as the text
chat's on-screen keyboard reads them: a bit for each (port/linux/game/
network_chat.c). The game gets none of the controller while that keyboard is
up (network_chat_keyboard_open), nor the touch controls' stick and look */
enum halo_chat_pad_button
{
	HALO_CHAT_PAD_UP,
	HALO_CHAT_PAD_DOWN,
	HALO_CHAT_PAD_LEFT,
	HALO_CHAT_PAD_RIGHT,
	HALO_CHAT_PAD_A,
	HALO_CHAT_PAD_B,
	HALO_CHAT_PAD_X,
	HALO_CHAT_PAD_Y,
	HALO_CHAT_PAD_START,
	/* the left stick, pushed most of the way */
	HALO_CHAT_PAD_STICK_UP,
	HALO_CHAT_PAD_STICK_DOWN,
	HALO_CHAT_PAD_STICK_LEFT,
	HALO_CHAT_PAD_STICK_RIGHT,
	NUMBER_OF_HALO_CHAT_PAD_BUTTONS
};

unsigned long halo_chat_pad_buttons(void);

/* the d-pad of a controller as its last read found it: the control of one of
the game's own (source/input/input.h) does not carry the d-pad, and the
Shadowrun prototype's maps read it for their spells and their buy menu
(port/linux/src/xinput_sdl.c, source/game/player_control.c) */
enum halo_linux_dpad_button
{
	HALO_LINUX_DPAD_UP = 1,
	HALO_LINUX_DPAD_DOWN = 2,
	HALO_LINUX_DPAD_LEFT = 4,
	HALO_LINUX_DPAD_RIGHT = 8,
};

unsigned int halo_linux_gamepad_dpad(short gamepad_index);
/* the d-pad's buttons that have just gone down (HALO_LINUX_DPAD_*): a press
once, however many reads it lasts (a held d-pad may come as repeats); the
buttons held are passed back so that the next call knows them. The first
call of the game's takes nothing: whatever was held while the menus had it
is not a press */
void halo_linux_gamepad_dpad_game_seen(unsigned int buttons);
int halo_linux_gamepad_dpad_pressed(unsigned int buttons);
/* the taps on the touchscreen outside the menus since the last call while
that keyboard is up, and where the latest was (0 to 1 across and down the
window) */
int halo_chat_taps(float *x, float *y);
/* whether the touch controls hold the screen's bottom left, where the chat
then goes at the top left: on Android, and in a browser once its touch
controls have been used */
int halo_chat_touch_layout(void);

#endif
