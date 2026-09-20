/* ESP32 I2S sound backend: DOSBox OPL music plus Doom-format PCM effects. */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "doomdef.h"
#include "doomstat.h"
#include "i_sound.h"
#include "lprintf.h"
#include "memio.h"
#include "mus2mid.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"

#include "i2s_dac.h"
#include "dbopl/oplplayer.h"

#define MIX_CHANNELS 32
#define MIX_FRAMES 256
#define NORM_PITCH_VALUE 128

typedef struct
{
    const uint8_t *samples;
    uint32_t length;
    uint32_t position;
    uint32_t step;
    uint16_t sample_rate;
    uint8_t left;
    uint8_t right;
    bool active;
} mix_channel_t;

typedef struct
{
    const uint8_t *samples;
    uint32_t length;
    uint16_t sample_rate;
    bool loaded;
    bool valid;
} cached_sfx_t;

static const music_player_t *music_player = &opl_synth_player;
static cached_sfx_t sfx_cache[NUMSFX];
static mix_channel_t channels[MIX_CHANNELS];
static SemaphoreHandle_t audio_mutex;
static StaticSemaphore_t audio_mutex_storage;
static StaticTask_t audio_task_storage;
static StackType_t audio_task_stack[8192];
static bool audio_initialized;
static bool music_playing;

int snd_card = 1;
int mus_card = 1;
int snd_samplerate = I2S_DAC_SAMPLE_RATE;

static inline uint32_t ReadLE32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8)
         | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static bool LoadSfx(int id)
{
    cached_sfx_t *result;
    const uint8_t *data;
    uint32_t length;
    int lump_length;

    if (id <= 0 || id >= NUMSFX) return false;
    result = &sfx_cache[id];
    if (result->loaded) return result->valid;
    result->loaded = true;
    if (S_sfx[id].lumpnum < 0) return false;

    data = W_CacheLumpNum(S_sfx[id].lumpnum);
    lump_length = W_LumpLength(S_sfx[id].lumpnum);
    if (lump_length < 8 || data[0] != 0x03 || data[1] != 0x00)
    {
        lprintf(LO_WARN, "I_StartSound: invalid SFX lump for %s\n", S_sfx[id].name);
        return false;
    }

    result->sample_rate = (uint16_t) data[2] | ((uint16_t) data[3] << 8);
    length = ReadLE32(data + 4);
    if (result->sample_rate == 0 || length > (uint32_t) lump_length - 8 || length <= 48)
    {
        lprintf(LO_WARN, "I_StartSound: bad SFX header for %s (rate=%u length=%lu/%d)\n",
                S_sfx[id].name, result->sample_rate,
                (unsigned long) length, lump_length);
        return false;
    }

    result->samples = data + 8 + 16;
    result->length = length - 32;
    result->valid = true;
    return true;
}

static inline int16_t Clip16(int32_t sample)
{
    if (sample > INT16_MAX) return INT16_MAX;
    if (sample < INT16_MIN) return INT16_MIN;
    return (int16_t) sample;
}

static void AudioTask(void *arg)
{
    int16_t output[MIX_FRAMES * 2];
    int16_t music[MIX_FRAMES * 2];

    (void) arg;
    i2s_dac_start();

    for (;;)
    {
        xSemaphoreTake(audio_mutex, portMAX_DELAY);
        memset(music, 0, sizeof(music));
        if (music_playing) music_player->render(music, MIX_FRAMES);

        for (unsigned int frame = 0; frame < MIX_FRAMES; ++frame)
        {
            int32_t left = music[frame * 2];
            int32_t right = music[frame * 2 + 1];

            for (unsigned int i = 0; i < MIX_CHANNELS; ++i)
            {
                mix_channel_t *channel = &channels[i];
                uint32_t index;
                int32_t sample;
                if (!channel->active) continue;

                index = channel->position >> 16;
                if (index >= channel->length)
                {
                    channel->active = false;
                    continue;
                }

                sample = ((int32_t) channel->samples[index] - 128) << 8;
                left += sample * channel->left / 127;
                right += sample * channel->right / 127;
                channel->position += channel->step;
            }

            output[frame * 2] = Clip16(left);
            output[frame * 2 + 1] = Clip16(right);
        }

        xSemaphoreGive(audio_mutex);
        i2s_dac_write(output, MIX_FRAMES);
        // Give IDLE1 a scheduling window so the task watchdog is serviced;
        // the blocking I2S DMA write above already paces this loop in real
        // time, so this delay does not add audible latency.
        vTaskDelay(1);
    }
}

void I_SetChannels(void) {}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[9];
    if (sfx->link != NULL) sfx = sfx->link;
    snprintf(name, sizeof(name), "ds%.6s", sfx->name);
    return W_CheckNumForName(name);
}

void I_UpdateSoundParams(int handle, int volume, int separation, int pitch)
{
    mix_channel_t *channel;
    if (!audio_initialized || handle < 0 || handle >= MIX_CHANNELS) return;
    if (volume < 0) volume = 0;
    if (volume > 127) volume = 127;
    if (separation < 0) separation = 0;
    if (separation > 255) separation = 255;
    if (pitch < 1) pitch = 1;

    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    channel = &channels[handle];
    channel->left = (uint8_t) (volume * (255 - separation) / 127);
    channel->right = (uint8_t) (volume * separation / 127);
    if (channel->left > 127) channel->left = 127;
    if (channel->right > 127) channel->right = 127;
    channel->step = (uint32_t) (((uint64_t) channel->sample_rate * pitch << 16)
                                / (I2S_DAC_SAMPLE_RATE * NORM_PITCH_VALUE));
    if (channel->step == 0) channel->step = 1;
    xSemaphoreGive(audio_mutex);
}

int I_StartSound(int id, int channel_num, int volume, int separation,
                 int pitch, int priority)
{
    mix_channel_t *channel;
    cached_sfx_t *sfx;
    (void) priority;
    if (!audio_initialized || channel_num < 0 || channel_num >= MIX_CHANNELS
     || !LoadSfx(id)) return -1;
    if (volume < 0) volume = 0;
    if (volume > 127) volume = 127;
    if (separation < 0) separation = 0;
    if (separation > 255) separation = 255;
    if (pitch < 1) pitch = 1;

    sfx = &sfx_cache[id];
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    channel = &channels[channel_num];
    channel->samples = sfx->samples;
    channel->length = sfx->length;
    channel->position = 0;
    channel->sample_rate = sfx->sample_rate;
    channel->step = (uint32_t) (((uint64_t) sfx->sample_rate * pitch << 16)
                                / (I2S_DAC_SAMPLE_RATE * NORM_PITCH_VALUE));
    if (channel->step == 0) channel->step = 1;
    channel->left = (uint8_t) (volume * (255 - separation) / 127);
    channel->right = (uint8_t) (volume * separation / 127);
    if (channel->left > 127) channel->left = 127;
    if (channel->right > 127) channel->right = 127;
    channel->active = true;
    xSemaphoreGive(audio_mutex);
    return channel_num;
}

void I_StopSound(int handle)
{
    if (audio_initialized && handle >= 0 && handle < MIX_CHANNELS)
    {
        xSemaphoreTake(audio_mutex, portMAX_DELAY);
        channels[handle].active = false;
        xSemaphoreGive(audio_mutex);
    }
}

int I_SoundIsPlaying(int handle)
{
    int result = 0;
    if (audio_initialized && handle >= 0 && handle < MIX_CHANNELS)
    {
        xSemaphoreTake(audio_mutex, portMAX_DELAY);
        result = channels[handle].active;
        xSemaphoreGive(audio_mutex);
    }
    return result;
}

int I_AnySoundStillPlaying(void)
{
    int result = 0;
    if (!audio_initialized) return 0;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].active) result = 1;
    xSemaphoreGive(audio_mutex);
    return result;
}

void I_InitSound(void)
{
    int core = 1;
    int music_ok;
#ifdef CONFIG_FREERTOS_UNICORE
    core = 0;
#endif
    if (audio_initialized) return;
    snd_card = mus_card = 1;
    snd_samplerate = I2S_DAC_SAMPLE_RATE;
    audio_mutex = xSemaphoreCreateMutexStatic(&audio_mutex_storage);
    assert(audio_mutex != NULL);
    music_ok = music_player->init(snd_samplerate);
    assert(music_ok);
    music_player->setvolume(snd_MusicVolume);
    audio_initialized = true;
    TaskHandle_t task = xTaskCreateStaticPinnedToCore(AudioTask, "doom_audio",
                                                       sizeof(audio_task_stack),
                                                       NULL, 6, audio_task_stack,
                                                       &audio_task_storage, core);
    assert(task != NULL);
    lprintf(LO_INFO, "I_InitSound: DOSBox OPL music, %d Hz PCM mixer, "
                      "%d SFX channels, audio task on core %d\n",
            snd_samplerate, MIX_CHANNELS, core);
}

void I_ShutdownSound(void)
{
    if (!audio_initialized) return;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    music_player->shutdown();
    music_playing = false;
    xSemaphoreGive(audio_mutex);
}

void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_UpdateMusic(void) {}

void I_PlaySong(int handle, int looping)
{
    if (!audio_initialized || handle == 0) return;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    music_player->play((const void *) handle, looping);
    music_playing = true;
    xSemaphoreGive(audio_mutex);
}

void I_PauseSong(int handle)
{
    (void) handle;
    if (!audio_initialized) return;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    music_player->pause();
    music_playing = false;
    xSemaphoreGive(audio_mutex);
}

void I_ResumeSong(int handle)
{
    (void) handle;
    if (!audio_initialized) return;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    music_player->resume();
    music_playing = true;
    xSemaphoreGive(audio_mutex);
}

void I_StopSong(int handle)
{
    (void) handle;
    if (!audio_initialized) return;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    music_player->stop();
    music_playing = false;
    xSemaphoreGive(audio_mutex);
}

void I_UnRegisterSong(int handle)
{
    if (!audio_initialized || handle == 0) return;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    music_player->unregistersong((const void *) handle);
    xSemaphoreGive(audio_mutex);
}

int I_RegisterSong(const void *data, size_t len)
{
    const void *handle = NULL;
    MEMFILE *input;
    MEMFILE *output;
    void *midi_data;
    size_t midi_len;
    if (!audio_initialized || data == NULL || len == 0) return 0;

    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    if (len >= 4 && memcmp(data, "MThd", 4) == 0)
    {
        handle = music_player->registersong(data, len);
    }
    else
    {
        input = mem_fopen_read((void *) data, len);
        output = mem_fopen_write();
        if (mus2mid(input, output) == 0)
        {
            mem_get_buf(output, &midi_data, &midi_len);
            handle = music_player->registersong(midi_data, midi_len);
        }
        mem_fclose(input);
        mem_fclose(output);
    }
    xSemaphoreGive(audio_mutex);
    if (handle == NULL)
        lprintf(LO_WARN, "I_RegisterSong: failed to register song (%u bytes)\n",
                (unsigned) len);
    return (int) handle;
}

int I_RegisterMusic(const char *filename, musicinfo_t *song)
{
    (void) filename;
    (void) song;
    return 1;
}

void I_SetMusicVolume(int volume)
{
    if (!audio_initialized) return;
    xSemaphoreTake(audio_mutex, portMAX_DELAY);
    music_player->setvolume(volume);
    xSemaphoreGive(audio_mutex);
}
