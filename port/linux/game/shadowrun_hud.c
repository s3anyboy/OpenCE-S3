/*
SHADOWRUN_HUD.C

The Shadowrun prototype's magic on the HUD (shadowrun.h), at the bottom of
the screen: the spell the right bumper casts (or Grenade, when the d-pad's
right has chosen the grenades) and the unit's magic, a cell a point: lit
what it has, dim what it has
not, and dark red what its spells hold back. The numbers are the
executable's own ("%d/%d"). A spell that could not be cast makes the line
flash red for a second; one waiting to be cast again is grey.

The executable drew its own HUD of slots and icons, assigned from a radial
menu (its globals' assignment scheme), with bitmaps of its globals and the
srud's: this is a plainer one, of the same facts.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "objects/objects.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/units.h"
#include "shadowrun_units.h"
#include "shadowrun_tech.h"
#include "shadowrun.h"

#include <stdio.h>

/* ---------- constants */

enum
{
	/* the line's bottom from the screen's, the cells' width, gap and the
	gap between them and the text (the HUD's 480 lines) */
	SHADOWRUN_HUD_BOTTOM_MARGIN = 34,
	SHADOWRUN_HUD_CELL_WIDTH = 6,
	SHADOWRUN_HUD_CELL_GAP = 2,
	SHADOWRUN_HUD_TEXT_GAP = 8,
	/* the most cells drawn */
	SHADOWRUN_HUD_MAXIMUM_CELLS = 30,
	/* how long a failed cast flashes (ticks) */
	SHADOWRUN_HUD_FAILURE_TICKS = 30,
	/* the gap between the tech and the spell on the one line */
	SHADOWRUN_HUD_TECH_GAP = 12,
	/* the accuracy reticle's lines' length, and their gap from the
	crosshair's point at the least accuracy (FUN_000EF590's 15 and 20) */
	SHADOWRUN_HUD_ACCURACY_LENGTH = 15,
	SHADOWRUN_HUD_ACCURACY_SPREAD = 20,
	/* draw_string.c's justifications (its enum is its own) */
	_shadowrun_hud_text_left = 0,
	_shadowrun_hud_text_right,
};

/* the spells' names, as the executable names them (at 0x4865E8) */
static char const *const shadowrun_spell_names[NUMBER_OF_SHADOWRUN_SPELLS] =
{
	"Teleport",
	"Gust",
	"Tree of Life",
	"Resurrection",
	"Strangle",
	"Summon Elemental",
	"Spirit Animal",
	"Pillar of Salt",
	"Smoke",
};

/* port: the techs' names, by the executable's ids (units.c, FUN_0027d4e0),
for the tech the left bumper uses */
static char const *shadowrun_tech_name(
	short tech)
{
	switch (tech)
	{
	case _shadowrun_tech_reflexes_id: return "Reflexes";
	case _shadowrun_tech_vision_id: return "Enhanced Vision";
	case _shadowrun_tech_smartlink_id: return "Smartlink";
	case _shadowrun_tech_gecko_id: return "Gecko";
	case _shadowrun_tech_glider_id: return "Glider";
	case _shadowrun_tech_grapple_id: return "Grapple";
	case _shadowrun_tech_zoom_id: return "Zoom";
	case _shadowrun_tech_hologram_id: return "Hologram";
	case _shadowrun_tech_hearing_id: return "Hearing";
	case _shadowrun_tech_heal_id: return "Heal";
	default: return "";
	}
}

/* ---------- globals */

/* port: each local player's bloom reticle, as its crosshair gave it this
frame (shadowrun_hud_draw_accuracy) */
static struct
{
	long unit_index;
	short x;
	short y;
	pixel32 color;
	boolean given;
} shadowrun_hud_reticles[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];

/* when each local player's unit last failed a cast */
static long shadowrun_hud_failure_times[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS] = { NONE, NONE, NONE, NONE };

/* ---------- private code */

static pixel32 shadowrun_hud_pixel(
	real_argb_color const *color)
{
	return ((pixel32)(PIN(color->alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24) |
		((pixel32)(PIN(color->red, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
		((pixel32)(PIN(color->green, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
		(pixel32)(PIN(color->blue, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static void shadowrun_hud_set_color(
	real_argb_color *color,
	real alpha,
	real red,
	real green,
	real blue)
{
	color->alpha = alpha;
	color->red = red;
	color->green = green;
	color->blue = blue;
}

static void shadowrun_hud_draw_accuracy_reticle(
	long unit_index,
	short x,
	short y,
	pixel32 color);

/* ---------- public code */

void shadowrun_hud_draw(
	short local_player_index,
	long unit_index)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	long font_index;
	struct font_header *font;
	short line_height;
	short cell_count;
	short held_cells;
	short lit_cells;
	short index;
	short spell;
	real capacity;
	char text[64];
	real_argb_color text_color;
	rectangle2d bounds;
	rectangle2d cell;
	short top;
	short left;

	/* port: without a unit (one spawning, or dead) there is nothing to draw,
	nor a hand to show: not even the buy menu, which needs one to sell to */
	if (!shadowrun_unit || cinematic_in_progress() ||
		local_player_index < 0 || local_player_index >= MAXIMUM_NUMBER_OF_LOCAL_PLAYERS)
	{
		return;
	}
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
	{
		/* (the menu has a font of its own to be drawn with) */
		shadowrun_buy_menu_draw(local_player_index);

		return;
	}
	/* port: a player with no srud at all (a map that has none) has no magic
	and no tech of its unit to show, but the buy menu is still drawn on its
	own: the menu shows the player its money. One whose biped names none
	plays by the map's (shadowrun_unit_definition_player: b30sr.map's) */
	if (!shadowrun_unit_definition_player(unit_index))
	{
		shadowrun_buy_menu_draw(local_player_index);

		return;
	}
	/* port: the bloom reticle, about the crosshair's point (in its colour)
	the weapon's HUD gave this frame, else the screen's middle, in the HUD's
	blue (a HUD colour: its alpha the transparency) */
	{
		short x;
		short y;
		pixel32 color;

		if (shadowrun_hud_reticles[local_player_index].given &&
			shadowrun_hud_reticles[local_player_index].unit_index == unit_index)
		{
			x = shadowrun_hud_reticles[local_player_index].x;
			y = shadowrun_hud_reticles[local_player_index].y;
			color = shadowrun_hud_reticles[local_player_index].color;
		}
		else
		{
			x = (short)((render.camera.window_bounds.x0 + render.camera.window_bounds.x1) / 2 - render.camera.viewport_bounds.x0);
			y = (short)((render.camera.window_bounds.y0 + render.camera.window_bounds.y1) / 2 - render.camera.viewport_bounds.y0);
			color = 0x002896FF;
		}
		shadowrun_hud_reticles[local_player_index].given = FALSE;
		shadowrun_hud_draw_accuracy_reticle(unit_index, x, y, color);
	}
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
	{
		return;
	}

	/* a failed cast flashes, once seen */
	spell = (short)(shadowrun_unit->current_spell - 1);
	for (index = 0; index < NUMBER_OF_SHADOWRUN_SPELLS; index++)
	{
		if (shadowrun_unit->spell_failed[index])
		{
			shadowrun_unit->spell_failed[index] = FALSE;
			shadowrun_hud_failure_times[local_player_index] = game_time_get();
		}
	}
	if (spell >= 0 && spell < NUMBER_OF_SHADOWRUN_SPELLS)
	{
		snprintf(text, sizeof(text), "%s", shadowrun_spell_names[spell]);
	}
	else
	{
		/* port: no spell chosen shows nothing (the grenades are not one of the
		spells the control chooses) */
		text[0] = 0;
	}
	if (shadowrun_hud_failure_times[local_player_index] != NONE &&
		game_time_get() - shadowrun_hud_failure_times[local_player_index] < SHADOWRUN_HUD_FAILURE_TICKS &&
		game_time_get() >= shadowrun_hud_failure_times[local_player_index])
	{
		shadowrun_hud_set_color(&text_color, (game_time_get() / 4) % 2 ? 1.0f : 0.5f, 1.0f, 0.3f, 0.3f);
	}
	else if (spell >= 0 && spell < NUMBER_OF_SHADOWRUN_SPELLS &&
		(shadowrun_unit->cast_cooldown_ticks > 0 || shadowrun_unit->power_cooldown_ticks[spell] > 0))
	{
		shadowrun_hud_set_color(&text_color, 0.9f, 0.55f, 0.55f, 0.6f);
	}
	else
	{
		shadowrun_hud_set_color(&text_color, 1.0f, 0.55f, 0.75f, 1.0f);
	}

	/* the cells: the pool's capacity, what it holds, what its spells hold
	back */
	capacity = shadowrun_unit_mana_capacity(unit_index);
	cell_count = (short)PIN((long)(capacity + 0.5f), 0, SHADOWRUN_HUD_MAXIMUM_CELLS);
	held_cells = (short)PIN(shadowrun_unit->tech_capacity_reserved + shadowrun_unit->magic_capacity_reserved, 0, cell_count);
	lit_cells = (short)PIN((long)shadowrun_unit->mana, 0, cell_count - held_cells);

	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	top = (short)(bounds.y1 - SHADOWRUN_HUD_BOTTOM_MARGIN - line_height);
	left = (short)((bounds.x0 + bounds.x1) / 2 -
		(cell_count * (SHADOWRUN_HUD_CELL_WIDTH + SHADOWRUN_HUD_CELL_GAP)) / 2);
	for (index = 0; index < cell_count; index++)
	{
		real_argb_color cell_color;
		real fraction = 0.f;

		if (index >= cell_count - held_cells)
		{
			shadowrun_hud_set_color(&cell_color, 0.85f, 0.45f, 0.1f, 0.12f);
		}
		else if (index < lit_cells)
		{
			shadowrun_hud_set_color(&cell_color, 0.95f, 0.45f, 0.7f, 1.0f);
		}
		else
		{
			shadowrun_hud_set_color(&cell_color, 0.6f, 0.2f, 0.22f, 0.3f);
			/* (the cell filling) */
			if (index == lit_cells)
			{
				fraction = shadowrun_unit->mana - (real)lit_cells;
			}
		}
		cell.x0 = (short)(left + index * (SHADOWRUN_HUD_CELL_WIDTH + SHADOWRUN_HUD_CELL_GAP));
		cell.x1 = (short)(cell.x0 + SHADOWRUN_HUD_CELL_WIDTH);
		cell.y0 = (short)(top + line_height + 3);
		cell.y1 = (short)(cell.y0 + 8);
		draw_quad(&cell, shadowrun_hud_pixel(&cell_color));
		if (fraction > 0.f)
		{
			real_argb_color filling;

			shadowrun_hud_set_color(&filling, 0.8f, 0.4f, 0.6f, 0.9f);
			cell.y0 = (short)(cell.y1 - (short)(8.f * PIN(fraction, 0.f, 1.f)));
			if (cell.y0 < cell.y1)
			{
				draw_quad(&cell, shadowrun_hud_pixel(&filling));
			}
		}
	}

	/* the spell, and the numbers */
	snprintf(text + strlen(text), sizeof(text) - strlen(text), "  %d/%d",
		(int)shadowrun_unit->mana, (int)(capacity + 0.5f));
	bounds.x0 = left;
	bounds.x1 = (short)(left + 400);
	bounds.y0 = top;
	bounds.y1 = (short)(top + line_height);
	draw_string_set_draw_mode(font_index, NONE, _shadowrun_hud_text_left, 0, &text_color);
	rasterizer_draw_string(&bounds, NULL, NULL, 0, text);

	/* port: the tech the left bumper uses, on the spell's line, to the left of
	the spell (right-justified against it, so neither moves when the other's

text changes). Bright while it runs, grey otherwise */
	{
		short tech = shadowrun_unit_tech(unit_index);
		char const *tech_name = tech != _shadowrun_tech_none ? shadowrun_tech_name(tech) : NULL;

		if (tech_name && *tech_name)
		{
			char tech_text[64];
			real_argb_color tech_color;

			/* (blue while it may be used, as the spell's is, and grey while it
			runs: its own time, as the spell's cooldown) */
			if (shadowrun_unit->tech_ticks > 0)
			{
				shadowrun_hud_set_color(&tech_color, 0.9f, 0.55f, 0.55f, 0.6f);
			}
			else
			{
				shadowrun_hud_set_color(&tech_color, 1.0f, 0.55f, 0.75f, 1.0f);
			}
			snprintf(tech_text, sizeof(tech_text), "%s", tech_name);
			bounds.x0 = (short)(left - SHADOWRUN_HUD_TECH_GAP - 400);
			bounds.x1 = (short)(left - SHADOWRUN_HUD_TECH_GAP);
			bounds.y0 = top;
			bounds.y1 = (short)(top + line_height);
			draw_string_set_draw_mode(font_index, NONE, _shadowrun_hud_text_right, 0, &tech_color);
			rasterizer_draw_string(&bounds, NULL, NULL, 0, tech_text);
		}
	}

	/* port: the anti-magic grenades a player carries (its third type of
	grenade, shadowrun_tech.c), above the spell's line: bright while they are
	the grenade chosen. The game's grenade panel counts them then, but shows
	the plasma grenade's picture */
	{
		short antimagic_grenades = shadowrun_unit_antimagic_grenades(unit_index);

		if (antimagic_grenades > 0)
		{
			char grenade_text[64];
			real_argb_color grenade_color;

			if (shadowrun_unit->antimagic_grenade_chosen)
			{
				shadowrun_hud_set_color(&grenade_color, 1.0f, 0.55f, 0.75f, 1.0f);
			}
			else
			{
				shadowrun_hud_set_color(&grenade_color, 0.9f, 0.55f, 0.55f, 0.6f);
			}
			snprintf(grenade_text, sizeof(grenade_text), "Anti-Magic Grenade x%d", (int)antimagic_grenades);
			bounds.x0 = left;
			bounds.x1 = (short)(left + 400);
			bounds.y0 = (short)(top - line_height);
			bounds.y1 = top;
			draw_string_set_draw_mode(font_index, NONE, _shadowrun_hud_text_left, 0, &grenade_color);
			rasterizer_draw_string(&bounds, NULL, NULL, 0, grenade_text);
		}
	}

	/* the buy menu, if it is open (shadowrun_buy_menu.c) */
	shadowrun_buy_menu_draw(local_player_index);

	return;
}

/* game_engine.c FUN_000EF590: the prototype's accuracy, about the aim
crosshair's point (x, y) in its colour: four lines of 15 pixels, right, left,
below and above it, apart by 20 pixels as the unit is least accurate (none
as it is most), and its weapon's srwd's gap more. A biped's alone, with an
srud, whose weapon has accuracy */
static void shadowrun_hud_draw_accuracy_reticle(
	long unit_index,
	short x,
	short y,
	pixel32 color)
{
	struct shadowrun_unit *shadowrun_unit = shadowrun_unit_get(unit_index);
	struct unit_datum *unit = shadowrun_unit ? unit_get(unit_index) : NULL;

	/* port: why the reticle is or is not drawn, said when that changes (its
	weapon, its unit's accuracy), so that one that does not show can be
	told from the log */
	{
		static long said_weapon_index = -2;
		static short said_reason = -1;
		struct shadowrun_weapon_data *data = unit ? shadowrun_unit_weapon_data_get(unit_index) : NULL;
		long weapon_index = unit && unit->unit.current_weapon_index != NONE ?
			unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index) : NONE;
		short reason =
			!unit ? 0 :
			unit->object.type != _object_type_biped ? 1 :
			!shadowrun_unit_definition_player(unit_index) ? 2 :
			!data ? 3 :
			!TEST_FLAG(data->flags, SHADOWRUN_WEAPON_DATA_ACCURACY_BIT) ? 4 :
			!shadowrun_unit->accuracy_active ? 5 : 6;

		if (reason != said_reason || weapon_index != said_weapon_index)
		{
			static char const *const reasons[] =
			{
				"no Shadowrun unit", "not a biped", "no srud", "no srwd (weapon or srud's)",
				"its srwd has no accuracy", "its accuracy not updated", "drawn"
			};

			said_reason = reason;
			said_weapon_index = weapon_index;
			error(_error_silent, "shadowrun: bloom reticle of unit %08lx, weapon %s: %s (flags %08lx, unitized %g, at %d,%d, colour %08lx)",
				(unsigned long)unit_index,
				weapon_index != NONE ? tag_name_strip_path(tag_get_name(object_get(weapon_index)->definition_index)) : "none",
				reasons[reason], data ? (unsigned long)data->flags : 0UL,
				shadowrun_unit ? (double)shadowrun_unit->accuracy_unitized : 0.,
				(int)x, (int)y, (unsigned long)color);
		}
	}

	if (unit && unit->object.type == _object_type_biped && shadowrun_unit->accuracy_active &&
		shadowrun_unit_definition_player(unit_index))
	{
		struct shadowrun_weapon_data *data = shadowrun_unit_weapon_data_get(unit_index);
		real gap = (1.f - PIN(shadowrun_unit->accuracy_unitized, 0.f, 1.f)) * SHADOWRUN_HUD_ACCURACY_SPREAD +
			(data ? data->accuracy_reticle_gap : 0.f);
		short near_gap = (short)gap;
		short far_gap = (short)(gap + SHADOWRUN_HUD_ACCURACY_LENGTH);
		rectangle2d line;

		/* port: a HUD colour's alpha is its transparency (0 is opaque: the
		crosshairs' are 0), as hud_draw.c reads a meter's; a quad's is its
		opacity */
		color = ((pixel32)(UNSIGNED_CHAR_MAX - (color >> 24)) << 24) | (color & 0x00FFFFFF);
		/* (the point is the pixel the crosshair's middle is in: its lines
		are drawn through it) */
		/* (right, left) */
		line.y0 = y;
		line.y1 = (short)(y + 1);
		line.x0 = (short)(x + near_gap);
		line.x1 = (short)(x + far_gap);
		draw_quad(&line, color);
		line.x0 = (short)(x - far_gap);
		line.x1 = (short)(x - near_gap);
		draw_quad(&line, color);
		/* (below, above) */
		line.x0 = x;
		line.x1 = (short)(x + 1);
		line.y0 = (short)(y + near_gap);
		line.y1 = (short)(y + far_gap);
		draw_quad(&line, color);
		line.y0 = (short)(y - far_gap);
		line.y1 = (short)(y - near_gap);
		draw_quad(&line, color);
	}

	return;
}

/* port: the crosshair's point and colour, as hud_weapon.c's crosshairs give
them (the aim crosshair's, each frame): the reticle is drawn with the rest
of this HUD (shadowrun_hud_draw, just after the weapon's HUD), where its
quads draw as the magic's cells do */
void shadowrun_hud_draw_accuracy(
	long unit_index,
	short x,
	short y,
	pixel32 color)
{
	short local_player_index = render.local_player_index;

	if (local_player_index >= 0 && local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS)
	{
		shadowrun_hud_reticles[local_player_index].unit_index = unit_index;
		shadowrun_hud_reticles[local_player_index].x = x;
		shadowrun_hud_reticles[local_player_index].y = y;
		shadowrun_hud_reticles[local_player_index].color = color;
		shadowrun_hud_reticles[local_player_index].given = TRUE;
	}

	return;
}
