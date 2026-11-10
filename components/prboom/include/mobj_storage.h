/* GPL-2.0-or-later. Derived actor references and legacy save adapters. */
#ifndef DOOM_MOBJ_STORAGE_H
#define DOOM_MOBJ_STORAGE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "lprintf.h"
static inline __attribute__((always_inline)) mobjinfo_t *P_MobjInfo(const mobj_t *mo) {
  return &mobjinfo[mo->type];
}
static inline __attribute__((always_inline)) state_t *P_MobjState(const mobj_t *mo) {
  return mo->state_index==65535 ? NULL : &states[mo->state_index];
}
static inline void P_SetMobjStateRef(mobj_t *mo,state_t *st) {
  if(st && ((uintptr_t)st<(uintptr_t)states ||
       (uintptr_t)st>=(uintptr_t)(states+NUMSTATES) ||
       ((uintptr_t)st-(uintptr_t)states)%sizeof(*st)))
    I_Error("Compact actor state outside table");
  mo->state_index=st ? st-states : 65535;
}
static inline void P_SetMobjSprite(mobj_t *mo,int sprite) {
  if((unsigned)sprite>=NUMSPRITES)I_Error("Compact actor sprite outside table");
  mo->sprite=sprite;
}
static inline void P_MobjToSave(mobj_save_t *dst,const mobj_t *src) {
  memset(dst,0,sizeof(*dst));
  dst->thinker=src->thinker;
  dst->x=src->x;
  dst->y=src->y;
  dst->z=src->z;
  dst->snext=src->snext;
  dst->sprev=src->sprev;
  dst->angle=src->angle;
  dst->frame=src->frame;
  dst->bnext=src->bnext;
  dst->bprev=src->bprev;
  dst->subsector=src->subsector;
  dst->floorz=src->floorz;
  dst->ceilingz=src->ceilingz;
  dst->dropoffz=src->dropoffz;
  dst->radius=src->radius;
  dst->height=src->height;
  dst->momx=src->momx;
  dst->momy=src->momy;
  dst->momz=src->momz;
  dst->validcount=src->validcount;
  dst->tics=src->tics;
  dst->flags=src->flags;
  dst->intflags=src->intflags;
  dst->health=src->health;
  dst->movedir=src->movedir;
  dst->movecount=src->movecount;
  dst->strafecount=src->strafecount;
  dst->target=src->target;
  dst->reactiontime=src->reactiontime;
  dst->threshold=src->threshold;
  dst->pursuecount=src->pursuecount;
  dst->gear=src->gear;
  dst->player=src->player;
  dst->lastlook=src->lastlook;
  dst->spawnpoint=src->spawnpoint;
  dst->tracer=src->tracer;
  dst->lastenemy=src->lastenemy;
  dst->friction=src->friction;
  dst->movefactor=src->movefactor;
  dst->touching_sectorlist=src->touching_sectorlist;
  dst->PrevX=src->PrevX;
  dst->PrevY=src->PrevY;
  dst->PrevZ=src->PrevZ;
  dst->type=src->type;dst->sprite=src->sprite;
  dst->state=P_MobjState(src);dst->info=P_MobjInfo(src);
}
static inline void P_MobjFromSave(mobj_t *dst,const mobj_save_t *src) {
  if((unsigned)src->type>=NUMMOBJTYPES || (unsigned)src->sprite>=NUMSPRITES)
    I_Error("Compact actor save IDs outside tables");
  memset(dst,0,sizeof(*dst));
  dst->thinker=src->thinker;
  dst->x=src->x;
  dst->y=src->y;
  dst->z=src->z;
  dst->snext=src->snext;
  dst->sprev=src->sprev;
  dst->angle=src->angle;
  dst->frame=src->frame;
  dst->bnext=src->bnext;
  dst->bprev=src->bprev;
  dst->subsector=src->subsector;
  dst->floorz=src->floorz;
  dst->ceilingz=src->ceilingz;
  dst->dropoffz=src->dropoffz;
  dst->radius=src->radius;
  dst->height=src->height;
  dst->momx=src->momx;
  dst->momy=src->momy;
  dst->momz=src->momz;
  dst->validcount=src->validcount;
  dst->tics=src->tics;
  dst->flags=src->flags;
  dst->intflags=src->intflags;
  dst->health=src->health;
  dst->movedir=src->movedir;
  dst->movecount=src->movecount;
  dst->strafecount=src->strafecount;
  dst->target=src->target;
  dst->reactiontime=src->reactiontime;
  dst->threshold=src->threshold;
  dst->pursuecount=src->pursuecount;
  dst->gear=src->gear;
  dst->player=src->player;
  dst->lastlook=src->lastlook;
  dst->spawnpoint=src->spawnpoint;
  dst->tracer=src->tracer;
  dst->lastenemy=src->lastenemy;
  dst->friction=src->friction;
  dst->movefactor=src->movefactor;
  dst->touching_sectorlist=src->touching_sectorlist;
  dst->PrevX=src->PrevX;
  dst->PrevY=src->PrevY;
  dst->PrevZ=src->PrevZ;
  dst->type=src->type;P_SetMobjSprite(dst,src->sprite);
  P_SetMobjStateRef(dst,src->state);
}
#endif
