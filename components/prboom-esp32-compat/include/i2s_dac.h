#ifndef I2S_DAC_H
#define I2S_DAC_H

#include <stddef.h>
#include <stdint.h>

#define I2S_DAC_SAMPLE_RATE 22050

void i2s_dac_init(void);
void i2s_dac_start(void);
void i2s_dac_write(const int16_t *samples, size_t frame_count);

#endif // I2S_DAC_H
