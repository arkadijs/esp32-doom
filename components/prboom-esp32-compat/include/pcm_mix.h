/* GPL-2.0-or-later. Platform-independent Doom PCM block mixer. */
#ifndef DOOM_PCM_MIX_H
#define DOOM_PCM_MIX_H
#include <stdint.h>
#include <stdbool.h>
#define PCM_CHANNELS 32
typedef struct {
    const uint8_t *samples;
    uint32_t length, position, step;
    uint16_t sample_rate;
    uint8_t left, right;
    bool active;
} mix_channel_t;

/* Music/output are interleaved stereo arrays of 2*frames samples. They may be
 * identical for in-place mixing; otherwise they must not overlap.
 * Returns the number of channels active at block entry. */
unsigned PCM_MixBlock(mix_channel_t channels[PCM_CHANNELS],
                     const int16_t *music, int16_t *output, unsigned frames);
#endif
