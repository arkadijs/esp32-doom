/* Adapted copy of Espressif's i2s_std example
 */

#include <stdint.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

void IRAM_ATTR dac_task(void *arg) {
#define EXAMPLE_BUFF_SIZE 2048

    uint8_t *w_buf = (uint8_t *)calloc(1, EXAMPLE_BUFF_SIZE);
    assert(w_buf); // Check if w_buf allocation success

    /* Assign w_buf */
    for (int i = 0; i < EXAMPLE_BUFF_SIZE; i += 8) {
        w_buf[i]     = 0x12;
        w_buf[i + 1] = 0x34;
        w_buf[i + 2] = 0x56;
        w_buf[i + 3] = 0x78;
        w_buf[i + 4] = 0x9A;
        w_buf[i + 5] = 0xBC;
        w_buf[i + 6] = 0xDE;
        w_buf[i + 7] = 0xF0;
    }

    size_t w_bytes = EXAMPLE_BUFF_SIZE;

    while (w_bytes == EXAMPLE_BUFF_SIZE) {
        /* Here we load the target buffer repeatedly, until all the DMA buffers are preloaded */
        ESP_ERROR_CHECK(i2s_channel_preload_data(dac_context.tx_chan, w_buf, EXAMPLE_BUFF_SIZE, &w_bytes));
    }

    ESP_ERROR_CHECK(i2s_channel_enable(dac_context.tx_chan));
    while (1) {
        /* Write i2s data */
        ESP_ERROR_CHECK(i2s_channel_write(dac_context.tx_chan, w_buf, EXAMPLE_BUFF_SIZE, &w_bytes, 1000));
        // if (i2s_channel_write(dac_context.tx_chan, w_buf, EXAMPLE_BUFF_SIZE, &w_bytes, 1000) == ESP_OK) {
        //     printf("Write Task: i2s write %d bytes\n", w_bytes);
        // } else {
        //     printf("Write Task: i2s write failed\n");
        // }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void i2s_dac_init() {
    ESP_LOGI(TAG, "I2S init...");
    i2s_chan_handle_t tx_chan = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(48000),
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

    int core = 1;
#ifdef CONFIG_FREERTOS_UNICORE
    core = 0;
#endif
    xTaskCreatePinnedToCore(dac_task, "dac", 6000, NULL, 5, NULL, core);
}

