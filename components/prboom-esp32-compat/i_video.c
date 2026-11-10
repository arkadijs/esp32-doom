/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2006 by
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
 *  DOOM graphics stuff for SDL
 *
 *-----------------------------------------------------------------------------
 */

#include "config.h"
#include <stdlib.h>
#include <unistd.h>
#include "m_argv.h"
#include "doomstat.h"
#include "doomdef.h"
#include "doomtype.h"
#include "v_video.h"
#include "r_draw.h"
#include "r_patch.h"
#include "d_main.h"
#include "d_event.h"
#include "i_video.h"
#include "z_zone.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "st_stuff.h"
#include "m_menu.h"
#include "am_map.h"
#include "r_main.h"
#include "lprintf.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "esp_err.h"
#include "i2s_dac.h"
#include "i_sound.h"
#include "g_game.h"

#include "i80_lcd.h"
#ifdef DOOM_USB_HOST
#include "i_usb.h"
#endif

static byte *screen0;
static int frame_swap_allowed;
// Both the observer and Doom execute on core 0. No polling or I/O in the hook.
#ifdef DOOM_USB_HOST
#define IDLE0_GRACE_US 100000
#else
#define IDLE0_GRACE_US 500000
#endif
static volatile unsigned idle0_generation;
static unsigned idle0_seen;
static void check_idle0(void);
static int64_t idle0_seen_at;
static bool IRAM_ATTR observe_idle0(void)
{
    ++idle0_generation;
    return true;
}

int use_fullscreen=0;
int use_doublebuffer=0;
int usejoystick=0;
int joyleft, joyright, joyup, joydown;

void I_StartTic (void)
{
#ifdef DOOM_USB_HOST
  I_USB_Poll();
#endif
}

//static void I_UploadNewPalette(int pal)
//{
//}

//////////////////////////////////////////////////////////////////////////////
// Graphics API

void I_ShutdownGraphics(void)
{
}

//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    check_idle0(); // Also service idle scheduling inside the wipe loop.
}

static void check_idle0(void)
{
    int64_t now=esp_timer_get_time();
    unsigned generation=idle0_generation;
    if(generation!=idle0_seen) {
        idle0_seen=generation;
        idle0_seen_at=now;
    } else {
        int64_t gap=now-idle0_seen_at;
        if(gap>=IDLE0_GRACE_US) {
            // Usually completion/tic waits already let IDLE0 run. Only force a
            // scheduling window when rendering has monopolized this core.
            vTaskDelay(1);
            int64_t end=esp_timer_get_time();
            idle0_seen=idle0_generation;
            idle0_seen_at=end;
        }
    }
}

void I_StartFrame(void)
{
    check_idle0();
}

int I_StartDisplay(void)
{
    check_idle0(); // Includes extra displays while waiting for a simulation tic.
    return true;
}

void I_SetFrameSwapAllowed(int allowed)
{
    if(frame_swap_allowed && !allowed)i80_lcd_restore_last(screens[0].data);
    frame_swap_allowed=allowed;
}

int I_FrameBufferSwaps(void)
{
    return frame_swap_allowed;
}

void I_EndDisplay(void)
{
}

//
// I_FinishUpdate
//

void I_FinishUpdate (void)
{

    byte *next=i80_lcd_send(screens[0].data,frame_swap_allowed);
    if(next!=screens[0].data) {
        screens[0].data=screen0=next;
        R_RebaseFrameBuffer();
    }
}

uint16_t lcdpal[256];

void I_SetPalette (int pal)
{
	int pplump = W_GetNumForName("PLAYPAL");
	const byte * palette = W_CacheLumpNum(pplump);
	palette+=pal*(3*256);
    for (int i=0; i<255 ; i++) {
        int v=((palette[0]>>3)<<11)+((palette[1]>>2)<<5)+(palette[2]>>3);
		lcdpal[i]=(v>>8)+(v<<8);
//		lcdpal[i]=v;
		palette += 3;
	}
	W_UnlockLumpNum(pplump);
}

void I_PreInitGraphics(void)
{
	lprintf(LO_INFO, "preinitgfx");
    static int idle_registered;
    if(!idle_registered) {
        ESP_ERROR_CHECK(esp_register_freertos_idle_hook_for_cpu(observe_idle0,0));
        idle_registered=1;
        idle0_seen_at=esp_timer_get_time();
    }
    screen0 = heap_caps_malloc(SCREENWIDTH*SCREENHEIGHT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(screen0);
}

// CPhipps -
// I_SetRes
// Sets the screen resolution
void I_SetRes(void)
{
//  I_CalculateRes(SCREENWIDTH, SCREENHEIGHT);

  // set first three to standard values
  for (int i=0; i<3; i++) {
    screens[i].width = SCREENWIDTH;
    screens[i].height = SCREENHEIGHT;
    screens[i].byte_pitch = SCREENPITCH;
    screens[i].short_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE16);
    screens[i].int_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE32);
  }

  // statusbar
  screens[4].width = SCREENWIDTH;
  screens[4].height = (ST_SCALED_HEIGHT+1);
  screens[4].byte_pitch = SCREENPITCH;
  screens[4].short_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE16);
  screens[4].int_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE32);

  screens[0].not_on_heap = true;
  screens[0].data = screen0;

  lprintf(LO_INFO,"I_SetRes: Using resolution %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
}

void I_InitGraphics(void)
{
  static int firsttime=1;

  if (firsttime)
  {
    firsttime = 0;

    atexit(I_ShutdownGraphics);
    lprintf(LO_INFO, "I_InitGraphics: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);

    /* Set the video mode */
    I_UpdateVideoMode();
  }
}

void I_UpdateVideoMode(void)
{
  video_mode_t mode;

  lprintf(LO_INFO, "I_UpdateVideoMode: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);

//    mode = VID_MODE16;
    mode = VID_MODE8;

  V_InitMode(mode);
  V_DestroyUnusedTrueColorPalettes();
  V_FreeScreens();

  I_SetRes();

  V_AllocScreens();

  R_InitBuffer(SCREENWIDTH, SCREENHEIGHT);
}
