/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
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
 *  Misc system stuff needed by Doom, implemented for Linux.
 *  Mainly timer handling, and ENDOOM/ENDBOOM.
 *
 *-----------------------------------------------------------------------------
 */

#include <stdio.h>

#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#ifdef _MSC_VER
#define    F_OK    0    /* Check for file existence */
#define    W_OK    2    /* Check for write permission */
#define    R_OK    4    /* Check for read permission */
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/stat.h>



#include "config.h"
#include <unistd.h>
#include <sched.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "m_argv.h"
#include "lprintf.h"
#include "doomtype.h"
#include "doomdef.h"
#include "lprintf.h"
#include "m_fixed.h"
#include "r_fps.h"
#include "i_system.h"
#include "i_joy.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_partition.h"
#include "esp_err.h"
#include "esp_log.h"

#ifdef __GNUG__
#pragma implementation "i_system.h"
#endif
#include "i_system.h"

#include <sys/time.h>

int realtime=0;


void I_uSleep(unsigned long usecs)
{
	vTaskDelay(usecs/1000);
}

static unsigned long getMsTicks() {
  struct timeval tv;
  struct timezone tz;
//  unsigned long thistimereply;

  gettimeofday(&tv, &tz);

  //convert to ms
  unsigned long now = tv.tv_usec/1000+tv.tv_sec*1000;
  return now;
}

int I_GetTime_RealTime (void)
{
  struct timeval tv;
  struct timezone tz;
  unsigned long thistimereply;

  gettimeofday(&tv, &tz);

  thistimereply = (tv.tv_sec * TICRATE + (tv.tv_usec * TICRATE) / 1000000);

  return thistimereply;

}

const int displaytime=0;

fixed_t I_GetTimeFrac (void)
{
  unsigned long now;
  fixed_t frac;


  now = getMsTicks();

  if (tic_vars.step == 0)
    return FRACUNIT;
  else
  {
    frac = (fixed_t)((now - tic_vars.start + displaytime) * FRACUNIT / tic_vars.step);
    if (frac < 0)
      frac = 0;
    if (frac > FRACUNIT)
      frac = FRACUNIT;
    return frac;
  }
}


void I_GetTime_SaveMS(void)
{
  if (!movement_smooth)
    return;

  tic_vars.start = getMsTicks();
  tic_vars.next = (unsigned int) ((tic_vars.start * tic_vars.msec + 1.0f) / tic_vars.msec);
  tic_vars.step = tic_vars.next - tic_vars.start;
}

unsigned long I_GetRandomTimeSeed(void)
{
	return 4; //per https://xkcd.com/221/
}

const char* I_GetVersionString(char* buf, size_t sz)
{
  sprintf(buf,"%s v%s (http://prboom.sourceforge.net/)",PACKAGE,VERSION);
  return buf;
}

const char* I_SigString(char* buf, size_t sz, int signum)
{
  return buf;
}

extern unsigned char *doom1waddata;

typedef struct {
	const esp_partition_t* part;
    esp_partition_mmap_handle_t handle;
    const void *mmap_ptr;
    int offset;
} FileDesc;

#define MAX_N_FILES 2
static FileDesc fds[MAX_N_FILES];
const char* flash_wads[] = {
    "doom2.wad",
    "prboom-plus.wad"
};

int I_Open(const char *wad, int flags) {
    FileDesc *file = NULL;
    int fd;
    for (int i = 0; i < MAX_N_FILES; ++i) {
        if (fds[i].part == NULL) {
            file = &fds[i];
            fd = i;
            break;
        }
    }

    if (file == NULL) {
        lprintf(LO_INFO, "I_Open: open %s failed\n", wad);
        return -1;
    }

    for (int i = 0; i < sizeof(flash_wads)/sizeof(flash_wads[0]); ++i) {
        if (!strcasecmp(wad, flash_wads[i])) {
            file->part = esp_partition_find_first(66, 6+i, NULL);
            assert(file->part);
            file->offset=0;
            break;
        }
    }

    if (!file->part) {
        lprintf(LO_INFO, "I_Open: open %s failed\n", wad);
        return -1;
    }

    ESP_LOGD("i_system", "mmaping %s of size %lu", wad, file->part->size);
    ESP_ERROR_CHECK(esp_partition_mmap(file->part, 0, file->part->size, ESP_PARTITION_MMAP_DATA, &file->mmap_ptr, &file->handle));
    ESP_LOGI("i_system", "%s @%p", wad, file->mmap_ptr);

    return fd;
}

int I_Lseek(int ifd, off_t offset, int whence) {
	if (whence==SEEK_SET) {
		fds[ifd].offset=offset;
	} else if (whence==SEEK_CUR) {
		fds[ifd].offset+=offset;
	} else if (whence==SEEK_END) {
		lprintf(LO_INFO, "I_Lseek: SEEK_END unimplemented\n");
	}
	return fds[ifd].offset;
}

int I_Filelength(int ifd)
{
    return fds[ifd].part->size;
}

void I_Close(int fd) {
    esp_partition_munmap(fds[fd].handle);
    fds[fd].part = NULL;
}

void *I_Mmap(void *addr, size_t length, int prot, int flags, int ifd, off_t offset) {
    return (byte*)fds[ifd].mmap_ptr + offset;
}

int I_Munmap(void *addr, size_t length) {
    return 0;
}

void I_Read(int ifd, void* vbuf, size_t sz)
{
    memcpy(vbuf, (byte*)fds[ifd].mmap_ptr + fds[ifd].offset, sz);
    fds[ifd].offset += sz;
}

const char *I_DoomExeDir(void)
{
  return "";
}

char* I_FindFile(const char* wfname, const char* ext)
{
  char *findfile_name = malloc(strlen(wfname) + strlen(ext) + 1);

  sprintf(findfile_name, "%s%s", wfname, ext);

  for (int i = 0; i < MAX_N_FILES; ++i) {
      if (!strcasecmp(findfile_name, flash_wads[i])) {
          return findfile_name;
      }
  }

  lprintf(LO_INFO, "I_FindFile: %s not found\n", findfile_name);
  return NULL;
}

void I_SetAffinityMask(void)
{
}
