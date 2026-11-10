/* GPL-2.0-or-later. Conservative no-output classification for sky portals.
 * These conditions imply zero wall textures, zero plane marks and zero sprite
 * silhouette under R_StoreWallRange's original rules. Sector inputs must be the
 * effective front/back sectors after R_FakeFlat. No state is cached by tic.
 */
#ifndef DOOM_R_SKY_PORTAL_H
#define DOOM_R_SKY_PORTAL_H
static inline int R_IsEmptySkyPortal(const sector_t *front, const sector_t *back,
                                    const side_t *side, int flags,
                                    fixed_t eyez, int sky)
{
  return back && !(flags&RF_CLOSED) && !side->midtexture &&
      front->heightsec==-1 && front->ceilingpic==sky && back->ceilingpic==sky &&
      front->floorheight==back->floorheight && front->floorheight<back->ceilingheight &&
      front->floorheight<=eyez &&
      front->ceilingheight>=back->ceilingheight && back->ceilingheight>=eyez &&
      front->floorpic==back->floorpic && front->lightlevel==back->lightlevel &&
      front->floor_xoffs==back->floor_xoffs && front->floor_yoffs==back->floor_yoffs &&
      front->ceiling_xoffs==back->ceiling_xoffs && front->ceiling_yoffs==back->ceiling_yoffs &&
      front->floorlightsec==back->floorlightsec &&
      front->ceilinglightsec==back->ceilinglightsec;
}
#endif
