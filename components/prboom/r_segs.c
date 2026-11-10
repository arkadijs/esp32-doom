/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2004 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *      All the clipping: columns, horizontal spans, sky columns.
 *
 *-----------------------------------------------------------------------------*/
//
// 4/25/98, 5/2/98 killough: reformatted, beautified

#include "doomstat.h"
#include "r_main.h"
#include "r_bsp.h"
#include "r_segs.h"
#include "r_plane.h"
#include "r_things.h"
#include "r_draw.h"
#include "w_wad.h"
#include "v_video.h"
#include "lprintf.h"

#include "esp_attr.h"
#include "r_sky_portal.h"

// OPTIMIZE: closed two sided lines as single sided

// killough 1/6/98: replaced globals with statics where appropriate

// True if any of the segs textures might be visible.
static boolean  segtextured;
static boolean  markfloor;      // False if the back side is the same plane.
static boolean  markceiling;
static boolean  maskedtexture;
static int      toptexture;
static int      bottomtexture;
static int      midtexture;

static fixed_t  toptexheight, midtexheight, bottomtexheight; // cph

angle_t         rw_normalangle; // angle to line origin
int             rw_angle1;
fixed_t         rw_distance;

//
// regular wall
//
static int      rw_x;
static int      rw_stopx;
static angle_t  rw_centerangle;
static fixed_t  rw_offset;
static fixed_t  rw_scale;
static fixed_t  rw_scalestep;
static fixed_t  rw_midtexturemid;
static fixed_t  rw_toptexturemid;
static fixed_t  rw_bottomtexturemid;
static int      rw_lightlevel;
static int      worldtop;
static int      worldbottom;
static int      worldhigh;
static int      worldlow;
static fixed_t  pixhigh;
static fixed_t  pixlow;
static fixed_t  pixhighstep;
static fixed_t  pixlowstep;
static fixed_t  topfrac;
static fixed_t  topstep;
static fixed_t  bottomfrac;
static fixed_t  bottomstep;
static int      *maskedtexturecol; // dropoff overflow

//
// R_ScaleFromGlobalAngle
// Returns the texture mapping scale
//  for the current line (horizontal span)
//  at the given angle.
// rw_distance must be calculated first.
//
// killough 5/2/98: reformatted, cleaned up
// CPhipps - moved here from r_main.c

static fixed_t R_ScaleFromGlobalAngle(angle_t visangle)
{
  int     anglea = ANG90 + (visangle-viewangle);
  int     angleb = ANG90 + (visangle-rw_normalangle);
  int     den = FixedMul(rw_distance, finesine[anglea>>ANGLETOFINESHIFT]);
// proff 11/06/98: Changed for high-res
  fixed_t num = FixedMul(projectiony, finesine[angleb>>ANGLETOFINESHIFT]);
  return den > num>>16 ? (num = FixedDiv(num, den)) > 64*FRACUNIT ?
    64*FRACUNIT : num < 256 ? 256 : num : 64*FRACUNIT;
}

//
// R_RenderMaskedSegRange
//

static inline const lighttable_t *R_WallColourMap(int base, fixed_t scale)
{
  if(fixedcolormap)return fixedcolormap;
  int level=base-(FixedMul(scale,pspriteiscale)/2>>LIGHTSCALESHIFT);
  if(level<0)level=0;
  else if(level>=NUMCOLORMAPS)level=NUMCOLORMAPS-1;
  return fullcolormap+level*256;
}

void IRAM_ATTR R_RenderMaskedSegRange(drawseg_t *ds, int x1, int x2)
{
  // A sprite may already have consumed this part of the masked wall. Trim
  // before fetching textures or setting lighting; retain the original x basis.
  while (x1<=x2 && ds->maskedtexturecol[x1]==INT_MAX) ++x1;
  if (x1>x2) {
    curline=NULL;
    return;
  }
  while (ds->maskedtexturecol[x2]==INT_MAX) --x2;
  int      texnum;
  sector_t tempsec;      // killough 4/13/98
  const rpatch_t *patch;
  R_DrawColumn_f colfunc;
  draw_column_vars_t dcvars;
  angle_t angle;

  R_SetDefaultDrawColumnVars(&dcvars);

  // Calculate light table.
  // Use different light tables
  //   for horizontal / vertical / diagonal. Diagonal?

  curline = ds->curline;  // OPTIMIZE: get rid of LIGHTSEGSHIFT globally

  // killough 4/11/98: draw translucent 2s normal textures

  colfunc = R_GetMaskedDrawColumnFunc(drawvars.filterwall,drawvars.filterz);
  if (curline->linedef->tranlump >= 0 && general_translucency)
    {
      colfunc = R_GetDrawColumnFunc(RDC_PIPELINE_TRANSLUCENT, drawvars.filterwall, drawvars.filterz);
      tranmap = main_tranmap;
      if (curline->linedef->tranlump > 0)
        tranmap = W_CacheLumpNum(curline->linedef->tranlump-1);
    }
  // killough 4/11/98: end translucent 2s normal code

  frontsector = curline->frontsector;
  backsector = curline->backsector;

  // cph 2001/11/25 - middle textures did not animate in v1.2
  texnum = curline->sidedef->midtexture;
  if (!comp[comp_maskedanim])
    texnum = texturetranslation[texnum];

  // killough 4/13/98: get correct lightlevel for 2s normal textures
  rw_lightlevel = R_FakeFlat(frontsector, &tempsec, NULL, NULL, false) ->lightlevel;

  maskedtexturecol = ds->maskedtexturecol;

  rw_scalestep = ds->scalestep;
  spryscale = ds->scale1 + (x1 - ds->x1)*rw_scalestep;
  mfloorclip = ds->sprbottomclip;
  mceilingclip = ds->sprtopclip;

  // find positioning
  if (curline->linedef->flags & ML_DONTPEGBOTTOM)
    {
      dcvars.texturemid = frontsector->floorheight > backsector->floorheight
        ? frontsector->floorheight : backsector->floorheight;
      dcvars.texturemid = dcvars.texturemid + textureheight[texnum] - viewz;
    }
  else
    {
      dcvars.texturemid =frontsector->ceilingheight<backsector->ceilingheight
        ? frontsector->ceilingheight : backsector->ceilingheight;
      dcvars.texturemid = dcvars.texturemid - viewz;
    }

  dcvars.texturemid += curline->sidedef->rowoffset;

  if (fixedcolormap) {
    dcvars.colormap = fixedcolormap;
    dcvars.nextcolormap = dcvars.colormap; // for filtering -- POPE
  }

  patch = R_CacheTextureCompositePatchNum(texnum);

  const int base0=fixedcolormap ? 0 : R_ColourMapBase(rw_lightlevel);
  const int base1=fixedcolormap || drawvars.filterz==RDRAW_FILTER_POINT ? base0 :
      R_ColourMapBase(rw_lightlevel+1);
  // draw the columns
  for (dcvars.x = x1 ; dcvars.x <= x2 ; dcvars.x++, spryscale += rw_scalestep)
    if (maskedtexturecol[dcvars.x] != INT_MAX) // dropoff overflow
      {
        if(mceilingclip[dcvars.x]>=mfloorclip[dcvars.x]-1) {
          maskedtexturecol[dcvars.x]=INT_MAX;
          continue;
        }
        {
          int_64_t t = ((int_64_t) centeryfrac << FRACBITS) -
            (int_64_t) dcvars.texturemid * spryscale;
          if (t + (int_64_t) textureheight[texnum] * spryscale < 0 ||
              t > (int_64_t) MAX_SCREENHEIGHT << FRACBITS*2)
          {
            maskedtexturecol[dcvars.x]=INT_MAX;
            continue;
          }
          sprtopscreen = (long)(t >> FRACBITS);
        }
        // calculate texture offset - POPE
        angle = (ds->rw_centerangle + xtoviewangle[dcvars.x]) >> ANGLETOFINESHIFT;
        dcvars.texu = ds->rw_offset - FixedMul(finetangent[angle], ds->rw_distance);
        if (drawvars.filterwall == RDRAW_FILTER_LINEAR)
          dcvars.texu -= (FRACUNIT>>1);

        if (!fixedcolormap)
          dcvars.z = spryscale; // for filtering -- POPE
        dcvars.colormap=R_WallColourMap(base0,spryscale);
        dcvars.nextcolormap=drawvars.filterz==RDRAW_FILTER_POINT ? dcvars.colormap :
            R_WallColourMap(base1,spryscale);

        // killough 3/2/98:
        //
        // This calculation used to overflow and cause crashes in Doom:
        //
        // sprtopscreen = centeryfrac - FixedMul(dcvars.texturemid, spryscale);
        //
        // This code fixes it, by using double-precision intermediate
        // arithmetic and by skipping the drawing of 2s normals whose
        // mapping to screen coordinates is totally out of range:

        dcvars.iscale = 0xffffffffu / (unsigned) spryscale;

        // killough 1/25/98: here's where Medusa came in, because
        // it implicitly assumed that the column was all one patch.
        // Originally, Doom did not construct complete columns for
        // multipatched textures, so there were no header or trailer
        // bytes in the column referred to below, which explains
        // the Medusa effect. The fix is to construct true columns
        // when forming multipatched textures (see r_data.c).

        // Point sampling and slope metadata use only the current source column.
        const rcolumn_t *column=R_GetPatchColumnWrapped(patch,maskedtexturecol[dcvars.x]);
        const rcolumn_t *previous=column,*next=column;
        if(drawvars.filterwall!=RDRAW_FILTER_POINT) {
          previous=R_GetPatchColumnWrapped(patch,maskedtexturecol[dcvars.x]-1);
          next=R_GetPatchColumnWrapped(patch,maskedtexturecol[dcvars.x]+1);
        }
        R_DrawMaskedColumn(patch,colfunc,&dcvars,column,previous,next);
        maskedtexturecol[dcvars.x] = INT_MAX; // dropoff overflow
      }

  // Except for main_tranmap, mark others purgable at this point
  if (curline->linedef->tranlump > 0 && general_translucency)
    W_UnlockLumpNum(curline->linedef->tranlump-1); // cph - unlock it

  R_UnlockTextureCompositePatchNum(texnum);

  curline = NULL; /* cph 2001/11/18 - must clear curline now we're done with it, so R_ColourMap doesn't try using it for other things */
}

//
// R_RenderSegLoop
// Draws zero, one, or two textures (and possibly a masked texture) for walls.
// Can draw or mark the starting pixel of floor and ceiling textures.
// CALLED: CORE LOOPING ROUTINE.
//

#define HEIGHTBITS 12
#define HEIGHTUNIT (1<<HEIGHTBITS)

static int didsolidcol; /* True if at least one column was marked solid */

// Masked-only columns need U coordinates, but their lighting/scaling is set up
// later by the masked renderer. Opaque sampling setup is shared across tiers.
static inline fixed_t R_WallTextureU(int x)
{
  angle_t angle=(rw_centerangle+xtoviewangle[x])>>ANGLETOFINESHIFT;
  fixed_t u=rw_offset-FixedMul(finetangent[angle],rw_distance);
  if(drawvars.filterwall==RDRAW_FILTER_LINEAR)u-=(FRACUNIT>>1);
  return u;
}

static int wall_lightbase[2];

static inline fixed_t R_PrepareOpaqueWallColumn(draw_column_vars_t *dcvars)
{
  dcvars->texu=R_WallTextureU(rw_x);
  dcvars->colormap=R_WallColourMap(wall_lightbase[0],rw_scale);
  dcvars->nextcolormap=drawvars.filterz==RDRAW_FILTER_POINT ? dcvars->colormap :
      R_WallColourMap(wall_lightbase[1],rw_scale);
  dcvars->z=rw_scale;
  dcvars->x=rw_x;
  dcvars->iscale=0xffffffffu/(unsigned)rw_scale;
  return dcvars->texu>>FRACBITS;
}

// Visibility-only portals require neither texture dispatch nor sampling state.
// Keep the same clip/plane updates and fixed-point stepping as the general loop.
static void R_RenderPortalLoop(void)
{
  int x=rw_x;
  const int stop=rw_stopx, n=stop-x;
  const int mc=markceiling, mf=markfloor;
  fixed_t tf=topfrac, bf=bottomfrac;
  const fixed_t ts=topstep, bs=bottomstep;
  visplane_t *const cp=ceilingplane, *const fp=floorplane;
  ++rendered_segs;
  if(mc || mf) for(;x<stop;++x) {
    int yh=bf>>HEIGHTBITS;
    int yl=(tf+HEIGHTUNIT-1)>>HEIGHTBITS;
    int top=ceilingclip[x]+1, bottom;
    if(yl<top)yl=top;
    if(mc) {
      bottom=yl-1;
      if(bottom>=floorclip[x])bottom=floorclip[x]-1;
      if(top<=bottom)R_SetPlaneColumn(cp,x,top,bottom);
      ceilingclip[x]=bottom;
    }
    bottom=floorclip[x]-1;
    if(yh>bottom)yh=bottom;
    if(mf) {
      top=yh<ceilingclip[x] ? ceilingclip[x] : yh;
      if(++top<=bottom)R_SetPlaneColumn(fp,x,top,bottom);
      floorclip[x]=top;
    }
    // The untextured branch performs these final assignments even if the
    // window has closed. Preserve that detail rather than simplifying clamps.
    if(mc)ceilingclip[x]=yl-1;
    if(mf)floorclip[x]=yh+1;
    if(floorclip[x]<=ceilingclip[x]+1) {solidcol[x]=1;didsolidcol=1;}
    tf+=ts;bf+=bs;
  }
  // Modulo-32-bit stepping is exactly the repeated wrapping additions, also
  // when a completely inactive span can be skipped in constant time.
  rw_scale=(fixed_t)((uint32_t)rw_scale+(uint32_t)rw_scalestep*(unsigned)n);
  topfrac=(fixed_t)((uint32_t)topfrac+(uint32_t)ts*(unsigned)n);
  bottomfrac=(fixed_t)((uint32_t)bottomfrac+(uint32_t)bs*(unsigned)n);
  rw_x=stop;
}

static void IRAM_ATTR R_RenderSegLoop (void)
{
  const rpatch_t *tex_patch;
  const byte *mid_pixels=NULL, *top_pixels=NULL, *bottom_pixels=NULL;
  // Pin only tiers actually drawn; allocation of another tier cannot evict them.
  // Keep separate balanced locks even when tiers share the same texture ID.
  const rpatch_t *mid_patch = NULL, *top_patch = NULL, *bottom_patch = NULL;
  R_DrawColumn_f colfunc = R_GetWallDrawColumnFunc();
  draw_column_vars_t dcvars;
  fixed_t  texturecolumn = 0;   // shut up compiler warning

  R_SetDefaultDrawColumnVars(&dcvars);
  if(segtextured) {
    wall_lightbase[0]=R_ColourMapBase(rw_lightlevel);
    if(drawvars.filterz!=RDRAW_FILTER_POINT)
      wall_lightbase[1]=R_ColourMapBase(rw_lightlevel+1);
  }

  rendered_segs++;
  for ( ; rw_x < rw_stopx ; rw_x++)
    {

      int wall_ready=0;

       // mark floor / ceiling areas

      int yh = bottomfrac>>HEIGHTBITS;
      int yl = (topfrac+HEIGHTUNIT-1)>>HEIGHTBITS;

      // no space above wall?
      int bottom,top = ceilingclip[rw_x]+1;

      if (yl < top)
        yl = top;

      if (markceiling)
        {
          bottom = yl-1;

          if (bottom >= floorclip[rw_x])
            bottom = floorclip[rw_x]-1;

          if (top <= bottom)
            {
              R_SetPlaneColumn(ceilingplane,rw_x,top,bottom);
            }
          // SoM: this should be set here
          ceilingclip[rw_x] = bottom;
        }

//      yh = bottomfrac>>HEIGHTBITS;

      bottom = floorclip[rw_x]-1;
      if (yh > bottom)
        yh = bottom;

      if (markfloor)
        {

          top  = yh < ceilingclip[rw_x] ? ceilingclip[rw_x] : yh;

          if (++top <= bottom)
            {
              R_SetPlaneColumn(floorplane,rw_x,top,bottom);
            }
          // SoM: This should be set here to prevent overdraw
          floorclip[rw_x] = top;
        }

      // draw the wall tiers
      if (midtexture)
        {

          if(yh>=yl) {
            texturecolumn=R_PrepareOpaqueWallColumn(&dcvars);wall_ready=1;
          dcvars.yl = yl;     // single sided line
          dcvars.yh = yh;
          dcvars.texturemid = rw_midtexturemid;
          if (!mid_patch) {
            mid_patch = R_CacheTextureCompositePatchNum(midtexture);
            if (mid_patch->isFlashComposite) mid_pixels=R_CacheFlashTexturePixels(mid_patch);
          }
          tex_patch = mid_patch;
          dcvars.source = R_GetWallTextureColumn(tex_patch,texturecolumn,mid_pixels);
          if (drawvars.filterwall == RDRAW_FILTER_POINT) {
            dcvars.prevsource = dcvars.nextsource = dcvars.source;
          } else
          {
            dcvars.prevsource = R_GetTextureColumn(tex_patch, texturecolumn-1);
            dcvars.nextsource = R_GetTextureColumn(tex_patch, texturecolumn+1);
          }
          dcvars.texheight = midtexheight;
          colfunc (&dcvars);
          tex_patch = NULL;
          }
          ceilingclip[rw_x] = viewheight;
          floorclip[rw_x] = -1;
        }
      else
        {

          // two sided line
          if (toptexture)
            {
              // top wall
              int mid = pixhigh>>HEIGHTBITS;
              pixhigh += pixhighstep;

              if (mid >= floorclip[rw_x])
                mid = floorclip[rw_x]-1;

              if (mid >= yl)
                {
                  if(!wall_ready) {
                    texturecolumn=R_PrepareOpaqueWallColumn(&dcvars);wall_ready=1;
                  }
                  dcvars.yl = yl;
                  dcvars.yh = mid;
                  dcvars.texturemid = rw_toptexturemid;
                  if (!top_patch) {
                    top_patch = R_CacheTextureCompositePatchNum(toptexture);
                    if (top_patch->isFlashComposite) top_pixels=R_CacheFlashTexturePixels(top_patch);
                  }
                  tex_patch = top_patch;
                  dcvars.source = R_GetWallTextureColumn(tex_patch,texturecolumn,top_pixels);
                  if (drawvars.filterwall == RDRAW_FILTER_POINT) {
                    dcvars.prevsource = dcvars.nextsource = dcvars.source;
                  } else
                  {
                    dcvars.prevsource = R_GetTextureColumn(tex_patch, texturecolumn-1);
                    dcvars.nextsource = R_GetTextureColumn(tex_patch, texturecolumn+1);
                  }
                  dcvars.texheight = toptexheight;
          colfunc (&dcvars);
                  tex_patch = NULL;
                  ceilingclip[rw_x] = mid;
                }
              else
                ceilingclip[rw_x] = yl-1;
            }
          else  // no top wall
            {

            if (markceiling)
              ceilingclip[rw_x] = yl-1;
             }

          if (bottomtexture)          // bottom wall
            {
              int mid = (pixlow+HEIGHTUNIT-1)>>HEIGHTBITS;
              pixlow += pixlowstep;

              // no space above wall?
              if (mid <= ceilingclip[rw_x])
                mid = ceilingclip[rw_x]+1;

              if (mid <= yh)
                {
                  if(!wall_ready) {
                    texturecolumn=R_PrepareOpaqueWallColumn(&dcvars);wall_ready=1;
                  }
                  dcvars.yl = mid;
                  dcvars.yh = yh;
                  dcvars.texturemid = rw_bottomtexturemid;
                  if (!bottom_patch) {
                    bottom_patch = R_CacheTextureCompositePatchNum(bottomtexture);
                    if (bottom_patch->isFlashComposite) bottom_pixels=R_CacheFlashTexturePixels(bottom_patch);
                  }
                  tex_patch = bottom_patch;
                  dcvars.source = R_GetWallTextureColumn(tex_patch,texturecolumn,bottom_pixels);
                  if (drawvars.filterwall == RDRAW_FILTER_POINT) {
                    dcvars.prevsource = dcvars.nextsource = dcvars.source;
                  } else
                  {
                    dcvars.prevsource = R_GetTextureColumn(tex_patch, texturecolumn-1);
                    dcvars.nextsource = R_GetTextureColumn(tex_patch, texturecolumn+1);
                  }
                  dcvars.texheight = bottomtexheight;
          colfunc (&dcvars);
                  tex_patch = NULL;
                  floorclip[rw_x] = mid;
                }
              else
                floorclip[rw_x] = yh+1;
            }
          else        // no bottom wall
            {
            if (markfloor)
              floorclip[rw_x] = yh+1;
            }

    // cph - if we completely blocked further sight through this column,
    // add this info to the solid columns array for r_bsp.c
    if ((markceiling || markfloor) &&
        (floorclip[rw_x] <= ceilingclip[rw_x] + 1)) {
      solidcol[rw_x] = 1; didsolidcol = 1;
    }

          // save texturecol for backdrawing of masked mid texture
          if (maskedtexture) {
            if(!wall_ready)texturecolumn=R_WallTextureU(rw_x)>>FRACBITS;
            maskedtexturecol[rw_x] = texturecolumn;
          }
        }

      rw_scale += rw_scalestep;
      topfrac += topstep;
      bottomfrac += bottomstep;
    }
  if (mid_patch) R_UnlockTextureCompositePatchNum(midtexture);
  if (top_patch) R_UnlockTextureCompositePatchNum(toptexture);
  if (bottom_patch) R_UnlockTextureCompositePatchNum(bottomtexture);

}

// killough 5/2/98: move from r_main.c, made static, simplified

static fixed_t R_PointToDist(fixed_t x, fixed_t y)
{
  fixed_t dx = D_abs(x - viewx);
  fixed_t dy = D_abs(y - viewy);

  if (dy > dx)
    {
      fixed_t t = dx;
      dx = dy;
      dy = t;
    }

  return FixedDiv(dx, finesine[(tantoangle[FixedDiv(dy,dx) >> DBITS]
                                + ANG90) >> ANGLETOFINESHIFT]);
}

#include <assert.h>
static wall_distance_entry_t *view_distances;
void R_SetWallDistanceCache(wall_distance_entry_t *entries)
{
  assert(!entries || !view_distances);
  view_distances=entries;
}
static inline fixed_t R_WallVertexDistance(const vertex_t *vertex)
{
  if(view_distances) {
    unsigned slot=((uintptr_t)vertex>>3)&(WALL_DISTANCE_SLOTS-1);
    wall_distance_entry_t *entry=&view_distances[slot];
    if(entry->vertex==vertex) {
      return entry->distance;
    }
    entry->distance=R_PointToDist(vertex->x,vertex->y);
    entry->vertex=vertex;
    return entry->distance;
  }
  return R_PointToDist(vertex->x,vertex->y);
}

//
// R_StoreWallRange
// A wall segment will be drawn
//  between start and stop pixels (inclusive).
//
void R_StoreWallRange(const int start, const int stop)
{
  fixed_t hyp;
  angle_t offsetangle;

  if (ds_p == drawsegs+maxdrawsegs)   // killough 1/98 -- fix 2s line HOM
    {
      unsigned pos = ds_p - drawsegs; // jff 8/9/98 fix from ZDOOM1.14a
      unsigned newmax = maxdrawsegs ? maxdrawsegs*2 : 128; // killough
      drawsegs = realloc(drawsegs,newmax*sizeof(*drawsegs));
      ds_p = drawsegs + pos;          // jff 8/9/98 fix from ZDOOM1.14a
      maxdrawsegs = newmax;
    }

  if(curline->miniseg == false) // figgi -- skip minisegs
    curline->linedef->flags |= ML_MAPPED;

#ifdef GL_DOOM
  if (V_GetMode() == VID_MODEGL)
  {
    // proff 11/99: the rest of the calculations is not needed for OpenGL
    ds_p++->curline = curline;
    gld_AddWall(curline);

    return;
  }
#endif

#ifdef RANGECHECK
  if (start >=viewwidth || start > stop)
    I_Error ("Bad R_RenderWallRange: %i to %i", start , stop);
#endif

  sidedef = curline->sidedef;
  linedef = curline->linedef;

  if(R_IsEmptySkyPortal(frontsector,backsector,sidedef,linedef->r_flags,viewz,skyflatnum)) {
    // The caller already clipped this range horizontally, so preserve mapping.
    // Its discarded drawseg would have no silhouette or masked columns.
    linedef->flags|=ML_MAPPED;
    ++rendered_segs;
    return;
  }

  // mark the segment as visible for auto map
  linedef->flags |= ML_MAPPED;

  // calculate rw_distance for scale calculation
  rw_normalangle = curline->angle + ANG90;

  offsetangle = rw_normalangle-rw_angle1;

  if (D_abs(offsetangle) > ANG90)
    offsetangle = ANG90;

  hyp = (viewx==curline->v1->x && viewy==curline->v1->y)?
    0 : R_WallVertexDistance(curline->v1);
  rw_distance = FixedMul(hyp, finecosine[offsetangle>>ANGLETOFINESHIFT]);

  ds_p->x1 = rw_x = start;
  ds_p->x2 = stop;
  ds_p->curline = curline;
  rw_stopx = stop+1;

  {     // killough 1/6/98, 2/1/98: remove limit on openings
    extern int *openings; // dropoff overflow
    extern size_t maxopenings;
    size_t pos = lastopening - openings;
    size_t need = (rw_stopx - start)*4 + pos;
    if (need > maxopenings)
      {
        drawseg_t *ds;                //jff 8/9/98 needed for fix from ZDoom
        int *oldopenings = openings; // dropoff overflow
        int *oldlast = lastopening; // dropoff overflow

        do
          maxopenings = maxopenings ? maxopenings*2 : 16384;
        while (need > maxopenings);
        openings = realloc(openings, maxopenings * sizeof(*openings));
        lastopening = openings + pos;

      // jff 8/9/98 borrowed fix for openings from ZDOOM1.14
      // [RH] We also need to adjust the openings pointers that
      //    were already stored in drawsegs.
      for (ds = drawsegs; ds < ds_p; ds++)
        {
#define ADJUST(p) if (ds->p + ds->x1 >= oldopenings && ds->p + ds->x1 <= oldlast)\
            ds->p = ds->p - oldopenings + openings;
          ADJUST (maskedtexturecol);
          ADJUST (sprtopclip);
          ADJUST (sprbottomclip);
        }
#undef ADJUST
      }
  }  // killough: end of code to remove limits on openings

  // calculate scale at both ends and step

  ds_p->scale1 = rw_scale =
    R_ScaleFromGlobalAngle (viewangle + xtoviewangle[start]);

  if (stop > start)
    {
      ds_p->scale2 = R_ScaleFromGlobalAngle (viewangle + xtoviewangle[stop]);
      ds_p->scalestep = rw_scalestep = (ds_p->scale2-rw_scale) / (stop-start);
    }
  else
    ds_p->scale2 = ds_p->scale1;

  // calculate texture boundaries
  //  and decide if floor / ceiling marks are needed

  worldtop = frontsector->ceilingheight - viewz;
  worldbottom = frontsector->floorheight - viewz;

  midtexture = toptexture = bottomtexture = maskedtexture = 0;
  ds_p->maskedtexturecol = NULL;

  if (!backsector)
    {
      // single sided line
      midtexture = texturetranslation[sidedef->midtexture];
      midtexheight = (linedef->r_flags & RF_MID_TILE) ? 0 : textureheight[midtexture] >> FRACBITS;

      // a single sided line is terminal, so it must mark ends
      markfloor = markceiling = true;

      if (linedef->flags & ML_DONTPEGBOTTOM)
        {         // bottom of texture at bottom
          fixed_t vtop = frontsector->floorheight +
            textureheight[sidedef->midtexture];
          rw_midtexturemid = vtop - viewz;
        }
      else        // top of texture at top
        rw_midtexturemid = worldtop;

      rw_midtexturemid += FixedMod(sidedef->rowoffset, textureheight[midtexture]);

      ds_p->silhouette = SIL_BOTH;
      ds_p->sprtopclip = screenheightarray;
      ds_p->sprbottomclip = negonearray;
      ds_p->bsilheight = INT_MAX;
      ds_p->tsilheight = INT_MIN;
    }
  else      // two sided line
    {
      ds_p->sprtopclip = ds_p->sprbottomclip = NULL;
      ds_p->silhouette = 0;

      if (linedef->r_flags & RF_CLOSED) { /* cph - closed 2S line e.g. door */
  // cph - killough's (outdated) comment follows - this deals with both
  // "automap fixes", his and mine
  // killough 1/17/98: this test is required if the fix
  // for the automap bug (r_bsp.c) is used, or else some
  // sprites will be displayed behind closed doors. That
  // fix prevents lines behind closed doors with dropoffs
  // from being displayed on the automap.

  ds_p->silhouette = SIL_BOTH;
  ds_p->sprbottomclip = negonearray;
  ds_p->bsilheight = INT_MAX;
  ds_p->sprtopclip = screenheightarray;
  ds_p->tsilheight = INT_MIN;

      } else { /* not solid - old code */

  if (frontsector->floorheight > backsector->floorheight)
    {
      ds_p->silhouette = SIL_BOTTOM;
      ds_p->bsilheight = frontsector->floorheight;
    }
  else
    if (backsector->floorheight > viewz)
      {
        ds_p->silhouette = SIL_BOTTOM;
        ds_p->bsilheight = INT_MAX;
      }

  if (frontsector->ceilingheight < backsector->ceilingheight)
    {
      ds_p->silhouette |= SIL_TOP;
      ds_p->tsilheight = frontsector->ceilingheight;
    }
  else
    if (backsector->ceilingheight < viewz)
      {
        ds_p->silhouette |= SIL_TOP;
        ds_p->tsilheight = INT_MIN;
      }
      }

      worldhigh = backsector->ceilingheight - viewz;
      worldlow = backsector->floorheight - viewz;

      // hack to allow height changes in outdoor areas
      if (frontsector->ceilingpic == skyflatnum
          && backsector->ceilingpic == skyflatnum)
        worldtop = worldhigh;

      markfloor = worldlow != worldbottom
        || backsector->floorpic != frontsector->floorpic
        || backsector->lightlevel != frontsector->lightlevel

        // killough 3/7/98: Add checks for (x,y) offsets
        || backsector->floor_xoffs != frontsector->floor_xoffs
        || backsector->floor_yoffs != frontsector->floor_yoffs

        // killough 4/15/98: prevent 2s normals
        // from bleeding through deep water
        || frontsector->heightsec != -1

        // killough 4/17/98: draw floors if different light levels
        || backsector->floorlightsec != frontsector->floorlightsec
        ;

      markceiling = worldhigh != worldtop
        || backsector->ceilingpic != frontsector->ceilingpic
        || backsector->lightlevel != frontsector->lightlevel

        // killough 3/7/98: Add checks for (x,y) offsets
        || backsector->ceiling_xoffs != frontsector->ceiling_xoffs
        || backsector->ceiling_yoffs != frontsector->ceiling_yoffs

        // killough 4/15/98: prevent 2s normals
        // from bleeding through fake ceilings
        || (frontsector->heightsec != -1 &&
            frontsector->ceilingpic!=skyflatnum)

        // killough 4/17/98: draw ceilings if different light levels
        || backsector->ceilinglightsec != frontsector->ceilinglightsec
        ;

      if (backsector->ceilingheight <= frontsector->floorheight
          || backsector->floorheight >= frontsector->ceilingheight)
        markceiling = markfloor = true;   // closed door

      if (worldhigh < worldtop)   // top texture
        {
          toptexture = texturetranslation[sidedef->toptexture];
    toptexheight = (linedef->r_flags & RF_TOP_TILE) ? 0 : textureheight[toptexture] >> FRACBITS;
          rw_toptexturemid = linedef->flags & ML_DONTPEGTOP ? worldtop :
            backsector->ceilingheight+textureheight[sidedef->toptexture]-viewz;
    rw_toptexturemid += FixedMod(sidedef->rowoffset, textureheight[toptexture]);
        }

      if (worldlow > worldbottom) // bottom texture
        {
          bottomtexture = texturetranslation[sidedef->bottomtexture];
    bottomtexheight = (linedef->r_flags & RF_BOT_TILE) ? 0 : textureheight[bottomtexture] >> FRACBITS;
          rw_bottomtexturemid = linedef->flags & ML_DONTPEGBOTTOM ? worldtop :
            worldlow;
    rw_bottomtexturemid += FixedMod(sidedef->rowoffset, textureheight[bottomtexture]);
        }

      // allocate space for masked texture tables
      if (sidedef->midtexture)    // masked midtexture
        {
          maskedtexture = true;
          ds_p->maskedtexturecol = maskedtexturecol = lastopening - rw_x;
          lastopening += rw_stopx - rw_x;
        }
    }

  // calculate rw_offset (only needed for textured lines)
  segtextured = midtexture | toptexture | bottomtexture | maskedtexture;

  if (segtextured)
    {
      rw_offset = FixedMul (hyp, -finesine[offsetangle >>ANGLETOFINESHIFT]);

      rw_offset += sidedef->textureoffset + curline->offset;

      rw_centerangle = ANG90 + viewangle - rw_normalangle;

      rw_lightlevel = frontsector->lightlevel;
    }

  // Remember the vars used to determine fractional U texture
  // coords for later - POPE
  ds_p->rw_offset = rw_offset;
  ds_p->rw_distance = rw_distance;
  ds_p->rw_centerangle = rw_centerangle;
      
  // if a floor / ceiling plane is on the wrong side of the view
  // plane, it is definitely invisible and doesn't need to be marked.

  // killough 3/7/98: add deep water check
  if (frontsector->heightsec == -1)
    {
      if (frontsector->floorheight >= viewz)       // above view plane
        markfloor = false;
      if (frontsector->ceilingheight <= viewz &&
          frontsector->ceilingpic != skyflatnum)   // below view plane
        markceiling = false;
    }

  // calculate incremental stepping values for texture edges
  worldtop >>= 4;
  worldbottom >>= 4;

  topstep = -FixedMul (rw_scalestep, worldtop);
  topfrac = (centeryfrac>>4) - FixedMul (worldtop, rw_scale);

  bottomstep = -FixedMul (rw_scalestep,worldbottom);
  bottomfrac = (centeryfrac>>4) - FixedMul (worldbottom, rw_scale);

  if (backsector)
    {
      worldhigh >>= 4;
      worldlow >>= 4;

      if (worldhigh < worldtop)
        {
          pixhigh = (centeryfrac>>4) - FixedMul (worldhigh, rw_scale);
          pixhighstep = -FixedMul (rw_scalestep,worldhigh);
        }
      if (worldlow > worldbottom)
        {
          pixlow = (centeryfrac>>4) - FixedMul (worldlow, rw_scale);
          pixlowstep = -FixedMul (rw_scalestep,worldlow);
        }
    }

  // render it
  if (markceiling) {
    if (ceilingplane)   // killough 4/11/98: add NULL ptr checks
      ceilingplane = R_CheckPlane (ceilingplane, rw_x, rw_stopx-1);
    else
      markceiling = 0;
  }

  if (markfloor) {
    if (floorplane)     // killough 4/11/98: add NULL ptr checks
      /* cph 2003/04/18  - ceilingplane and floorplane might be the same
       * visplane (e.g. if both skies); R_CheckPlane doesn't know about
       * modifications to the plane that might happen in parallel with the check
       * being made, so we have to override it and split them anyway if that is
       * a possibility, otherwise the floor marking would overwrite the ceiling
       * marking, resulting in HOM. */
      if (markceiling && ceilingplane == floorplane)
	floorplane = R_DupPlane (floorplane, rw_x, rw_stopx-1);
      else
	floorplane = R_CheckPlane (floorplane, rw_x, rw_stopx-1);
    else
      markfloor = 0;
  }

  didsolidcol = 0;
  if(!segtextured)R_RenderPortalLoop();
  else
  R_RenderSegLoop();

  /* cph - if a column was made solid by this wall, we _must_ save full clipping info */
  if (backsector && didsolidcol) {
    if (!(ds_p->silhouette & SIL_BOTTOM)) {
      ds_p->silhouette |= SIL_BOTTOM;
      ds_p->bsilheight = backsector->floorheight;
    }
    if (!(ds_p->silhouette & SIL_TOP)) {
      ds_p->silhouette |= SIL_TOP;
      ds_p->tsilheight = backsector->ceilingheight;
    }
  }

  // save sprite clipping info
  if ((ds_p->silhouette & SIL_TOP || maskedtexture) && !ds_p->sprtopclip)
    {
      memcpy (lastopening, ceilingclip+start, sizeof(int)*(rw_stopx-start)); // dropoff overflow
      ds_p->sprtopclip = lastopening - start;
      lastopening += rw_stopx - start;
    }
  if ((ds_p->silhouette & SIL_BOTTOM || maskedtexture) && !ds_p->sprbottomclip)
    {
      memcpy (lastopening, floorclip+start, sizeof(int)*(rw_stopx-start)); // dropoff overflow
      ds_p->sprbottomclip = lastopening - start;
      lastopening += rw_stopx - start;
    }
  if (maskedtexture && !(ds_p->silhouette & SIL_TOP))
    {
      ds_p->silhouette |= SIL_TOP;
      ds_p->tsilheight = INT_MIN;
    }
  if (maskedtexture && !(ds_p->silhouette & SIL_BOTTOM))
    {
      ds_p->silhouette |= SIL_BOTTOM;
      ds_p->bsilheight = INT_MAX;
    }
  ds_p++;
}
