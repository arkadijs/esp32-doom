/* Adapted copy of Espressif's i2s_std example
 */

#include <stdint.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "i2s_dac.h"

static const char *TAG = "i2s_dac";

#define PIN_NUM_BCLK GPIO_NUM_16
#define PIN_NUM_LRCK GPIO_NUM_21
#define PIN_NUM_DOUT GPIO_NUM_17

typedef struct {
    i2s_chan_handle_t tx_chan;
} dac_context_t;

static dac_context_t dac_context;

void i2s_dac_init(void) {
    ESP_LOGI(TAG, "I2S init: %d Hz, 16-bit stereo, BCLK=%d Hz",
             I2S_DAC_SAMPLE_RATE, I2S_DAC_SAMPLE_RATE * 16 * 2);
    i2s_chan_handle_t tx_chan = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_DAC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // some codecs may require mclk signal, PCM510x doesn't need it
            .bclk = PIN_NUM_BCLK,
            .ws   = PIN_NUM_LRCK,
            .dout = PIN_NUM_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));

    dac_context = (dac_context_t){
        .tx_chan = tx_chan,
    };

}

void i2s_dac_start(void)
{
    ESP_ERROR_CHECK(i2s_channel_enable(dac_context.tx_chan));
}

void i2s_dac_write(const int16_t *samples, size_t frame_count)
{
    const size_t bytes = frame_count * 2 * sizeof(*samples);
    size_t written = 0;
    esp_err_t err = i2s_channel_write(dac_context.tx_chan, samples, bytes,
                                      &written, portMAX_DELAY);
    ESP_ERROR_CHECK(err);
    assert(written == bytes);
}
