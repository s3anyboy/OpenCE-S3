/*
SHADOWRUN_HUD.C

The Shadowrun prototype's magic on the HUD (shadowrun.h), at the bottom of
the screen: the spell the right bumper casts (or Grenade, when the left
bumper has chosen the grenades) and the unit's magic, a cell a point: lit what it has, dim what it has
not, and dark red what its spells hold back. The numbers are the
executable's own ("%d/%d"). A spell that could not be cast makes the line
flash red for a second; one waiting to be cast again is grey.

The executable drew its own HUD of slots and icons, assigned from a radial
menu (its globals' assignment scheme), with bitmaps of its globals and the
srud's: this is a plainer one, of the same facts.
*/

/* ---------- headers */

#include "cseries.h"
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
	/* draw_string.c's left-justified text (its enum is its own) */
	_shadowrun_hud_text_left = 0,
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

/* ---------- globals */

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

	if (!shadowrun_unit || !shadowrun_unit_definition_get(unit_index) || cinematic_in_progress() ||
		local_player_index < 0 || local_player_index >= MAXIMUM_NUMBER_OF_LOCAL_PLAYERS)
	{
		return;
	}
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
	{
		return;
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
		snprintf(text, sizeof(text), "Grenade");
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

	/* the buy menu, if it is open (shadowrun_buy_menu.c) */
	shadowrun_buy_menu_draw(local_player_index);

	return;
}
