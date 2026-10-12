/* d_pe.c -- MC1 pixel engine driver (see d_pe.h) */

#include "quakedef.h"
#include "d_local.h"
#include "d_pe.h"

qboolean	pe_present;
qboolean	pe_active;
qboolean	pe_alias;
unsigned	pe_submitted;
unsigned	pe_synced;
unsigned	pe_spans_fence;

#if !defined(MC1)
volatile unsigned	pe_noreg;
#endif

static cvar_t	r_pe = {"r_pe", "1", CVAR_NONE};
static const byte	*pe_skin;
static int	pe_skinwidth;

void PE_Init (void)
{
	Cvar_RegisterVariable (&r_pe);
#if defined(MC1)
	pe_present = (PE_REG(PE_ID) >> 16) == PE_ID_MAGIC;
#endif
	if (pe_present)
		Con_Printf ("Pixel engine version %d found.\n", PE_REG(PE_ID) & 0xffff);
}

void PE_BeginFrame (void)
{
	PE_Finish ();
	pe_active = pe_present && r_pe.integer;
	pe_alias = false;
	if (!pe_active)
		return;

	PE_Begin (PE_TARGET, 5);
	PE_Ptr (d_viewbuffer);
	PE_Word (screenwidth);
	PE_Ptr (d_pzbuffer);
	PE_Ptr (vid.colormap);
	PE_Ptr (transTable);
	pe_skin = NULL;
}

void PE_SetSkin (const byte *skin, int skinwidth)
{
	if (skin == pe_skin && skinwidth == pe_skinwidth)
		return;
	pe_skin = skin;
	pe_skinwidth = skinwidth;
	PE_Begin (PE_SKIN, 2);
	PE_Ptr (skin);
	PE_Word (skinwidth);
}

/* one surface's spans with the gradients D_CalcGradients left behind */
void PE_DrawSpans (espan_t *spans, int flags, const int *turb,
		float zorigin, float zstepu, float zstepv)
{
	pe_spans_fence = PE_Begin (PE_SPANS, 21);
	PE_Word (flags);
	PE_Ptr (spans);
	PE_Ptr (cacheblock);
	PE_Word (cachewidth);
	PE_Word (sadjust);
	PE_Word (tadjust);
	PE_Word (bbextents);
	PE_Word (bbextentt);
	PE_Float (d_sdivzorigin);
	PE_Float (d_sdivzstepu);
	PE_Float (d_sdivzstepv);
	PE_Float (d_tdivzorigin);
	PE_Float (d_tdivzstepu);
	PE_Float (d_tdivzstepv);
	PE_Float (d_ziorigin);
	PE_Float (d_zistepu);
	PE_Float (d_zistepv);
	PE_Float (zorigin);
	PE_Float (zstepu);
	PE_Float (zstepv);
	PE_Ptr (turb);
}

void PE_Triangle (const int *p0, const int *p1, const int *p2)
{
	int	i;

	PE_Begin (PE_TRIANGLE, 18);
	for (i = 0; i < 6; i++)
		PE_Word (p0[i]);
	for (i = 0; i < 6; i++)
		PE_Word (p1[i]);
	for (i = 0; i < 6; i++)
		PE_Word (p2[i]);
}
