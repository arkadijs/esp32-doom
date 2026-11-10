/* GPL-2.0-or-later. Preserve legacy sample order, rounding and channel state. */
#include "pcm_mix.h"
#include <string.h>

static inline int16_t clip16(int32_t sample)
{
    return sample>32767 ? 32767 : sample < -32768 ? -32768 : sample;
}


unsigned PCM_MixBlock(mix_channel_t channels[PCM_CHANNELS],
                     const int16_t *music, int16_t *output, unsigned frames)
{
    mix_channel_t *active[PCM_CHANNELS];
    unsigned count=0;
    for(unsigned i=0;i<PCM_CHANNELS;++i)
        if(channels[i].active)active[count++]=&channels[i];
    if(!count) {
        // Synth output is already signed 16-bit. No clipping or channel work.
        if(frames && output!=music)memcpy(output,music,frames*2*sizeof(*output));
        return 0;
    }
    for(unsigned frame=0;frame<frames;++frame) {
        int32_t left=music[frame*2], right=music[frame*2+1];
        for(unsigned i=0;i<count;++i) {
            mix_channel_t *ch=active[i];
            // A channel can finish midway through this block.
            if(!ch->active)continue;
            unsigned index=ch->position>>16;
            if(index>=ch->length) {ch->active=false;continue;}
            int32_t sample=((int32_t)ch->samples[index]-128)*256;
            left+=sample*ch->left/127;
            right+=sample*ch->right/127;
            ch->position+=ch->step;
        }
        output[frame*2]=clip16(left);output[frame*2+1]=clip16(right);
    }
    return count;
}
