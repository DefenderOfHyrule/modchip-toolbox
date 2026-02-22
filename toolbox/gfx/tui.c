/*
 * Copyright (c) 2018 naehrwert
 * Copyright (c) 2018 CTCaer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <bdk.h>

#include "tui.h"
#include "../config.h"
#include <input/joycon.h>
#include <string.h>

extern hekate_config h_cfg;

/*
 * replacement for btn_wait() that also reads joycon input.
 * maps joycon buttons onto the existing BTN_* instances so all menu logic
 * needs no changes.
 *
 * mapping:
 *   D-pad up / left / ZL  -> BTN_VOL_UP
 *   D-pad down / right / ZR -> BTN_VOL_DOWN
 *   A / B / + / -           -> BTN_POWER
 */
u8 btn_wait_with_jc()
{
	// current hardware button state to detect release.
	u8 prev_hw = btn_read();

	u8 hw;
	bool pwr_held = (prev_hw & BTN_POWER) != 0;

	for (u32 i = 0; i < 8; i++)
	{
		joycon_poll();
		msleep(20);
	}

	do
	{
		hw = btn_read();
		if (!(hw & BTN_POWER) && pwr_held)
			pwr_held = false;
		else if (pwr_held)
			hw &= ~BTN_POWER;

		jc_gamepad_rpt_t *jc = joycon_poll();
		if (jc)
		{
			u8 jc_btns = 0;

			// D-pad up/left/ZL -> VOL_UP
			if (jc->up || jc->left || jc->zl)
				jc_btns |= BTN_VOL_UP;

			// D-pad down/right/ZR -> VOL_DOWN
			if (jc->down || jc->right || jc->zr)
				jc_btns |= BTN_VOL_DOWN;

			// A / B / + / - -> POWER (select)
			if (jc->a || jc->b || jc->plus || jc->minus)
				jc_btns |= BTN_POWER;

			if (jc_btns)
			{
				// wait for release before returning.
				while (true)
				{
					msleep(20);
					jc = joycon_poll();
					if (!jc)
						break;
					bool still_held = false;
					if ((jc_btns & BTN_VOL_UP)   && (jc->up || jc->left || jc->zl)) still_held = true;
					if ((jc_btns & BTN_VOL_DOWN) && (jc->down || jc->right || jc->zr)) still_held = true;
					if ((jc_btns & BTN_POWER)    && (jc->a || jc->b || jc->plus || jc->minus)) still_held = true;
					if (!still_held)
						break;
				}
				return jc_btns;
			}
		}

		msleep(10);
	} while (prev_hw == hw);

	return hw;
}

void tui_sbar(bool force_update)
{
	u32 cx, cy;
	static u32 sbar_time_keeping = 0;

	u32 timePassed = get_tmr_s() - sbar_time_keeping;
	if (!force_update)
		if (timePassed < 5)
			return;

	u8 prevFontSize = gfx_con.fntsz;
	gfx_con.fntsz = 16;
	sbar_time_keeping = get_tmr_s();

	u32 battPercent = 0;
	int battVoltCurr = 0;

	gfx_con_getpos(&cx, &cy);
	gfx_con_setpos(0, 704);

	max17050_get_property(MAX17050_RepSOC, (int *)&battPercent);
	max17050_get_property(MAX17050_VCELL, &battVoltCurr);

	gfx_clear_partial_grey(0x30, 704, 16);
	gfx_printf("%K%k Battery: %d.%d%% (%d mV) - Charge:", 0xFF303030, 0xFF888888,
		(battPercent >> 8) & 0xFF, (battPercent & 0xFF) / 26, battVoltCurr);

	max17050_get_property(MAX17050_Current, &battVoltCurr);

	if (battVoltCurr >= 0)
		gfx_printf(" %k+%d mA%k%K\n",
			0xFF008800, battVoltCurr / 1000, 0xFFCCCCCC, 0xFF1B1B1B);
	else
		gfx_printf(" %k-%d mA%k%K\n",
			0xFF880000, (~battVoltCurr) / 1000, 0xFFCCCCCC, 0xFF1B1B1B);
	gfx_con.fntsz = prevFontSize;
	gfx_con_setpos(cx, cy);
}

void tui_pbar(int x, int y, u32 val, u32 fgcol, u32 bgcol)
{
	u32 cx, cy;
	if (val > 200)
		val = 200;

	gfx_con_getpos(&cx, &cy);

	gfx_con_setpos(x, y);

	gfx_printf("%k[%3d%%]%k", fgcol, val, 0xFFCCCCCC);

	x += 7 * gfx_con.fntsz;

	for (u32 i = 0; i < (gfx_con.fntsz >> 3) * 6; i++)
	{
		gfx_line(x, y + i + 1, x + 3 * val, y + i + 1, fgcol);
		gfx_line(x + 3 * val, y + i + 1, x + 3 * 100, y + i + 1, bgcol);
	}

	gfx_con_setpos(cx, cy);

	// Update status bar.
	tui_sbar(false);
}

void *tui_do_menu(menu_t *menu)
{
	int idx = 0, prev_idx = 0, cnt = 0x7FFFFFFF;

	gfx_clear_partial_grey(0x1B, 0, 704);
	tui_sbar(true);

	while (true)
	{
		gfx_con_setcol(0xFFCCCCCC, 1, 0xFF1B1B1B);
		gfx_con_setpos(menu->x, menu->y);
		gfx_printf("[%s]\n\n", menu->caption);

		// Skip caption or seperator lines selection.
		while (menu->ents[idx].type == MENT_CAPTION ||
			menu->ents[idx].type == MENT_CHGLINE)
		{
			if (prev_idx <= idx || (!idx && prev_idx == cnt - 1))
			{
				idx++;
				if (idx > (cnt - 1))
				{
					idx = 0;
					prev_idx = 0;
				}
			}
			else
			{
				idx--;
				if (idx < 0)
				{
					idx = cnt - 1;
					prev_idx = cnt;
				}
			}
		}
		prev_idx = idx;

		// Draw the menu.
		for (cnt = 0; menu->ents[cnt].type != MENT_END; cnt++)
		{
			if (cnt == idx)
				gfx_con_setcol(0xFF1B1B1B, 1, 0xFFCCCCCC);
			else
				gfx_con_setcol(0xFFCCCCCC, 1, 0xFF1B1B1B);
			if (menu->ents[cnt].type == MENT_CAPTION)
				gfx_printf("%k %s", menu->ents[cnt].color, menu->ents[cnt].caption);
			else if (menu->ents[cnt].type != MENT_CHGLINE)
				gfx_printf(" %s", menu->ents[cnt].caption);
			if(menu->ents[cnt].type == MENT_MENU)
				gfx_printf("%k...", 0xFF0099EE);
			gfx_printf(" \n");
		}
		gfx_con_setcol(0xFFCCCCCC, 1, 0xFF1B1B1B);
		gfx_putc('\n');

		// Indicate that functionality is only available if modchip is powered on
		if (!strcmp(menu->caption, "Modchip Toolbox " TOOLBOX_VERSION "] - [ BDK 6.5.1 "))
		{
			gfx_con_setpos(0, 205);
			gfx_printf("  %kFor HWFLY modchips, ensure the modchip\n", 0xFF5E95BC);
			gfx_printf("  is awake:\n", 0xFF5E95BC);
			gfx_printf("  \n");
			gfx_printf("  Hold %kVOL+%k during power on.\n", 0xFFC0C0C0, 0xFF5E95BC);
			gfx_printf("  The %kgreen%k LED should be on and static.\n", 0xFF00FF00, 0xFF5E95BC);
		}

		// print errors, help and battery status.
		gfx_con_setpos(0, 672);
		gfx_printf("%k VOL or DPAD Up/Down: Move up/down\n PWR or A/B: Select option%k", 0xFF555555, 0xFFCCCCCC);

		display_backlight_brightness(h_cfg.backlight, 1000);

		// Wait for user command.
		u32 btn = btn_wait_with_jc();

		if (btn & BTN_VOL_DOWN && idx < (cnt - 1))
			idx++;
		else if (btn & BTN_VOL_DOWN && idx == (cnt - 1))
		{
			idx = 0;
			prev_idx = -1;
		}
		if (btn & BTN_VOL_UP && idx > 0)
			idx--;
		else if (btn & BTN_VOL_UP && idx == 0)
		{
			idx = cnt - 1;
			prev_idx = cnt;
		}
		if (btn & BTN_POWER)
		{
			ment_t *ent = &menu->ents[idx];
			switch (ent->type)
			{
			case MENT_HANDLER:
				ent->handler(ent->data);
				break;
			case MENT_MENU:
				return tui_do_menu(ent->menu);
				break;
			case MENT_DATA:
				return ent->data;
				break;
			case MENT_BACK:
				return NULL;
				break;
			case MENT_HDLR_RE:
				ent->handler(ent);
				if (!ent->data)
					return NULL;
				break;
			default:
				break;
			}
			gfx_con.fntsz = 16;
			gfx_clear_partial_grey(0x1B, 0, 704);
		}
		tui_sbar(false);
	}

	return NULL;
}
