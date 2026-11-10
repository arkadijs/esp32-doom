/* GPL-2.0-or-later. Relocatable, read-only offline wall composites.
 * TXF1 stores little-endian offsets; native pointers are constructed only in
 * purgeable RAM column views. Pixels and posts remain in mapped flash. */
#include "doomstat.h"
#include "r_data.h"
#include "r_patch.h"
#include "w_wad.h"
#include "z_zone.h"
#include "i_system.h"
#include "lprintf.h"
#include "r_draw.h"
#include "r_main.h"
#include "v_video.h"
#include "esp_heap_caps.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const byte *flash_data;
static uint32_t flash_size;
#define FLASH_TEXTURE_CACHE_KB 128
#define FLASH_TEXTURE_SLOTS 16
static struct {
  const rpatch_t *owner;
  unsigned offset, bytes, age;
} pixel_cache[FLASH_TEXTURE_SLOTS];
static byte *pixel_arena;
static unsigned pixel_bytes, cache_clock;
static int copy_frame, cache_frame_active;

int R_CanUseFlashTextures(void) {
  // Legacy filters may sample across column boundaries. Preserve their original
  // contiguous source layout; mapped views currently serve point-filtered 8-bit.
  return flash_data && V_GetMode()==VID_MODE8 && drawvars.filterwall==RDRAW_FILTER_POINT;
}

void R_BeginFlashTextureFrame(void) {
  // The arena is purgeable between views. Invalidate offsets before reuse if
  // the zone allocator reclaimed it; pin it while the renderer holds pointers.
  if (!pixel_arena) {
    memset(pixel_cache,0,sizeof(pixel_cache));pixel_bytes=0;copy_frame=-1;
  } else Z_ChangeTag(pixel_arena,PU_STATIC);
  cache_frame_active=1;
}

void R_EndFlashTextureFrame(void) {
  cache_frame_active=0;
  if(pixel_arena)Z_ChangeTag(pixel_arena,PU_CACHE);
}

static int find_pixel_gap(unsigned bytes, unsigned *offset) {
  const unsigned budget=FLASH_TEXTURE_CACHE_KB*1024;
  unsigned start=0;
  // At most 16 occupied regions. Never move pixels behind live span pointers.
  for (int pass=0;pass<=FLASH_TEXTURE_SLOTS;++pass) {
    if(start>budget || bytes>budget-start)return 0;
    unsigned next=start;
    for(int i=0;i<FLASH_TEXTURE_SLOTS;++i)
      if(pixel_cache[i].owner && start<pixel_cache[i].offset+pixel_cache[i].bytes &&
         start+bytes>pixel_cache[i].offset && next<pixel_cache[i].offset+pixel_cache[i].bytes)
        next=pixel_cache[i].offset+pixel_cache[i].bytes;
    if(next==start) {*offset=start;return 1;}
    start=next;
  }
  return 0;
}

const byte *R_CacheFlashTexturePixels(const rpatch_t *patch) {
  const unsigned budget=FLASH_TEXTURE_CACHE_KB*1024;
  unsigned bytes=(unsigned)patch->width*patch->height;
  if(!cache_frame_active)return NULL;
  for(int i=0;i<FLASH_TEXTURE_SLOTS;++i)
    if(pixel_cache[i].owner==patch) {
      pixel_cache[i].age=++cache_clock;
      return pixel_arena+pixel_cache[i].offset;
    }

  if(bytes>budget || copy_frame==validcount) {return NULL;}
  if(!pixel_arena) {
    // Optional cache: leave room for the zone header/alignment, otherwise use
    // mapped columns directly. A cache allocation must not turn pressure fatal.
    if(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)<budget+4096) {
      return NULL;
    }
    pixel_arena=Z_Malloc(budget,PU_STATIC,(void**)&pixel_arena);
  }
  int slot=-1;unsigned offset=0;
  for (;;) {
    for(int i=0;i<FLASH_TEXTURE_SLOTS;++i)
      if(!pixel_cache[i].owner) {slot=i;break;}
    if(slot>=0 && find_pixel_gap(bytes,&offset))break;
    int victim=-1;
    for(int i=0;i<FLASH_TEXTURE_SLOTS;++i)
      if(pixel_cache[i].owner && !pixel_cache[i].owner->locks &&
         (victim<0 || pixel_cache[i].age<pixel_cache[victim].age))victim=i;
    if(victim<0) {return NULL;}
    pixel_bytes-=pixel_cache[victim].bytes;

    memset(&pixel_cache[victim],0,sizeof(pixel_cache[victim]));slot=-1;
  }
  byte *pixels=pixel_arena+offset;
  for(int x=0;x<patch->width;++x)
    memcpy(pixels+x*patch->height,patch->columns[x].pixels,patch->height);
  pixel_cache[slot].owner=patch;pixel_cache[slot].offset=offset;
  pixel_cache[slot].bytes=bytes;pixel_cache[slot].age=++cache_clock;
  pixel_bytes+=bytes;copy_frame=validcount;

  return pixels;
}



static uint32_t get32(const byte *p) {
  uint32_t v; memcpy(&v, p, sizeof(v)); return LONG(v);
}

static void check_range(uint32_t offset, uint32_t bytes) {
  if ((offset & 3) || offset > flash_size || bytes > flash_size-offset)
    I_Error("TXFLASH: invalid mapped range %lu + %lu", (unsigned long)offset, (unsigned long)bytes);
}

static uint32_t lump_hash(const char *name) {
  int lump = W_GetNumForName(name);
  const byte *p = W_CacheLumpNum(lump);
  uint32_t h = 2166136261u;
  for (int n = W_LumpLength(lump); n > 0; --n)
    h = (h ^ *p++) * 16777619u;
  W_UnlockLumpNum(lump);
  return h;
}

void R_InitFlashTextures(void) {
  flash_data = NULL;
  flash_size = 0;
  if(pixel_arena)Z_Free(pixel_arena);
  memset(pixel_cache,0,sizeof(pixel_cache));
  pixel_bytes=cache_clock=0;
  copy_frame=-1;cache_frame_active=0;
  _Static_assert(sizeof(rpost_t) == 12, "TXF1 post ABI must be three 32-bit fields");
  int lump = W_CheckNumForName("TXFLASH");
  if (lump < 0) return; // Normal WADs retain the original compositor.
  int size = W_LumpLength(lump);
  if (size < 32) I_Error("TXFLASH: truncated header");
  const byte *p = W_CacheLumpNum(lump);
  if (((uintptr_t)p & 3) || memcmp(p,"TXF1",4) || get32(p+4) != 1 ||
      get32(p+12) != (uint32_t)size || get32(p+24) != 24 || get32(p+28) != 0)
    I_Error("TXFLASH: incompatible texture image");
  // PWADs can replace texture directories or individual patches. Use their
  // normal runtime composites rather than stale precompiled IWAD pixels.
  int overridden = get32(p+8) != (uint32_t)numtextures ||
      get32(p+16) != lump_hash("TEXTURE1") || get32(p+20) != lump_hash("PNAMES");
  for (int i=0; i<numtextures && !overridden; ++i)
    for (int j=0; j<textures[i]->patchcount; ++j)
      if (lumpinfo[textures[i]->patches[j].patch].wadfile != lumpinfo[lump].wadfile) {
        overridden=1;
        break;
      }
  if (overridden) {
    W_UnlockLumpNum(lump);
    return;
  }
  flash_data = p; flash_size = size;
  if ((uint32_t)numtextures > (flash_size-32)/24)
    I_Error("TXFLASH: truncated directory");
  unsigned compiled = 0;
  for (int i = 0; i < numtextures; ++i) {
    const byte *r = p+32+24*i;
    uint32_t w=get32(r+8), h=get32(r+12), mask=get32(r+16), co=get32(r+20);
    if (memcmp(r,textures[i]->name,8) || w != (uint32_t)textures[i]->width ||
        h != (uint32_t)textures[i]->height || mask != textures[i]->widthmask || !w || !h)
      I_Error("TXFLASH: invalid texture record %d", i);
    if (!co) continue; // Explicit raw-compositor fallback for uncompiled textures.
    check_range(co,w*12);
    for (uint32_t x=0;x<w;++x) {
      const byte *c=p+co+12*x;
      uint32_t n=get32(c), pixels=get32(c+4), posts=get32(c+8);
      if (n > flash_size/12) I_Error("TXFLASH: post count overflow");
      check_range(pixels,h);
      if (n) check_range(posts,n*12);
      else if (posts) I_Error("TXFLASH: invalid empty post list");
    }
    ++compiled;
  }
  lprintf(LO_INFO,"TXFLASH: %u/%d textures, %lu mapped bytes\n",compiled,numtextures,(unsigned long)flash_size);
}

int R_LoadFlashTexture(int id, rpatch_t *patch) {
  if (!R_CanUseFlashTextures()) return 0;
  const byte *r=flash_data+32+24*id;
  uint32_t w=get32(r+8), h=get32(r+12), co=get32(r+20);
  if (!co) return 0;
  patch->width=w; patch->height=h; patch->widthmask=get32(r+16);
  patch->isFlashComposite=1;
  patch->isNotTileable=0; patch->leftoffset=patch->topoffset=0;
  patch->data=Z_Malloc(w*sizeof(rcolumn_t),PU_STATIC,(void**)&patch->data);
  patch->columns=(rcolumn_t*)patch->data;
  for (uint32_t x=0;x<w;++x) {
    const byte *c=flash_data+co+12*x;
    patch->columns[x].numPosts=get32(c);
    patch->columns[x].pixels=(byte*)flash_data+get32(c+4);
    patch->columns[x].posts=get32(c) ? (rpost_t*)(flash_data+get32(c+8)) : NULL;
  }
  // These are representative pointers, not contiguous pixel/post arrays.
  // Texture consumers access data through the per-column views.
  patch->pixels=patch->columns[0].pixels;
  patch->posts=patch->columns[0].posts;
  return 1;
}
