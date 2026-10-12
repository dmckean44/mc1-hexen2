/*
 * vid_mc1.c -- video driver for the MC1 computer
 * Based on mc1-quake's vid_mc1.c and on vid_sdl.c.
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
#include "d_local.h"
#include "mc1.h"

/* -DBASEWIDTH=640 for 640x240 (pixels twice as tall as wide) */
#ifndef BASEWIDTH
#define BASEWIDTH	320
#endif
#ifndef BASEHEIGHT
#define BASEHEIGHT	240
#endif

unsigned short	d_8to16table[256];
unsigned int	d_8to24table[256];

byte globalcolormap[VID_GRADES*256], lastglobalcolor = 0;
byte *lastsourcecolormap = NULL;

qboolean	in_mode_set;
modestate_t	modestate = MS_FULLDIB;
viddef_t	vid;

cvar_t		_enable_mouse = {"_enable_mouse", "1", CVAR_ARCHIVE};
static cvar_t	vid_dumpframes = {"vid_dumpframes", "0", CVAR_NONE};

/* two pages, each with its own VCP; we draw into one while the other is shown */
static byte	*vcp[2];
static byte	*palette_vcp;
static byte	*framebuffer[2];
static int	back_page;
static unsigned	frame_count;
static unsigned	frames_dumped;
static unsigned char	curpal[256*3];

/* VRAM from 0x40002000 up belongs to us (below are MC1's video registers) */
static byte	*vram_ptr = (byte *)0x40002000;

static byte *VRAM_Alloc (size_t bytes)
{
	byte	*ptr = vram_ptr;
	byte	*end = (byte *)(0x40000000 + GET_MMIO (VRAMSIZE));

	if (ptr + bytes > end)
		return NULL;
	vram_ptr = (byte *)(((size_t)(ptr + bytes) + 31) & ~(size_t)31);
	return ptr;
}

static void VID_CreateVCP (int page)
{
	unsigned	native_width = GET_MMIO (VIDWIDTH);
	unsigned	native_height = GET_MMIO (VIDHEIGHT);
	unsigned	xincr = ((BASEWIDTH - 1) << 16) / (native_width - 1);
	unsigned	yincr = ((native_height - 1) << 16) / (BASEHEIGHT - 1);
	unsigned	*p = (unsigned *)vcp[page];
	unsigned	y = 0;
	unsigned	addr = (unsigned)framebuffer[page];
	int		i;

	*p++ = VCP_SETREG (VCR_XINCR, xincr);
	*p++ = VCP_SETREG (VCR_CMODE, CMODE_PAL8);
	*p++ = VCP_JSR (palette_vcp);
	*p++ = VCP_SETREG (VCR_HSTOP, native_width);
	for (i = 0; i < BASEHEIGHT; ++i)
	{
		*p++ = VCP_WAITY (y >> 16);
		*p++ = VCP_SETREG (VCR_ADDR, VCP_TOVCPADDR (addr));
		addr += BASEWIDTH;
		y += yincr;
	}
	*p = VCP_WAITY (32767);
}

static void VID_ShowPage (int page)
{
	*((unsigned *)0x40000010) = VCP_JMP (vcp[page]);
}

void VID_SetPalette (const unsigned char *palette)
{
	unsigned	*dst = &((unsigned *)palette_vcp)[1];
	int		i;

	if (palette != curpal)
		memcpy (curpal, palette, sizeof(curpal));
	for (i = 0; i < 256; ++i)
	{
		unsigned r = palette[i * 3];
		unsigned g = palette[i * 3 + 1];
		unsigned b = palette[i * 3 + 2];
		dst[i] = (255u << 24) | (b << 16) | (g << 8) | r;
	}
}

void VID_ShiftPalette (const unsigned char *palette)
{
	VID_SetPalette (palette);
}

void VID_Init (const unsigned char *palette)
{
	int	surfcache_size;

	Cvar_RegisterVariable (&_enable_mouse);
	Cvar_RegisterVariable (&vid_dumpframes);

	vcp[0] = VRAM_Alloc ((5 + BASEHEIGHT * 2) * sizeof(unsigned));
	vcp[1] = VRAM_Alloc ((5 + BASEHEIGHT * 2) * sizeof(unsigned));
	palette_vcp = VRAM_Alloc ((2 + 256) * sizeof(unsigned));
	framebuffer[0] = VRAM_Alloc (BASEWIDTH * BASEHEIGHT);
	framebuffer[1] = VRAM_Alloc (BASEWIDTH * BASEHEIGHT);
	if (!framebuffer[1])
		Sys_Error ("Not enough VRAM");
	((unsigned *)palette_vcp)[0] = VCP_SETPAL (0, 256);
	((unsigned *)palette_vcp)[257] = VCP_RTS;
	VID_CreateVCP (0);
	VID_CreateVCP (1);
	VID_ShowPage (0);
	back_page = 1;
	/* layer 2 does nothing */
	*((unsigned *)0x40000020) = VCP_WAITY (32767);

	vid.width = vid.conwidth = BASEWIDTH;
	vid.height = vid.conheight = BASEHEIGHT;
	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
	vid.numpages = 2;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
	vid.buffer = vid.conbuffer = vid.direct = framebuffer[back_page];
	vid.rowbytes = vid.conrowbytes = BASEWIDTH;

	surfcache_size = D_SurfaceCacheForRes (BASEWIDTH, BASEHEIGHT);
	d_pzbuffer = (short *) VRAM_Alloc (BASEWIDTH * BASEHEIGHT * sizeof(*d_pzbuffer));
	if (!d_pzbuffer)
		d_pzbuffer = (short *) Hunk_HighAllocName (BASEWIDTH * BASEHEIGHT * sizeof(*d_pzbuffer), "zbuffer");
	D_InitCaches (Hunk_HighAllocName (surfcache_size, "surfcache"), surfcache_size);

	VID_SetPalette (palette);
	vid.recalc_refdef = 1;
}

void VID_Shutdown (void)
{
}

/* frames are dumped as 320x240 8-bit pixels followed by the 768-byte palette */
static void VID_DumpFrame (void)
{
	char	name[MAX_OSPATH];
	FILE	*f;

	q_snprintf (name, sizeof(name), "%s/frames/f%05u.raw", host_parms->userdir, frame_count);
	f = fopen (name, "wb");
	if (!f)
	{
		Sys_mkdir (va("%s/frames", host_parms->userdir), false);
		f = fopen (name, "wb");
	}
	if (!f)
		return;
	fwrite (vid.buffer, 1, BASEWIDTH * BASEHEIGHT, f);
	fwrite (curpal, 1, sizeof(curpal), f);
	fclose (f);
	frames_dumped++;
}

void VID_Update (vrect_t *rects)
{
	frame_count++;
	if (frames_dumped < (unsigned)vid_dumpframes.integer)
		VID_DumpFrame ();

	VID_ShowPage (back_page);
	back_page ^= 1;
	vid.buffer = vid.conbuffer = vid.direct = framebuffer[back_page];
}

void VID_LockBuffer (void)
{
}

void VID_UnlockBuffer (void)
{
}

void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}

void D_EndDirectRect (int x, int y, int width, int height)
{
}

void D_ShowLoadingSize (void)
{
}

void VID_HandlePause (qboolean paused)
{
}

void VID_ToggleFullscreen (void)
{
}

qboolean VID_HasMouseOrInputFocus (void)
{
	return true;
}

qboolean VID_IsMinimized (void)
{
	return false;
}
