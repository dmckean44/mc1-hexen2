/*
 * in_mc1.c -- mouse input for the MC1 computer
 * Based on mc1-quake's in_mc1.c and on in_sdl.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 */

#include "quakedef.h"
#include "mc1.h"

static short	old_mouse_x, old_mouse_y;
static unsigned	old_mouse_buttons;

void IN_Init (void)
{
	unsigned pos = GET_MMIO (MOUSEPOS);

	old_mouse_x = (short)pos;
	old_mouse_y = (short)(pos >> 16);
	old_mouse_buttons = GET_MMIO (MOUSEBTNS);
}

void IN_ReInit (void)
{
}

void IN_Shutdown (void)
{
}

void IN_ShowMouse (void)
{
}

void IN_HideMouse (void)
{
}

void IN_ActivateMouse (void)
{
}

void IN_DeactivateMouse (void)
{
}

void IN_ClearStates (void)
{
}

void IN_SendKeyEvents (void)
{
	Sys_SendKeyEvents ();
}

void IN_Commands (void)
{
	unsigned	buttons = GET_MMIO (MOUSEBTNS);
	int		i;

	for (i = 0; i < 3; ++i)
	{
		unsigned bit = 1u << i;
		if ((buttons ^ old_mouse_buttons) & bit)
			Key_Event (K_MOUSE1 + i, (buttons & bit) ? true : false);
	}
	old_mouse_buttons = buttons;
}

void IN_Move (usercmd_t *cmd)
{
	unsigned	pos = GET_MMIO (MOUSEPOS);
	short		x = (short)pos, y = (short)(pos >> 16);
	float		mouse_x = (float)(x - old_mouse_x) * sensitivity.value;
	float		mouse_y = (float)(y - old_mouse_y) * sensitivity.value;

	old_mouse_x = x;
	old_mouse_y = y;
	if (mouse_x == 0 && mouse_y == 0)
		return;

	if ((in_strafe.state & 1) || (lookstrafe.integer && (in_mlook.state & 1)))
		cmd->sidemove += m_side.value * mouse_x;
	else
		cl.viewangles[YAW] -= m_yaw.value * mouse_x;

	if (in_mlook.state & 1)
		V_StopPitchDrift ();

	if ((in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * mouse_y;
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else if ((in_strafe.state & 1) && cl.v.movetype == MOVETYPE_NOCLIP)
		cmd->upmove -= m_forward.value * mouse_y;
	else
		cmd->forwardmove -= m_forward.value * mouse_y;
}
