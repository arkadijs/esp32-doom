/* GPL-2.0-or-later. Host adapter for the original PrBoom compositor.
 * The build tool inserts compositor functions verbatim from r_patch.c.
 * This adapter's native pointers never enter the serialized asset format. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#pragma pack(push,4)
#include "r_patch.h"
#pragma pack(pop)
typedef unsigned char byte;
typedef struct { byte topdelta, length; } column_t;
typedef struct { short width,height,leftoffset,topoffset; int columnofs[1]; } patch_t;
typedef struct { int originx,originy,patch; } texpatch_t;
typedef struct { char name[8]; unsigned widthmask; short width,height,patchcount; texpatch_t patches[1]; } texture_t;
#define SHORT(x) (x)
#define LONG(x) (x)
#define PU_STATIC 0
#define RANGECHECK 1
static int numtextures, numlumps;
static texture_t **textures;
static rpatch_t *texture_composites;
static byte *wad;
static size_t wadsize;
static uint32_t *positions,*sizes;
static char (*names)[8];
static byte *namespaces;
static void I_Error(const char *format, ...) { va_list a;va_start(a,format);vfprintf(stderr,format,a);va_end(a);fputc('\n',stderr);exit(1); }
static const void *W_CacheLumpNum(int i) { assert(i>=0 && i<numlumps); return wad+positions[i]; }
static void W_UnlockLumpNum(int i) { (void)i; }
static void *Z_Malloc(size_t n,int tag,void **user) { (void)tag;void *p=malloc(n);if(!p)I_Error("host OOM");if(user)memcpy(user,&p,sizeof(p));return p; }
static uint32_t u32(const byte *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int16_t s16(const byte *p) { return (int16_t)((unsigned)p[0]|(unsigned)p[1]<<8); }
static int find(const char *key,int ns) { for(int i=numlumps-1;i>=0;--i)if(namespaces[i]==ns && !memcmp(names[i],key,8))return i;return -1; }
static void out32(FILE *f,uint32_t v) { byte p[4]={v,v>>8,v>>16,v>>24};assert(fwrite(p,1,4,f)==4); }
/* ORIGINAL_COMPOSITOR */
int main(int argc,char **argv) {
  if(argc!=3)I_Error("usage: compositor input.wad output.raw");
  _Static_assert(sizeof(rpost_t)==12,"post ABI");
  FILE *f=fopen(argv[1],"rb");if(!f)I_Error("open WAD");fseek(f,0,SEEK_END);wadsize=ftell(f);rewind(f);wad=malloc(wadsize);assert(fread(wad,1,wadsize,f)==wadsize);fclose(f);
  assert(wadsize>=12);numlumps=u32(wad+4);uint32_t dir=u32(wad+8);assert((uint64_t)dir+16ull*numlumps<=wadsize);
  positions=calloc(numlumps,4);sizes=calloc(numlumps,4);names=calloc(numlumps,8);namespaces=calloc(numlumps,1);int ns=0;
  for(int i=0;i<numlumps;++i) { const byte *d=wad+dir+16*i;positions[i]=u32(d);sizes[i]=u32(d+4);assert((uint64_t)positions[i]+sizes[i]<=wadsize);memcpy(names[i],d+8,8);
    if(!memcmp(names[i],"S_START",7))ns=1;else if(!memcmp(names[i],"F_START",7))ns=2;
    namespaces[i]=ns;if(!memcmp(names[i],"S_END",5)||!memcmp(names[i],"F_END",5))ns=0;
  }
  int ti=find("TEXTURE1",0),pi=find("PNAMES\0\0",0);assert(ti>=0&&pi>=0);
  const byte *td=W_CacheLumpNum(ti),*pn=W_CacheLumpNum(pi);unsigned np=u32(pn);assert(4ull+8ull*np<=sizes[pi]);numtextures=u32(td);assert(4ull+4ull*numtextures<=sizes[ti]);
  textures=calloc(numtextures,sizeof(*textures));texture_composites=calloc(numtextures,sizeof(*texture_composites));
  for(int i=0;i<numtextures;++i) { unsigned off=u32(td+4+4*i);assert(off+22<=sizes[ti]);const byte *r=td+off;int n=s16(r+20);assert(n>=0 && off+22ull+10ull*n<=sizes[ti]);texture_t *t=calloc(1,sizeof(*t)+(size_t)n*sizeof(texpatch_t));textures[i]=t;memcpy(t->name,r,8);t->width=s16(r+12);t->height=s16(r+14);t->patchcount=n;assert(t->width>0&&t->height>0);unsigned j=1;while(j*2<=(unsigned)t->width)j*=2;t->widthmask=j-1;
    for(int k=0;k<n;++k) {const byte *p=r+22+10*k;int idx=s16(p+4);assert(idx>=0 && (unsigned)idx<np);int lump=find((const char*)pn+4+8*idx,0);if(lump<0)lump=find((const char*)pn+4+8*idx,1);assert(lump>=0);t->patches[k]=(texpatch_t){s16(p),s16(p+2),lump};}
  }
  FILE *out=fopen(argv[2],"wb");assert(out);assert(fwrite("TXR1",1,4,out)==4);out32(out,numtextures);
  for(int i=0;i<numtextures;++i) { createTextureCompositePatch(i);rpatch_t *p=&texture_composites[i];assert(fwrite(textures[i]->name,1,8,out)==8);out32(out,p->width);out32(out,p->height);out32(out,p->widthmask);
    for(int x=0;x<p->width;++x) {const rcolumn_t *c=&p->columns[x];assert(fwrite(c->pixels,1,p->height,out)==(size_t)p->height);out32(out,c->numPosts);for(int j=0;j<c->numPosts;++j){out32(out,c->posts[j].topdelta);out32(out,c->posts[j].length);out32(out,c->posts[j].slope);}}
    free(p->data);free(textures[i]);
  }
  assert(fclose(out)==0);free(textures);free(texture_composites);free(positions);free(sizes);free(names);free(namespaces);free(wad);return 0;
}
