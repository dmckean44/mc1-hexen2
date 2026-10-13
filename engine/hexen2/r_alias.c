/* r_alias.c -- routines for setting up to draw alias models
 * Copyright (C) 1996-1997  Id Software, Inc.
 * Copyright (C) 1997-1998  Raven Software Corp.
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "quakedef.h"
#include "r_local.h"
#include "d_pe.h"

static int		pe_mode;
static const byte	*pe_table;
static qboolean	pe_mesh;
#include "d_local.h"	// FIXME: shouldn't be needed (is needed for patch
			//	  right now, but that should move)

#define LIGHT_MIN	5	// lowest light value we'll allow, to avoid the
				// need for inner-loop light clamping

mtriangle_t		*ptriangles;
affinetridesc_t		r_affinetridesc;

void			*acolormap;	// FIXME: should go away

ASM_LINKAGE_BEGIN
trivertx_t		*r_apverts;

// TODO: these probably will go away with optimized rasterization
vec3_t			r_plightvec;
int			r_ambientlight;
float			r_shadelight;
ASM_LINKAGE_END

aliashdr_t		*paliashdr;
finalvert_t		*pfinalverts;
auxvert_t		*pauxverts;

newmdl_t		*pmdl;		//JFM: new model fmt

static qmodel_t		*pmodel;
static float		ziscale;

static vec3_t		alias_forward, alias_right, alias_up;

static maliasskindesc_t	*pskindesc;

int			r_amodels_drawn;
ASM_LINKAGE_BEGIN
int			r_anumverts;

float			aliastransform[3][4];

const float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};
ASM_LINKAGE_END

typedef struct {
	int	index0;
	int	index1;
} aedge_t;

static aedge_t	aedges[12] =
{
	{0, 1}, {1, 2}, {2, 3}, {3, 0},
	{4, 5}, {5, 6}, {6, 7}, {7, 4},
	{0, 5}, {1, 4}, {2, 7}, {3, 6}
};


#if !id386
static void R_AliasTransformAndProjectFinalVerts (finalvert_t *fv, stvert_t *pstverts);
#endif
static void R_AliasSetUpTransform (int trivial_accept);
/* set by R_AliasCheckBBox: the model may use the engine's guard band */
cvar_t	r_pepartial = {"r_pepartial", "1", CVAR_NONE};	/* see R_AliasPreparePartial */
static qboolean	r_alias_zonly;
static void PE_AliasVertsM (finalvert_t *fv, const trivertx_t *pverts, int numverts, float m[3][4]);
static void R_AliasTransformVector (vec3_t in, vec3_t out);
static void R_AliasTransformFinalVert (finalvert_t *fv, auxvert_t *av, trivertx_t *pverts);


/*
================
R_AliasCheckBBox
================
*/
qboolean R_AliasCheckBBox (void)
{
	int			i, flags, frame, numv;
	aliashdr_t		*pahdr;
	float			zi, basepts[8][3], v0, v1, frac;
	finalvert_t		*pv0, *pv1, viewpts[16];
	auxvert_t		*pa0, *pa1, viewaux[16];
	maliasframedesc_t	*pframedesc;
	qboolean		zclipped, zfullyclipped;
	unsigned int		anyclip, allclip, guardclip;
	qboolean		guard;
	int			minz;

// expand, rotate, and translate points into worldspace

	currententity->trivial_accept = 0;
	pmodel = currententity->model;
	pahdr = (aliashdr_t *) Mod_Extradata (pmodel);
	pmdl = (newmdl_t*)((byte *)pahdr + pahdr->model);

	R_AliasSetUpTransform (0);

// construct the base bounding box for this frame
	frame = currententity->frame;
// TODO: don't repeat this check when drawing?
	if ((frame >= pmdl->numframes) || (frame < 0))
	{
		Con_DPrintf ("No such frame %d %s\n", frame, pmodel->name);
		frame = 0;
	}

	pframedesc = &pahdr->frames[frame];

// x worldspace coordinates
	basepts[0][0] =
		basepts[1][0] =
		basepts[2][0] =
		basepts[3][0] =
			(float)pframedesc->bboxmin.v[0];
	basepts[4][0] =
		basepts[5][0] =
		basepts[6][0] =
		basepts[7][0] =
			(float)pframedesc->bboxmax.v[0];

// y worldspace coordinates
	basepts[0][1] =
		basepts[3][1] =
		basepts[5][1] =
		basepts[6][1] =
			(float)pframedesc->bboxmin.v[1];
	basepts[1][1] =
		basepts[2][1] =
		basepts[4][1] =
		basepts[7][1] =
			(float)pframedesc->bboxmax.v[1];

// z worldspace coordinates
	basepts[0][2] =
		basepts[1][2] =
		basepts[4][2] =
		basepts[5][2] =
			(float)pframedesc->bboxmin.v[2];
	basepts[2][2] =
		basepts[3][2] =
		basepts[6][2] =
		basepts[7][2] =
			(float)pframedesc->bboxmax.v[2];

	zclipped = false;
	zfullyclipped = true;

	minz = 9999;
	for (i = 0 ; i < 8 ; i++)
	{
		R_AliasTransformVector (&basepts[i][0], &viewaux[i].fv[0]);

		if (viewaux[i].fv[2] < ALIAS_Z_CLIP_PLANE)
		{
		// we must clip points that are closer than the near clip plane
			viewpts[i].flags = ALIAS_Z_CLIP;
			zclipped = true;
		}
		else
		{
			if (viewaux[i].fv[2] < minz)
				minz = viewaux[i].fv[2];
			viewpts[i].flags = 0;
			zfullyclipped = false;
		}
	}

	if (minz < r_aliasmip.integer || zclipped)
		pmodel->flags &= ~EF_MIP_MAP_FAR;
	else
		pmodel->flags |=  EF_MIP_MAP_FAR;

	if (zfullyclipped)
	{
		return false;	// everything was near-z-clipped
	}

	numv = 8;

	if (zclipped)
	{
	// organize points by edges, use edges to get new points
	// (possible trivial reject)
		for (i = 0 ; i < 12 ; i++)
		{
		// edge endpoints
			pv0 = &viewpts[aedges[i].index0];
			pv1 = &viewpts[aedges[i].index1];
			pa0 = &viewaux[aedges[i].index0];
			pa1 = &viewaux[aedges[i].index1];

		// if one end is clipped and the other isn't, make a new point
			if (pv0->flags ^ pv1->flags)
			{
				frac = (ALIAS_Z_CLIP_PLANE - pa0->fv[2]) / (pa1->fv[2] - pa0->fv[2]);
				viewaux[numv].fv[0] = pa0->fv[0] + (pa1->fv[0] - pa0->fv[0]) * frac;
				viewaux[numv].fv[1] = pa0->fv[1] + (pa1->fv[1] - pa0->fv[1]) * frac;
				viewaux[numv].fv[2] = ALIAS_Z_CLIP_PLANE;
				viewpts[numv].flags = 0;
				numv++;
			}
		}
	}

// project the vertices that remain after clipping
	anyclip = 0;
	allclip = ALIAS_XY_CLIP_MASK;
	guardclip = 0;
	/* the engine clips to the screen: in its guard band, models that only
	   leave the screen count as unclipped */
	guard = pe_guard_models && currententity->colormap == vid.colormap;

// TODO: probably should do this loop in ASM, especially if we use floats
	for (i = 0 ; i < numv ; i++)
	{
	// we don't need to bother with vertices that were z-clipped
		if (viewpts[i].flags & ALIAS_Z_CLIP)
			continue;

		zi = 1.0 / viewaux[i].fv[2];

	// FIXME: do with chop mode in ASM, or convert to float
		v0 = (viewaux[i].fv[0] * xscale * zi) + xcenter;
		v1 = (viewaux[i].fv[1] * yscale * zi) + ycenter;

		flags = 0;

		if (v0 < r_refdef.fvrectx)
			flags |= ALIAS_LEFT_CLIP;
		if (v1 < r_refdef.fvrecty)
			flags |= ALIAS_TOP_CLIP;
		if (v0 > r_refdef.fvrectright)
			flags |= ALIAS_RIGHT_CLIP;
		if (v1 > r_refdef.fvrectbottom)
			flags |= ALIAS_BOTTOM_CLIP;

		anyclip |= flags;
		allclip &= flags;

		if (guard && (v0 < xcenter - PE_GUARD * (xcenter - r_refdef.fvrectx) ||
				v0 > xcenter + PE_GUARD * (r_refdef.fvrectright - xcenter) ||
				v1 < ycenter - PE_GUARD * (ycenter - r_refdef.fvrecty) ||
				v1 > ycenter + PE_GUARD * (r_refdef.fvrectbottom - ycenter)))
			guardclip = 1;
	}

	if (allclip)
		return false;	// trivial reject off one side

	currententity->trivial_accept = !(guard ? guardclip : anyclip) & !zclipped;
	r_alias_zonly = guard;	/* R_AliasPreparePartial takes it from here */

	if (currententity->trivial_accept)
	{
		if (minz > (r_aliastransition + (pmdl->size * r_resfudge)))
		{
			currententity->trivial_accept |= 2;
		}
	}

	return true;
}


/*
================
R_AliasTransformVector
================
*/
static void R_AliasTransformVector (vec3_t in, vec3_t out)
{
	out[0] = DotProduct(in, aliastransform[0]) + aliastransform[0][3];
	out[1] = DotProduct(in, aliastransform[1]) + aliastransform[1][3];
	out[2] = DotProduct(in, aliastransform[2]) + aliastransform[2][3];
}


/*
================
R_AliasPreparePoints

General clipped case
================
*/

#if defined(__MRISC32_VECTOR_OPS__) && !defined(NO_MR32_ASM)
#define USE_VERTS_VEC
/*
 * R_AliasTransformFinalVert, R_AliasProjectFinalVert and the clip flags of
 * R_AliasPreparePoints, VL vertices at a time. With unit scales it is also
 * R_AliasTransformAndProjectFinalVerts (its flags are replaced per triangle).
 * Z-clipped vertices get projected too; those values are never read.
 */
enum {
	VV_M = 0, VV_LIGHTVEC = 12, VV_SHADE = 15, VV_AMBIENT, VV_ZISCALE, VV_XSCALE, VV_YSCALE,
	VV_XCENTER, VV_YCENTER, VV_ZCLIP, VV_LEFT, VV_TOP, VV_RIGHT, VV_BOTTOM, VV_ONE, VV_COUNT
};

static void R_AliasVertsVec (int n, finalvert_t *fv, auxvert_t *av, const trivertx_t *pv,
			     float xscale, float yscale, float zscale)
{
	union { float f; int i; } k[VV_COUNT];
	int	i, j, count;
	const trivertx_t *p;
	auxvert_t	*a;
	finalvert_t	*f;

	for (i = 0; i < 3; i++)
		for (j = 0; j < 4; j++)
			k[VV_M + i * 4 + j].f = aliastransform[i][j];
	for (i = 0; i < 3; i++)
		k[VV_LIGHTVEC + i].f = r_plightvec[i];
	k[VV_SHADE].f = r_shadelight;
	k[VV_AMBIENT].i = r_ambientlight;
	k[VV_ZISCALE].f = zscale;
	k[VV_XSCALE].f = xscale;
	k[VV_YSCALE].f = yscale;
	k[VV_XCENTER].f = aliasxcenter;
	k[VV_YCENTER].f = aliasycenter;
	k[VV_ZCLIP].f = ALIAS_Z_CLIP_PLANE;
	k[VV_LEFT].i = r_refdef.aliasvrect.x;
	k[VV_TOP].i = r_refdef.aliasvrect.y;
	k[VV_RIGHT].i = r_refdef.aliasvrectright;
	k[VV_BOTTOM].i = r_refdef.aliasvrectbottom;
	k[VV_ONE].f = 1.0f;

	/* transform and light */
	count = n; p = pv; a = av; f = fv;
	__asm__ volatile(
		"    getsr   vl, #0x10\n"
		"    ldw     r9, [%[k], #0]\n    mov v8, r9\n"
		"    ldw     r9, [%[k], #4]\n    mov v9, r9\n"
		"    ldw     r9, [%[k], #8]\n    mov v10, r9\n"
		"    ldw     r9, [%[k], #12]\n   mov v11, r9\n"
		"    ldw     r9, [%[k], #16]\n   mov v12, r9\n"
		"    ldw     r9, [%[k], #20]\n   mov v13, r9\n"
		"    ldw     r9, [%[k], #24]\n   mov v14, r9\n"
		"    ldw     r9, [%[k], #28]\n   mov v15, r9\n"
		"    ldw     r9, [%[k], #32]\n   mov v16, r9\n"
		"    ldw     r9, [%[k], #36]\n   mov v17, r9\n"
		"    ldw     r9, [%[k], #40]\n   mov v18, r9\n"
		"    ldw     r9, [%[k], #44]\n   mov v19, r9\n"
		"    ldw     r9, [%[k], #48]\n   mov v20, r9\n"
		"    ldw     r9, [%[k], #52]\n   mov v21, r9\n"
		"    ldw     r9, [%[k], #56]\n   mov v22, r9\n"
		"    ldw     r9, [%[k], #60]\n   mov v23, r9\n"
		"    ldw     r9, [%[k], #64]\n   mov v24, r9\n"
		"    ldi     r10, #12\n"
		"1:\n"
		"    min     vl, vl, %[count]\n"
		"    sub     %[count], %[count], vl\n"
		"    ldub    v1, [%[p], #4]\n"
		"    add     r9, %[p], #1\n"
		"    ldub    v2, [r9, #4]\n"
		"    add     r9, %[p], #2\n"
		"    ldub    v3, [r9, #4]\n"
		"    add     r9, %[p], #3\n"
		"    ldub    v4, [r9, #4]\n"
		"    itof    v1, v1, z\n"
		"    itof    v2, v2, z\n"
		"    itof    v3, v3, z\n"
		"    fmul    v5, v1, v8\n"
		"    fmul    v6, v2, v9\n"
		"    fadd    v5, v5, v6\n"
		"    fmul    v6, v3, v10\n"
		"    fadd    v5, v5, v6\n"
		"    fadd    v5, v5, v11\n"
		"    stw     v5, [%[a], #12]\n"
		"    fmul    v5, v1, v12\n"
		"    fmul    v6, v2, v13\n"
		"    fadd    v5, v5, v6\n"
		"    fmul    v6, v3, v14\n"
		"    fadd    v5, v5, v6\n"
		"    fadd    v5, v5, v15\n"
		"    add     r9, %[a], #4\n"
		"    stw     v5, [r9, #12]\n"
		"    fmul    v5, v1, v16\n"
		"    fmul    v6, v2, v17\n"
		"    fadd    v5, v5, v6\n"
		"    fmul    v6, v3, v18\n"
		"    fadd    v5, v5, v6\n"
		"    fadd    v5, v5, v19\n"
		"    add     r9, %[a], #8\n"
		"    stw     v5, [r9, #12]\n"
		"    mul     v4, v4, r10\n"
		"    ldw     v1, [%[nrm], v4]\n"
		"    add     r9, %[nrm], #4\n"
		"    ldw     v2, [r9, v4]\n"
		"    add     r9, %[nrm], #8\n"
		"    ldw     v3, [r9, v4]\n"
		"    fmul    v1, v1, v20\n"
		"    fmul    v2, v2, v21\n"
		"    fadd    v1, v1, v2\n"
		"    fmul    v3, v3, v22\n"
		"    fadd    v1, v1, v3\n"
		"    fmin    v1, v1, z\n"
		"    fmul    v1, v23, v1\n"
		"    ftoi    v1, v1, z\n"
		"    add     v1, v1, v24\n"
		"    max     v1, v1, z\n"
		"    add     r9, %[f], #16\n"
		"    stw     v1, [r9, #32]\n"
		"    ldea    %[p], [%[p], vl*4]\n"
		"    mul     r9, vl, r10\n"
		"    add     %[a], %[a], r9\n"
		"    ldea    %[f], [%[f], vl*8]\n"
		"    ldea    %[f], [%[f], vl*8]\n"
		"    ldea    %[f], [%[f], vl*8]\n"
		"    ldea    %[f], [%[f], vl*8]\n"
		"    bnz     %[count], 1b\n"
		: [count] "+r"(count), [p] "+r"(p), [a] "+r"(a), [f] "+r"(f)
		: [k] "r"(k), [nrm] "r"(r_avertexnormals)
		: "r9", "r10", "vl", "v1", "v2", "v3", "v4", "v5", "v6",
		  "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18",
		  "v19", "v20", "v21", "v22", "v23", "v24", "memory"
	);

	/* project and clip test */
	count = n; a = av; f = fv;
	__asm__ volatile(
		"    getsr   vl, #0x10\n"
		"    ldw     r9, [%[k], #68]\n   mov v8, r9\n"
		"    ldw     r9, [%[k], #72]\n   mov v9, r9\n"
		"    ldw     r9, [%[k], #76]\n   mov v10, r9\n"
		"    ldw     r9, [%[k], #80]\n   mov v11, r9\n"
		"    ldw     r9, [%[k], #84]\n   mov v12, r9\n"
		"    ldw     r9, [%[k], #88]\n   mov v13, r9\n"
		"    ldw     r9, [%[k], #92]\n   mov v14, r9\n"
		"    ldw     r9, [%[k], #96]\n   mov v15, r9\n"
		"    ldw     r9, [%[k], #100]\n  mov v16, r9\n"
		"    ldw     r9, [%[k], #104]\n  mov v17, r9\n"
		"    ldw     r9, [%[k], #108]\n  mov v18, r9\n"
		"    ldi     r10, #12\n"
		"1:\n"
		"    min     vl, vl, %[count]\n"
		"    sub     %[count], %[count], vl\n"
		"    ldw     v1, [%[a], #12]\n"
		"    add     r9, %[a], #4\n"
		"    ldw     v2, [r9, #12]\n"
		"    add     r9, %[a], #8\n"
		"    ldw     v3, [r9, #12]\n"
		"    fdiv    v4, v18, v3\n"
		"    fmul    v5, v4, v8\n"
		"    ftoi    v5, v5, z\n"
		"    add     r9, %[f], #20\n"
		"    stw     v5, [r9, #32]\n"
		"    fmul    v1, v1, v9\n"
		"    fmul    v1, v1, v4\n"
		"    fadd    v1, v1, v11\n"
		"    ftoi    v1, v1, z\n"
		"    stw     v1, [%[f], #32]\n"
		"    fmul    v2, v2, v10\n"
		"    fmul    v2, v2, v4\n"
		"    fadd    v2, v2, v12\n"
		"    ftoi    v2, v2, z\n"
		"    add     r9, %[f], #4\n"
		"    stw     v2, [r9, #32]\n"
		"    slt     v5, v1, v14\n"
		"    and     v5, v5, #1\n"
		"    slt     v6, v2, v15\n"
		"    and     v6, v6, #2\n"
		"    or      v5, v5, v6\n"
		"    slt     v6, v16, v1\n"
		"    and     v6, v6, #4\n"
		"    or      v5, v5, v6\n"
		"    slt     v6, v17, v2\n"
		"    and     v6, v6, #8\n"
		"    or      v5, v5, v6\n"
		"    fslt    v6, v3, v13\n"
		"    ldi     r9, #16\n"
		"    and     v7, v6, r9\n"
		"    sel     v6, v7, v5\n"
		"    add     r9, %[f], #24\n"
		"    stw     v6, [r9, #32]\n"
		"    mul     r9, vl, r10\n"
		"    add     %[a], %[a], r9\n"
		"    ldea    %[f], [%[f], vl*8]\n"
		"    ldea    %[f], [%[f], vl*8]\n"
		"    ldea    %[f], [%[f], vl*8]\n"
		"    ldea    %[f], [%[f], vl*8]\n"
		"    bnz     %[count], 1b\n"
		: [count] "+r"(count), [a] "+r"(a), [f] "+r"(f)
		: [k] "r"(k)
		: "r9", "r10", "vl", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
		  "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "memory"
	);
}
#endif

static void R_AliasPreparePoints (void)
{
	int			i;
	stvert_t	*pstverts;
	finalvert_t	*fv;
	auxvert_t	*av;
	mtriangle_t	*ptri;
	finalvert_t	*pfv[3];

	pstverts = (stvert_t *)((byte *)paliashdr + paliashdr->stverts);
	r_anumverts = pmdl->numverts;
	fv = pfinalverts;
	av = pauxverts;

#ifdef USE_VERTS_VEC
	R_AliasVertsVec (r_anumverts, fv, av, r_apverts, aliasxscale, aliasyscale, ziscale);
	r_apverts += r_anumverts;
#else
	for (i = 0 ; i < r_anumverts ; i++, fv++, av++, r_apverts++)
	{
//		fv->v[2] = pstverts->s;
//		fv->v[3] = pstverts->t;
		fv->flags = 0;
		R_AliasTransformFinalVert (fv, av, r_apverts);
		if (av->fv[2] < ALIAS_Z_CLIP_PLANE)
			fv->flags |= ALIAS_Z_CLIP;
		else
		{
			R_AliasProjectFinalVert (fv, av);

			if (fv->v[0] < r_refdef.aliasvrect.x)
				fv->flags |= ALIAS_LEFT_CLIP;
			if (fv->v[1] < r_refdef.aliasvrect.y)
				fv->flags |= ALIAS_TOP_CLIP;
			if (fv->v[0] > r_refdef.aliasvrectright)
				fv->flags |= ALIAS_RIGHT_CLIP;
			if (fv->v[1] > r_refdef.aliasvrectbottom)
				fv->flags |= ALIAS_BOTTOM_CLIP;
		}
	}
#endif

//
// clip and draw all triangles
//
	r_affinetridesc.numtriangles = 1;

	ptri = (mtriangle_t *)((byte *)paliashdr + paliashdr->triangles);
	for (i = 0 ; i < pmdl->numtris ; i++, ptri++)
	{
		pfv[0] = &pfinalverts[ptri->vertindex[0]];
		pfv[1] = &pfinalverts[ptri->vertindex[1]];
		pfv[2] = &pfinalverts[ptri->vertindex[2]];

		if ( pfv[0]->flags & pfv[1]->flags & pfv[2]->flags & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP) )
			continue;		// completely clipped

		//jfm: fill in the triangles s and t into the finalvert
		pfv[0]->v[2] = pstverts[ptri->stindex[0]].s;
		pfv[0]->v[3] = pstverts[ptri->stindex[0]].t;
		pfv[0]->flags |= pstverts[ptri->stindex[0]].onseam;

		pfv[1]->v[2] = pstverts[ptri->stindex[1]].s;
		pfv[1]->v[3] = pstverts[ptri->stindex[1]].t;
		pfv[1]->flags |= pstverts[ptri->stindex[1]].onseam;

		pfv[2]->v[2] = pstverts[ptri->stindex[2]].s;
		pfv[2]->v[3] = pstverts[ptri->stindex[2]].t;
		pfv[2]->flags |= pstverts[ptri->stindex[2]].onseam;

		if ( ! ( (pfv[0]->flags | pfv[1]->flags | pfv[2]->flags) & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP) ) )
		{	// totally unclipped
			r_affinetridesc.pfinalverts = pfinalverts;
			r_affinetridesc.ptriangles = ptri;

			if (currententity->model->flags & EF_SPECIAL_TRANS)
				D_PolysetDrawT5 ();
			else if (currententity->drawflags & DRF_TRANSLUCENT)
				D_PolysetDrawT ();
			else if (currententity->model->flags & EF_TRANSPARENT)
				D_PolysetDrawT2 ();
			else if (currententity->model->flags & EF_HOLEY)
				D_PolysetDrawT3 ();
			else
				D_PolysetDraw ();
		}
		else
		{	// partially clipped
			R_AliasClipTriangle (ptri);
		}
	}
}


/*
================
R_AliasSetUpTransform
================
*/
static void R_AliasSetUpTransform (int trivial_accept)
{
	int			i;
	float		rotationmatrix[3][4], t2matrix[3][4];
	static float	tmatrix[3][4];
	static float	viewmatrix[3][4];
	vec3_t		angles;
	float		entScale;
	float		xyfact = 1.0, zfact = 1.0; // avoid compiler warning
	float		forward;
	float		yaw, pitch;

// TODO: should really be stored with the entity instead of being reconstructed
// TODO: should use a look-up table
// TODO: could cache lazily, stored in the entity

	if (currententity->model->flags & EF_FACE_VIEW)
	{
		VectorSubtract(currententity->origin,r_origin,angles);
		VectorSubtract(r_origin,currententity->origin,angles);
		VectorNormalizeFast(angles);

		if (angles[1] == 0 && angles[0] == 0)
		{
			yaw = 0;
			if (angles[2] > 0)
				pitch = 90;
			else
				pitch = 270;
		}
		else
		{
			yaw = (int) (atan2(angles[1], angles[0]) * 180 / M_PI);
			if (yaw < 0)
				yaw += 360;

			forward = Q_sqrt (angles[0]*angles[0] + angles[1]*angles[1]);
			pitch = (int) (atan2(angles[2], forward) * 180 / M_PI);
			if (pitch < 0)
				pitch += 360;
		}

		angles[0] = -pitch;
		angles[1] = yaw;
//		angles[2] = 0;
		angles[2] = currententity->angles[ROLL];
	}
	else
	{
		angles[ROLL] = currententity->angles[ROLL];
		angles[PITCH] = -currententity->angles[PITCH];
		if (currententity->model->flags & EF_ROTATE)
		{
			angles[YAW] = anglemod( (currententity->origin[0] + currententity->origin[1])*0.8
						+ (108*cl.time) );
		}
		else
		{
			angles[YAW] = currententity->angles[YAW];
		}
	}

	AngleVectors (angles, alias_forward, alias_right, alias_up);

	if (currententity->scale != 0 && currententity->scale != 100)
	{
		entScale = (float)currententity->scale/100.0;
		switch (currententity->drawflags & SCALE_TYPE_MASKIN)
		{
		case SCALE_TYPE_UNIFORM:
			tmatrix[0][0] = pmdl->scale[0]*entScale;
			tmatrix[1][1] = pmdl->scale[1]*entScale;
			tmatrix[2][2] = pmdl->scale[2]*entScale;
			xyfact = zfact = (entScale-1.0)*127.95;
			break;
		case SCALE_TYPE_XYONLY:
			tmatrix[0][0] = pmdl->scale[0]*entScale;
			tmatrix[1][1] = pmdl->scale[1]*entScale;
			tmatrix[2][2] = pmdl->scale[2];
			xyfact = (entScale-1.0)*127.95;
			zfact = 1.0;
			break;
		case SCALE_TYPE_ZONLY:
			tmatrix[0][0] = pmdl->scale[0];
			tmatrix[1][1] = pmdl->scale[1];
			tmatrix[2][2] = pmdl->scale[2]*entScale;
			xyfact = 1.0;
			zfact = (entScale-1.0)*127.95;
			break;
		}
		switch (currententity->drawflags & SCALE_ORIGIN_MASKIN)
		{
		case SCALE_ORIGIN_CENTER:
			tmatrix[0][3] = pmdl->scale_origin[0]-pmdl->scale[0]*xyfact;
			tmatrix[1][3] = pmdl->scale_origin[1]-pmdl->scale[1]*xyfact;
			tmatrix[2][3] = pmdl->scale_origin[2]-pmdl->scale[2]*zfact;
			break;
		case SCALE_ORIGIN_BOTTOM:
			tmatrix[0][3] = pmdl->scale_origin[0]-pmdl->scale[0]*xyfact;
			tmatrix[1][3] = pmdl->scale_origin[1]-pmdl->scale[1]*xyfact;
			tmatrix[2][3] = pmdl->scale_origin[2];
			break;
		case SCALE_ORIGIN_TOP:
			tmatrix[0][3] = pmdl->scale_origin[0]-pmdl->scale[0]*xyfact;
			tmatrix[1][3] = pmdl->scale_origin[1]-pmdl->scale[1]*xyfact;
			tmatrix[2][3] = pmdl->scale_origin[2]-pmdl->scale[2]*zfact*2.0;
			break;
		}
	}
	else
	{
		tmatrix[0][0] = pmdl->scale[0];
		tmatrix[1][1] = pmdl->scale[1];
		tmatrix[2][2] = pmdl->scale[2];
		tmatrix[0][3] = pmdl->scale_origin[0];
		tmatrix[1][3] = pmdl->scale_origin[1];
		tmatrix[2][3] = pmdl->scale_origin[2];
	}

	if (currententity->model->flags & EF_ROTATE)
	{
		// Floating motion
		tmatrix[2][3] += q_sinrad(currententity->origin[0]
					+ currententity->origin[1]
					+ (cl.time*3)) * 5.5;
	}

// TODO: can do this with simple matrix rearrangement

	for (i = 0 ; i < 3 ; i++)
	{
		t2matrix[i][0] = alias_forward[i];
		t2matrix[i][1] = -alias_right[i];
		t2matrix[i][2] = alias_up[i];
	}

	t2matrix[0][3] = -modelorg[0];
	t2matrix[1][3] = -modelorg[1];
	t2matrix[2][3] = -modelorg[2];

// FIXME: can do more efficiently than full concatenation
	R_ConcatTransforms (t2matrix, tmatrix, rotationmatrix);

// TODO: should be global, set when vright, etc., set
	VectorCopy (vright, viewmatrix[0]);
	VectorCopy (vup, viewmatrix[1]);
	VectorInverse (viewmatrix[1]);
	VectorCopy (vpn, viewmatrix[2]);

//	viewmatrix[0][3] = 0;
//	viewmatrix[1][3] = 0;
//	viewmatrix[2][3] = 0;

	R_ConcatTransforms (viewmatrix, rotationmatrix, aliastransform);

// do the scaling up of x and y to screen coordinates as part of the transform
// for the unclipped case (it would mess up clipping in the clipped case).
// Also scale down z, so 1/z is scaled 31 bits for free, and scale down x and y
// correspondingly so the projected x and y come out right
// FIXME: make this work for clipped case too?
	if (trivial_accept)
	{
		for (i = 0 ; i < 4 ; i++)
		{
			aliastransform[0][i] *= aliasxscale * (1.0 / ((float)0x8000 * 0x10000));
			aliastransform[1][i] *= aliasyscale * (1.0 / ((float)0x8000 * 0x10000));
			aliastransform[2][i] *= 1.0 / ((float)0x8000 * 0x10000);
		}
	}
}


/*
================
R_AliasTransformFinalVert
================
*/
static void R_AliasTransformFinalVert (finalvert_t *fv, auxvert_t *av, trivertx_t *pverts)
{
	int		temp;
	float		lightcos;
	const float *plightnormal;

	av->fv[0] = DotProduct(pverts->v, aliastransform[0]) + aliastransform[0][3];
	av->fv[1] = DotProduct(pverts->v, aliastransform[1]) + aliastransform[1][3];
	av->fv[2] = DotProduct(pverts->v, aliastransform[2]) + aliastransform[2][3];

// lighting
	plightnormal = r_avertexnormals[pverts->lightnormalindex];
	lightcos = DotProduct (plightnormal, r_plightvec);
	temp = r_ambientlight;

	if (lightcos < 0)
	{
		temp += (int)(r_shadelight * lightcos);

	// clamp; because we limited the minimum ambient and shading light, we
	// don't have to clamp low light, just bright
		if (temp < 0)
			temp = 0;
	}

	fv->v[4] = temp;
}


#if	!id386

/*
================
R_AliasTransformAndProjectFinalVerts
================
*/
static void R_AliasTransformAndProjectFinalVerts (finalvert_t *fv, stvert_t *pstverts)
{
	int		i, temp;
	float		lightcos, zi;
	const float *plightnormal;
	const trivertx_t *pverts;

	pverts = r_apverts;

	for (i = 0 ; i < r_anumverts ; i++, fv++, pverts++, pstverts++)
	{
	// transform and project
		zi = 1.0 / (DotProduct(pverts->v, aliastransform[2]) +
						aliastransform[2][3]);

	// x, y, and z are scaled down by 1/2**31 in the transform, so 1/z is
	// scaled up by 1/2**31, and the scaling cancels out for x and y in the
	// projection
		fv->v[5] = zi;

		fv->v[0] = ((DotProduct(pverts->v, aliastransform[0]) +
				aliastransform[0][3]) * zi) + aliasxcenter;
		fv->v[1] = ((DotProduct(pverts->v, aliastransform[1]) +
				aliastransform[1][3]) * zi) + aliasycenter;

		fv->v[2] = pstverts->s;
		fv->v[3] = pstverts->t;
		fv->flags = pstverts->onseam;

	// lighting
		plightnormal = r_avertexnormals[pverts->lightnormalindex];
		lightcos = DotProduct (plightnormal, r_plightvec);
		temp = r_ambientlight;

		if (lightcos < 0)
		{
			temp += (int)(r_shadelight * lightcos);

		// clamp; because we limited the minimum ambient and shading
		// light, we don't have to clamp low light, just bright
			if (temp < 0)
				temp = 0;
		}

		fv->v[4] = temp;
	}
}

#endif


/*
================
R_AliasProjectFinalVert
================
*/
void R_AliasProjectFinalVert (finalvert_t *fv, auxvert_t *av)
{
	float	zi;

// project points
	zi = 1.0 / av->fv[2];

	fv->v[5] = zi * ziscale;

	fv->v[0] = (av->fv[0] * aliasxscale * zi) + aliasxcenter;
	fv->v[1] = (av->fv[1] * aliasyscale * zi) + aliasycenter;
}


/*
================
R_AliasPreparePartial

A model that needs clipping but may use the guard band (the view weapon, mostly):
the engine transforms all the vertices (VERTS, scaled as R_AliasSetUpTransform does
for the unclipped case, with ziscale) and draws the triangles whose vertices are in
front of the near plane and inside the guard band as one MESH; the CPU transforms
only the vertices of the other triangles, and clips those (r_pepartial).
================
*/
static void R_AliasPreparePartial (void)
{
	stvert_t	*pstverts = (stvert_t *)((byte *)paliashdr + paliashdr->stverts);
	mtriangle_t	*ptri = (mtriangle_t *)((byte *)paliashdr + paliashdr->triangles);
	const int	n = pmdl->numverts, nt = pmdl->numtris;
	finalvert_t	*fvpe = PE_AllocVerts (n + (nt * (int)sizeof(unsigned short) + (int)sizeof(finalvert_t) - 1) / (int)sizeof(finalvert_t));
	unsigned short	*order = (unsigned short *)(fvpe + n);
	byte		state[MAXALIASVERTS];	/* bit 0: clipped by the CPU, bit 1: transformed here */
	float		m[3][4];
	int		i, j, count = 0;

	for (j = 0; j < 4; j++)
	{
		m[0][j] = aliastransform[0][j] * aliasxscale / ziscale;
		m[1][j] = aliastransform[1][j] * aliasyscale / ziscale;
		m[2][j] = aliastransform[2][j] / ziscale;
	}
	PE_AliasVertsM (fvpe, r_apverts, n, m);

	/* bit 0: the CPU clips the vertex's triangles, as it is behind the near plane
	   or outside the guard band */
	{
		const float	gx0 = xcenter - PE_GUARD * (xcenter - r_refdef.fvrectx);
		const float	gx1 = xcenter + PE_GUARD * (r_refdef.fvrectright - xcenter);
		const float	gy0 = ycenter - PE_GUARD * (ycenter - r_refdef.fvrecty);
		const float	gy1 = ycenter + PE_GUARD * (r_refdef.fvrectbottom - ycenter);

		for (i = 0; i < n; i++)
		{
			const byte	*v = r_apverts[i].v;
			const float	z = DotProduct(v, aliastransform[2]) + aliastransform[2][3];
			float		zi, u, w;

			state[i] = 1;
			if (z < ALIAS_Z_CLIP_PLANE)
				continue;
			zi = 1.0f / z;
			u = (DotProduct(v, aliastransform[0]) + aliastransform[0][3]) * xscale * zi + xcenter;
			w = (DotProduct(v, aliastransform[1]) + aliastransform[1][3]) * yscale * zi + ycenter;
			if (u >= gx0 && u <= gx1 && w >= gy0 && w <= gy1)
				state[i] = 0;
		}
	}

	r_affinetridesc.numtriangles = 1;
	for (i = 0; i < nt; i++, ptri++)
	{
		const int cpu = (state[ptri->vertindex[0]] | state[ptri->vertindex[1]] |
				state[ptri->vertindex[2]]) & 1;
		finalvert_t	*pfv[3];

		if (!cpu)
		{
			order[count++] = i;
			continue;
		}
		for (j = 0; j < 3; j++)
		{
			const int vi = ptri->vertindex[j];
			finalvert_t	*fv = &pfinalverts[vi];
			auxvert_t	*av = &pauxverts[vi];

			pfv[j] = fv;
			if (state[vi] & 2)
				continue;
			state[vi] |= 2;
			fv->flags = 0;
			R_AliasTransformFinalVert (fv, av, &r_apverts[vi]);
			if (av->fv[2] < ALIAS_Z_CLIP_PLANE)
				fv->flags |= ALIAS_Z_CLIP;
			else
			{
				R_AliasProjectFinalVert (fv, av);
				if (fv->v[0] < r_refdef.aliasvrect.x)
					fv->flags |= ALIAS_LEFT_CLIP;
				if (fv->v[1] < r_refdef.aliasvrect.y)
					fv->flags |= ALIAS_TOP_CLIP;
				if (fv->v[0] > r_refdef.aliasvrectright)
					fv->flags |= ALIAS_RIGHT_CLIP;
				if (fv->v[1] > r_refdef.aliasvrectbottom)
					fv->flags |= ALIAS_BOTTOM_CLIP;
			}
		}
		if (pfv[0]->flags & pfv[1]->flags & pfv[2]->flags & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))
			continue;		// completely clipped
		for (j = 0; j < 3; j++)
		{
			pfv[j]->v[2] = pstverts[ptri->stindex[j]].s;
			pfv[j]->v[3] = pstverts[ptri->stindex[j]].t;
			pfv[j]->flags |= pstverts[ptri->stindex[j]].onseam;
		}
		if (!((pfv[0]->flags | pfv[1]->flags | pfv[2]->flags) & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP)))
		{	// on the screen after all (between the guard band and the screen edge)
			r_affinetridesc.pfinalverts = pfinalverts;
			r_affinetridesc.ptriangles = ptri;
			if (currententity->model->flags & EF_SPECIAL_TRANS)
				D_PolysetDrawT5 ();
			else if (currententity->drawflags & DRF_TRANSLUCENT)
				D_PolysetDrawT ();
			else if (currententity->model->flags & EF_TRANSPARENT)
				D_PolysetDrawT2 ();
			else if (currententity->model->flags & EF_HOLEY)
				D_PolysetDrawT3 ();
			else
				D_PolysetDraw ();
		}
		else
			R_AliasClipTriangle (ptri);
	}
	if (count)
		PE_DrawMeshOrder (fvpe, (mtriangle_t *)((byte *)paliashdr + paliashdr->triangles), order, count,
				pstverts, r_affinetridesc.seamfixupX16);
}


/* the engine transforms, projects and lights the vertices (version 9) */
static void PE_AliasVertsM (finalvert_t *fv, const trivertx_t *pverts, int numverts, float m[3][4])
{
	int	i, j;

	PE_Begin (PE_VERTS, 23);
	PE_Ptr (pverts);
	PE_Word (numverts);
	PE_Ptr (fv);
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 4; j++)
			PE_Float (m[i][j]);
	}
	PE_Float (aliasxcenter);
	PE_Float (aliasycenter);
	PE_Ptr (r_avertexnormals);
	PE_Float (r_plightvec[0]);
	PE_Float (r_plightvec[1]);
	PE_Float (r_plightvec[2]);
	PE_Word (r_ambientlight);
	PE_Float (r_shadelight);
}

#define PE_AliasVerts(fv, pverts, numverts)	PE_AliasVertsM (fv, pverts, numverts, aliastransform)

/*
================
R_AliasPrepareUnclippedPoints
================
*/
void R_AliasPrepareUnclippedPoints (void)
{
	stvert_t	*pstverts;
	finalvert_t	*fv;
	mtriangle_t	*ptri;
	int	i;

	pstverts = (stvert_t *)((byte *)paliashdr + paliashdr->stverts);
	r_anumverts = pmdl->numverts;
// FIXME: just use pfinalverts directly?
	fv = pfinalverts;

	if (pe_mesh && pe_version >= 9)
		PE_AliasVerts (fv, r_apverts, r_anumverts);
	else
	{
#ifdef USE_VERTS_VEC
		R_AliasVertsVec (r_anumverts, fv, pauxverts, r_apverts, 1.0f, 1.0f, 1.0f);
#else
		R_AliasTransformAndProjectFinalVerts (fv, pstverts);
#endif
	}

	r_affinetridesc.pfinalverts = pfinalverts;
	r_affinetridesc.ptriangles = (mtriangle_t *)
					((byte *)paliashdr + paliashdr->triangles);
	r_affinetridesc.numtriangles = 1;//pmdl->numtris;

	if (pe_mesh && PE_DrawMesh (pfinalverts, r_affinetridesc.ptriangles, pmdl->numtris,
			pstverts, r_affinetridesc.seamfixupX16))
		return;

	ptri = (mtriangle_t *)((byte *)paliashdr + paliashdr->triangles);
	for (i = 0 ; i < pmdl->numtris ; i++, ptri++)
	{
		finalvert_t	*pfv[3];

		pfv[0] = &pfinalverts[ptri->vertindex[0]];
		pfv[1] = &pfinalverts[ptri->vertindex[1]];
		pfv[2] = &pfinalverts[ptri->vertindex[2]];

		//jfm: fill in the triangles s and t into the finalvert
		pfv[0]->v[2] = pstverts[ptri->stindex[0]].s;
		pfv[0]->v[3] = pstverts[ptri->stindex[0]].t;
		pfv[0]->flags = pstverts[ptri->stindex[0]].onseam;

		pfv[1]->v[2] = pstverts[ptri->stindex[1]].s;
		pfv[1]->v[3] = pstverts[ptri->stindex[1]].t;
		pfv[1]->flags = pstverts[ptri->stindex[1]].onseam;

		pfv[2]->v[2] = pstverts[ptri->stindex[2]].s;
		pfv[2]->v[3] = pstverts[ptri->stindex[2]].t;
		pfv[2]->flags = pstverts[ptri->stindex[2]].onseam;

		r_affinetridesc.ptriangles = ptri;

		if (r_affinetridesc.drawtype)
		{
			if (currententity->model->flags & EF_SPECIAL_TRANS)
			{
				D_PolysetDrawFinalVertsT5 (pfv[0],pfv[1],pfv[2]);
				D_PolysetDrawT5 ();
			}
			else if (currententity->drawflags & DRF_TRANSLUCENT)
			{
				D_PolysetDrawFinalVertsT (pfv[0],pfv[1],pfv[2]);
				D_PolysetDrawT ();
			}
			else if (currententity->model->flags & EF_TRANSPARENT)
			{
				D_PolysetDrawFinalVertsT2 (pfv[0],pfv[1],pfv[2]);
				D_PolysetDrawT2 ();
			}
			else if (currententity->model->flags & EF_HOLEY)
			{
				D_PolysetDrawFinalVertsT3 (pfv[0],pfv[1],pfv[2]);
				D_PolysetDrawT3 ();
			}
			else
			{
				D_PolysetDrawFinalVerts (pfv[0],pfv[1],pfv[2]);
				D_PolysetDraw ();
			}
		}
		else
		{
			if (currententity->model->flags & EF_SPECIAL_TRANS)
				D_PolysetDrawT5 ();
			else if (currententity->drawflags & DRF_TRANSLUCENT)
				D_PolysetDrawT ();
			else if (currententity->model->flags & EF_TRANSPARENT)
				D_PolysetDrawT2 ();
			else if (currententity->model->flags & EF_HOLEY)
				D_PolysetDrawT3 ();
			else
				D_PolysetDraw ();
		}
	}
}

/*
===============
R_AliasSetupSkin
===============
*/
static void R_AliasSetupSkin (void)
{
	int			i, numskins, skinnum;
	int			a_skinwidth;
	maliasskingroup_t	*paliasskingroup;
	float			*pskinintervals, fullskininterval;
	float			skintargettime, skintime;
	qpic_t			*stonepic;
	char			temp[40];

	skinnum = currententity->skinnum;
	if (skinnum >= 100)
		skinnum = 0;

	if ((skinnum >= pmdl->numskins) || (skinnum < 0))
	{
		Con_DPrintf ("%s: no such skin # %d\n", __thisfunc__, skinnum);
		skinnum = 0;
	}

	if ( (currententity->model->flags & EF_MIP_MAP) &&
			(currententity->model->flags & EF_MIP_MAP_FAR) &&
			skinnum+1 < pmdl->numskins )
		skinnum++;

	pskindesc = ((maliasskindesc_t *)((byte *)paliashdr + paliashdr->skindesc)) + skinnum;
	a_skinwidth = pmdl->skinwidth;

	if (pskindesc->type == ALIAS_SKIN_GROUP)
	{
		paliasskingroup = (maliasskingroup_t *)((byte *)paliashdr + pskindesc->skin);
		pskinintervals = (float *)((byte *)paliashdr + paliasskingroup->intervals);
		numskins = paliasskingroup->numskins;
		fullskininterval = pskinintervals[numskins-1];

		skintime = cl.time + currententity->syncbase;

	// when loading in Mod_LoadAliasSkinGroup, we guaranteed all interval
	// values are positive, so we don't have to worry about division by 0
		skintargettime = skintime -
				((int)(skintime / fullskininterval)) * fullskininterval;

		for (i = 0 ; i < (numskins-1) ; i++)
		{
			if (pskinintervals[i] > skintargettime)
				break;
		}

		pskindesc = &paliasskingroup->skindescs[i];
	}

	if (currententity->skinnum >= 100)
	{
		sprintf(temp,"gfx/skin%d.lmp",currententity->skinnum);
		stonepic = Draw_CachePic(temp);

		r_affinetridesc.pskindesc = pskindesc;
		r_affinetridesc.pskin = stonepic->data;
		r_affinetridesc.skinwidth = stonepic->width;
		r_affinetridesc.seamfixupX16 =  (stonepic->width >> 1) << 16;
		r_affinetridesc.skinheight = stonepic->height;
	}
	else
	{
		r_affinetridesc.pskindesc = pskindesc;
		r_affinetridesc.pskin = (void *)((byte *)paliashdr + pskindesc->skin);
		r_affinetridesc.skinwidth = a_skinwidth;
		r_affinetridesc.seamfixupX16 =  (a_skinwidth >> 1) << 16;
		r_affinetridesc.skinheight = pmdl->skinheight;
	}
}

/*
================
R_AliasSetupLighting
================
*/
static void R_AliasSetupLighting (alight_t *plighting)
{

// guarantee that no vertex will ever be lit below LIGHT_MIN, so we don't have
// to clamp off the bottom
	r_ambientlight = plighting->ambientlight;

	if (r_ambientlight < LIGHT_MIN)
		r_ambientlight = LIGHT_MIN;

	r_ambientlight = (255 - r_ambientlight) << VID_CBITS;

	if (r_ambientlight < LIGHT_MIN)
		r_ambientlight = LIGHT_MIN;

	r_shadelight = plighting->shadelight;

	if (r_shadelight < 0)
		r_shadelight = 0;

	r_shadelight *= VID_GRADES;

// rotate the lighting vector into the model's frame of reference
	r_plightvec[0] = DotProduct (plighting->plightvec, alias_forward);
	r_plightvec[1] = -DotProduct (plighting->plightvec, alias_right);
	r_plightvec[2] = DotProduct (plighting->plightvec, alias_up);
}

/*
=================
R_AliasSetupFrame

set r_apverts
=================
*/
static void R_AliasSetupFrame (void)
{
	int		i, frame, numframes;
	maliasgroup_t	*paliasgroup;
	float		*pintervals, fullinterval, targettime, time;

	frame = currententity->frame;
	if ((frame >= pmdl->numframes) || (frame < 0))
	{
		Con_DPrintf ("%s: no such frame %d\n", __thisfunc__, frame);
		frame = 0;
	}

	if (paliashdr->frames[frame].type == ALIAS_SINGLE)
	{
		r_apverts = (trivertx_t *)
				((byte *)paliashdr + paliashdr->frames[frame].frame);
		return;
	}

	paliasgroup = (maliasgroup_t *)
				((byte *)paliashdr + paliashdr->frames[frame].frame);
	pintervals = (float *)((byte *)paliashdr + paliasgroup->intervals);
	numframes = paliasgroup->numframes;
	fullinterval = pintervals[numframes-1];

	time = cl.time + currententity->syncbase;

//
// when loading in Mod_LoadAliasGroup, we guaranteed all interval values
// are positive, so we don't have to worry about division by 0
//
	targettime = time - ((int)(time / fullinterval)) * fullinterval;

	for (i = 0 ; i < (numframes-1) ; i++)
	{
		if (pintervals[i] > targettime)
			break;
	}

	r_apverts = (trivertx_t *)
			((byte *)paliashdr + paliasgroup->frames[i].frame);
}


/*
================
R_AliasDrawModel
================
*/
static qboolean R_AliasSameLayout (aliashdr_t *a, aliashdr_t *b)
{
	newmdl_t	*ma = (newmdl_t *)((byte *)a + a->model);
	newmdl_t	*mb = (newmdl_t *)((byte *)b + b->model);

	return ma->numframes == mb->numframes && ma->numskins == mb->numskins;
}

static aliashdr_t *R_AliasSelectLod (void)
{
	qmodel_t	*mod = currententity->model;
	aliashdr_t	*full, *lod;
	vec3_t		delta;

	if (!mod->lod || r_lodsize.value <= 0 || currententity == &cl.viewent)
		return (aliashdr_t *)Mod_Extradata (mod);
	VectorSubtract (currententity->origin, r_origin, delta);
	if (DotProduct (delta, vpn) * r_lodsize.value <= 64.0f * xscale)
		return (aliashdr_t *)Mod_Extradata (mod);

	// loading one can push the other out of the cache
	lod = (aliashdr_t *)Mod_Extradata (mod->lod);
	full = (aliashdr_t *)Mod_Extradata (mod);
	if (!Cache_Check (&mod->lod->cache) || !R_AliasSameLayout (lod, full))
		return full;
	return lod;
}

void R_AliasDrawModel (alight_t *plighting)
{
	int		mls;
	int		i, j;
	byte		*dest, *source, *sourceA;
	auxvert_t	auxverts[MAXALIASVERTS];
	finalvert_t	finalverts[MAXALIASVERTS + ((CACHE_SIZE - 1) / sizeof(finalvert_t)) + 1];

	r_amodels_drawn++;

// cache align
	pfinalverts = (finalvert_t *)
			(((intptr_t)&finalverts[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
	pauxverts = &auxverts[0];

	paliashdr = R_AliasSelectLod ();
	pmdl = (newmdl_t *)((byte *)paliashdr + paliashdr->model);

	R_AliasSetupSkin ();
	R_AliasSetUpTransform (currententity->trivial_accept);

	mls = currententity->drawflags & MLS_MASKIN;
	if (currententity->model->flags & EF_ROTATE)
	{
		plighting->ambientlight =
		plighting->shadelight =
			60 + 34 + q_sinrad(currententity->origin[0] + currententity->origin[1] + (cl.time*3.8))*34;
		R_AliasSetupLighting (plighting);
		r_plightvec[0] = r_plightvec[1] = r_plightvec[2] = 0;
	}
	else if (mls == MLS_ABSLIGHT)
	{
		plighting->ambientlight =
		plighting->shadelight =
					currententity->abslight;
		R_AliasSetupLighting (plighting);
		r_plightvec[0] = r_plightvec[1] = r_plightvec[2] = 0;
	}
	else if (mls != MLS_NONE)
	{
		// Use a model light style (25-30)
		plighting->ambientlight =
		plighting->shadelight =
					d_lightstylevalue[24+mls]/2;
		R_AliasSetupLighting (plighting);
	}
	else
		R_AliasSetupLighting (plighting);

	R_AliasSetupFrame ();

	if (!currententity->colormap)
		Sys_Error ("%s: !currententity->colormap", __thisfunc__);

	r_affinetridesc.drawtype =
		(currententity->trivial_accept == 3) && r_recursiveaffinetriangles;

	if (r_affinetridesc.drawtype)
	{
		D_PolysetUpdateTables ();	// FIXME: precalc...
	}
	else
	{
#if	id386
		if (currententity->model->flags & EF_SPECIAL_TRANS)
			D_Aff8PatchT5 (currententity->colormap);
		else if (currententity->drawflags & DRF_TRANSLUCENT)
			D_Aff8PatchT (currententity->colormap);
		else if (currententity->model->flags & EF_TRANSPARENT)
			D_Aff8PatchT2 (currententity->colormap);
		else if (currententity->model->flags & EF_HOLEY)
			D_Aff8PatchT3 (currententity->colormap);
		else
			D_Aff8Patch (currententity->colormap);
#endif
	}

	acolormap = currententity->colormap;

	if ( currententity->colorshade &&
		(currententity->colorshade != lastglobalcolor ||
		 currententity->sourcecolormap != lastsourcecolormap) )
	{
		lastglobalcolor = currententity->colorshade;
		lastsourcecolormap = currententity->sourcecolormap;

		dest = globalcolormap;
		source = currententity->sourcecolormap;
		sourceA = transTable + (((int)lastglobalcolor)*256);

		for (i = 0 ; i < VID_GRADES ; i++)
		{
			for (j = 0 ; j < 256 ; j++, dest++, source++)
			{
				*dest = sourceA[*source];
			}
		}
	}

	/* the engine's skin modes match D_PolysetDrawT5, T, T2 and T3 */
	pe_mode = 0;
	pe_table = NULL;
	if (currententity->model->flags & EF_SPECIAL_TRANS)
	{
		pe_mode = PE_SKIN_SKIP0 | PE_SKIN_BLEND_TEXEL;
		pe_table = transTable;
	}
	else if (currententity->drawflags & DRF_TRANSLUCENT)
	{
		pe_mode = PE_SKIN_SKIP0 | PE_SKIN_BLEND_LIT;
		pe_table = mainTransTable;
	}
	else if (currententity->model->flags & EF_TRANSPARENT)
	{
		pe_mode = PE_SKIN_SKIP0 | PE_SKIN_BLEND_ODD;
		pe_table = mainTransTable;
	}
	else if (currententity->model->flags & EF_HOLEY)
		pe_mode = PE_SKIN_SKIP0;

	pe_alias = pe_active && acolormap == vid.colormap && (!pe_mode || pe_version >= 8);
	pe_mesh = false;
	if (pe_alias)
	{
		PE_SetSkin (r_affinetridesc.pskin, r_affinetridesc.skinwidth, pe_mode, pe_table);
		if (currententity->trivial_accept && pe_version >= 8)
		{
			pfinalverts = PE_AllocVerts (pmdl->numverts);
			pe_mesh = true;
		}
	}
	else
		PE_Finish ();

	if (currententity != &cl.viewent)
		ziscale = (float)0x8000 * (float)0x10000;
	else
		ziscale = (float)0x8000 * (float)0x10000 * 3.0;

	if (currententity->trivial_accept)
		R_AliasPrepareUnclippedPoints ();
	else if (pe_alias && r_alias_zonly && pe_version >= 9 && r_pepartial.integer)
		R_AliasPreparePartial ();
	else
		R_AliasPreparePoints ();
}

