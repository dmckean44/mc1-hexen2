/*
 * sys_mc1.c -- system layer for the MC1 computer and the MRISC32 simulator
 * Based on sys_unix.c and on mc1-quake's sys_mc1.c.
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

#ifdef MC1_SDK
#include <mc1/newlib_integ.h>
#endif

#include "quakedef.h"
#include "debuglog.h"
#include "mc1.h"

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#define HEAP_SIZE_MB	24

cvar_t		sys_nostdout = {"sys_nostdout", "0", CVAR_NONE};
cvar_t		sys_throttle = {"sys_throttle", "0", CVAR_ARCHIVE};

qboolean		isDedicated;
static quakeparms_t	parms;


/*
===============================================================================

FILE IO

===============================================================================
*/

int Sys_mkdir (const char *path, qboolean crash)
{
	int rc = mkdir (path, 0777);
	if (rc != 0 && errno == EEXIST)
		rc = 0;
	if (rc != 0 && crash)
		Sys_Error("Unable to create directory %s: %s", path, strerror(errno));
	return rc;
}

int Sys_rmdir (const char *path)
{
	return rmdir(path);
}

int Sys_unlink (const char *path)
{
	return unlink(path);
}

int Sys_rename (const char *oldp, const char *newp)
{
	return rename(oldp, newp);
}

long Sys_filesize (const char *path)
{
	struct stat	st;

	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return -1;
	return (long) st.st_size;
}

int Sys_FileType (const char *path)
{
	struct stat	st;

	if (stat(path, &st) != 0)
		return FS_ENT_NONE;
	if (S_ISDIR(st.st_mode))
		return FS_ENT_DIRECTORY;
	if (S_ISREG(st.st_mode))
		return FS_ENT_FILE;
	return FS_ENT_NONE;
}

int Sys_CopyFile (const char *frompath, const char *topath)
{
	char	buf[4096];
	FILE	*in, *out;
	size_t	n;
	int	err = 0;

	in = fopen(frompath, "rb");
	if (!in)
		return 1;
	out = fopen(topath, "wb");
	if (!out)
	{
		fclose(in);
		return 1;
	}
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
	{
		if (fwrite(buf, 1, n, out) != n)
		{
			err = 1;
			break;
		}
	}
	fclose(in);
	fclose(out);
	return err;
}

/*
=================================================
simplified findfirst/findnext implementation:
Sys_FindFirstFile and Sys_FindNextFile return
filenames only, not a dirent struct. this is
what we presently need in this engine.
=================================================
*/
static DIR		*finddir;
static char		*findpath, *findpattern;

/* '*' and '?' wildcards, which is all the engine asks for */
static qboolean wildcard_match (const char *pattern, const char *s)
{
	if (*pattern == '\0')
		return *s == '\0';
	if (*pattern == '*')
		return wildcard_match(pattern + 1, s) || (*s && wildcard_match(pattern, s + 1));
	if (*s && (*pattern == '?' || *pattern == *s))
		return wildcard_match(pattern + 1, s + 1);
	return false;
}

const char *Sys_FindFirstFile (const char *path, const char *pattern)
{
	if (finddir)
		Sys_Error ("Sys_FindFirst without FindClose");

	finddir = opendir (path);
	if (!finddir)
		return NULL;

	findpattern = Z_Strdup (pattern);
	findpath = Z_Strdup (path);

	return Sys_FindNextFile();
}

const char *Sys_FindNextFile (void)
{
	struct dirent	*finddata;
	char		file[MAX_OSPATH];

	if (!finddir)
		return NULL;

	while ((finddata = readdir(finddir)) != NULL)
	{
		if (!wildcard_match(findpattern, finddata->d_name))
			continue;
		q_snprintf (file, sizeof(file), "%s/%s", findpath, finddata->d_name);
		if (Sys_FileType(file) == FS_ENT_FILE)
			return finddata->d_name;
	}
	return NULL;
}

void Sys_FindClose (void)
{
	if (finddir != NULL)
		closedir(finddir);
	if (findpath != NULL)
		Z_Free (findpath);
	if (findpattern != NULL)
		Z_Free (findpattern);
	finddir = NULL;
	findpath = NULL;
	findpattern = NULL;
}


/*
===============================================================================

SYSTEM IO

===============================================================================
*/

void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[MAX_PRINTMSG];

	host_parms->errstate++;

	va_start (argptr, error);
	q_vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);

	if (con_debuglog)
	{
		LOG_Print ("\nFATAL ERROR: ");
		LOG_Print (text);
		LOG_Print ("\n\n");
	}

	Host_Shutdown ();

	fprintf (stderr, "\nFATAL ERROR: %s\n\n", text);
	exit (1);
}

void Sys_PrintTerm (const char *msgtxt)
{
	if (sys_nostdout.integer)
		return;
	fputs (msgtxt, stdout);
}

void Sys_Quit (void)
{
	Host_Shutdown();
	exit (0);
}

/* the CPU cycle counter, also in the simulator, where the clock is taken as 96 MHz */
double Sys_DoubleTime (void)
{
	static double	inv_clk;
	uint32_t	hi_old, hi, lo;

	if (inv_clk == 0.0)
	{
#ifdef MC1_SDK
		inv_clk = 1.0 / (double)GET_MMIO (CPUCLK);
#else
		inv_clk = 1.0 / 96e6;
#endif
	}
	hi = GET_MMIO (CLKCNTHI);
	do
	{
		hi_old = hi;
		lo = GET_MMIO (CLKCNTLO);
		hi = GET_MMIO (CLKCNTHI);
	} while (hi != hi_old);
	return inv_clk * (double)((((uint64_t)hi) << 32) | lo);
}

char *Sys_DateTimeString (char *buf)
{
	static char strbuf[24];

	if (!buf) buf = strbuf;
	q_strlcpy (buf, "01/01/1997 00:00:00", 20);
	return buf;
}

const char *Sys_ConsoleInput (void)
{
	return NULL;
}

void Sys_Sleep (unsigned long msecs)
{
}

static int Sys_TranslateKey (unsigned keycode)
{
	switch (keycode)
	{
	case KB_SPACE:		return K_SPACE;
	case KB_LEFT:		return K_LEFTARROW;
	case KB_RIGHT:		return K_RIGHTARROW;
	case KB_DOWN:		return K_DOWNARROW;
	case KB_UP:		return K_UPARROW;
	case KB_ESC:		return K_ESCAPE;
	case KB_ENTER:
	case KB_KP_ENTER:	return K_ENTER;
	case KB_TAB:		return K_TAB;
	case KB_F1:		return K_F1;
	case KB_F2:		return K_F2;
	case KB_F3:		return K_F3;
	case KB_F4:		return K_F4;
	case KB_F5:		return K_F5;
	case KB_F6:		return K_F6;
	case KB_F7:		return K_F7;
	case KB_F8:		return K_F8;
	case KB_F9:		return K_F9;
	case KB_F10:		return K_F10;
	case KB_F11:		return K_F11;
	case KB_F12:		return K_F12;
	case KB_DEL:		return K_DEL;
	case KB_BACKSPACE:	return K_BACKSPACE;
	case KB_INSERT:		return K_INS;
	case KB_HOME:		return K_HOME;
	case KB_END:		return K_END;
	case KB_PGUP:		return K_PGUP;
	case KB_PGDN:		return K_PGDN;
	case KB_MM_PLAY_PAUSE:	return K_PAUSE;
	case KB_LSHIFT:
	case KB_RSHIFT:		return K_SHIFT;
	case KB_LCTRL:
	case KB_RCTRL:		return K_CTRL;
	case KB_LALT:
	case KB_LMETA:
	case KB_RALT:
	case KB_RMETA:		return K_ALT;
	case KB_KP_PLUS:	return '+';
	case KB_KP_MINUS:	return '-';
	case KB_A:		return 'a';
	case KB_B:		return 'b';
	case KB_C:		return 'c';
	case KB_D:		return 'd';
	case KB_E:		return 'e';
	case KB_F:		return 'f';
	case KB_G:		return 'g';
	case KB_H:		return 'h';
	case KB_I:		return 'i';
	case KB_J:		return 'j';
	case KB_K:		return 'k';
	case KB_L:		return 'l';
	case KB_M:		return 'm';
	case KB_N:		return 'n';
	case KB_O:		return 'o';
	case KB_P:		return 'p';
	case KB_Q:		return 'q';
	case KB_R:		return 'r';
	case KB_S:		return 's';
	case KB_T:		return 't';
	case KB_U:		return 'u';
	case KB_V:		return 'v';
	case KB_W:		return 'w';
	case KB_X:		return 'x';
	case KB_Y:		return 'y';
	case KB_Z:		return 'z';
	case KB_0:		return '0';
	case KB_1:		return '1';
	case KB_2:		return '2';
	case KB_3:		return '3';
	case KB_4:		return '4';
	case KB_5:		return '5';
	case KB_6:		return '6';
	case KB_7:		return '7';
	case KB_8:		return '8';
	case KB_9:		return '9';
	default:		return 0;
	}
}

static unsigned	keyptr;

void Sys_SendKeyEvents (void)
{
	unsigned	keycode;
	int		key;

	while (keyptr != GET_MMIO (KEYPTR))
	{
		++keyptr;
		keycode = GET_KEYBUF (keyptr % KEYBUF_SIZE);
		key = Sys_TranslateKey (keycode & 0x1ff);
		if (key != 0)
			Key_Event (key, (keycode & 0x80000000) ? true : false);
	}
}

char *Sys_GetClipboardData (void)
{
	return NULL;
}


int main (int argc, char **argv)
{
	int		i;
	double		time, oldtime, newtime;

#ifdef MC1_SDK
	mc1newlib_init (MC1NEWLIB_ALL & ~MC1NEWLIB_CONSOLE);
#endif

	memset (&parms, 0, sizeof(parms));
	parms.basedir = ".";
	parms.userdir = ".";
	parms.argc = argc;
	parms.argv = argv;
	host_parms = &parms;

	LOG_Init (&parms);

	COM_ValidateByteorder ();

	parms.memsize = HEAP_SIZE_MB * 1024 * 1024;
	i = COM_CheckParm ("-heapsize");
	if (i && i < com_argc-1)
		parms.memsize = atoi (com_argv[i+1]) * 1024;

	parms.membase = malloc (parms.memsize);
	if (!parms.membase)
		Sys_Error ("Insufficient memory.");

	keyptr = GET_MMIO (KEYPTR);

	Host_Init();

	oldtime = Sys_DoubleTime ();
	while (1)
	{
		newtime = Sys_DoubleTime ();
		time = newtime - oldtime;
		Host_Frame (time);
		oldtime = newtime;
	}

	return 0;
}
