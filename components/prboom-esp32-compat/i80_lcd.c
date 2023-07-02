/* Adapted copy of Espressif's i80_controller example
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "i80_lcd.h"

static const char *TAG = "i80_lcd";

//#define CONFIG_LCD_I80_COLOR_IN_PSRAM
#define CONFIG_LCD_I80_BUS_WIDTH 8

#ifdef CONFIG_LCD_I80_COLOR_IN_PSRAM
// PCLK frequency can't go too high as the limitation of PSRAM bandwidth
#define LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)
#else
#define LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)
#endif // CONFIG_LCD_I80_COLOR_IN_PSRAM

#define PIN_POWER_ON           15
#define PIN_NUM_BK_LIGHT       38
#define LCD_BK_LIGHT_ON_LEVEL  1
#define LCD_BK_LIGHT_OFF_LEVEL !LCD_BK_LIGHT_ON_LEVEL
#define PIN_NUM_DATA0          39
#define PIN_NUM_DATA1          40
#define PIN_NUM_DATA2          41
#define PIN_NUM_DATA3          42
#define PIN_NUM_DATA4          45
#define PIN_NUM_DATA5          46
#define PIN_NUM_DATA6          47
#define PIN_NUM_DATA7          48
#define PIN_LCD_RD             9
#define PIN_NUM_PCLK           8
#define PIN_NUM_DC             7
#define PIN_NUM_CS             6
#define PIN_NUM_RST            5

// The pixel number in horizontal and vertical
#define LCD_H_RES              320
#define LCD_V_RES              170
#define LCD_LINES_PER_TRANS     10

// Bit number used to represent command and parameter
#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

// Supported alignment: 16, 32, 64. A higher alignment can enables higher burst transfer size, thus a higher i80 bus throughput.
#define PSRAM_DATA_ALIGNMENT   64

typedef struct {
    esp_lcd_panel_handle_t    panel_handle;
    esp_lcd_panel_io_handle_t panel_io_handle;

    SemaphoreHandle_t sem;

    byte *fb;
    void *trans_buf;
    uint  next_line;
} lcd_context_t;

static lcd_context_t lcd_context;

static void fb_to_rgb16(void *fb_in, uint16_t *rgb16_out, int size) {
    size /= 4;
    uint32_t *fb32 = (uint32_t*)fb_in;
    for (int i = 0, j = 0; i < size; i++, j += 4) {
        uint32_t w = fb32[i];
        rgb16_out[j+0] = lcdpal[ w       &0xff];
        rgb16_out[j+1] = lcdpal[(w >>  8)&0xff];
        rgb16_out[j+2] = lcdpal[(w >> 16)&0xff];
        rgb16_out[j+3] = lcdpal[(w >> 24)&0xff];
    }
}

void i80_lcd_send(byte *screen) {
    memcpy(lcd_context.fb, screen, LCD_H_RES * LCD_V_RES);
    xSemaphoreGive(lcd_context.sem);
    vTaskDelay(1); // TODO for now give idle task a chance to reset watchdog
}

void IRAM_ATTR lcd_task(void *arg) {
    while (1) {
        xSemaphoreTake(lcd_context.sem, portMAX_DELAY);

        if (lcd_context.next_line >= LCD_V_RES) {
            lcd_context.next_line = 0;
        }

        fb_to_rgb16(lcd_context.fb + lcd_context.next_line * LCD_H_RES,
            lcd_context.trans_buf, LCD_H_RES * LCD_LINES_PER_TRANS);

        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(lcd_context.panel_handle,
            0, lcd_context.next_line,
            LCD_H_RES, lcd_context.next_line + LCD_LINES_PER_TRANS,
            lcd_context.trans_buf));

        lcd_context.next_line += LCD_LINES_PER_TRANS;
    }
}

bool IRAM_ATTR notify_lcd_trans_done(esp_lcd_panel_io_handle_t panel_io_handle, esp_lcd_panel_io_event_data_t *_edata, void *user_ctx) {
    lcd_context_t *lcd_context = (lcd_context_t*)user_ctx;

    if (lcd_context->next_line > 0 && lcd_context->next_line < LCD_V_RES) {
        xSemaphoreGive(lcd_context->sem);
    }

    return false; // Whether a high priority task has been waken up by this function
}

void i80_lcd_init(void) {
    ESP_LOGI(TAG, "LCD init...");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT | 1ULL << PIN_POWER_ON | 1ULL << PIN_LCD_RD
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(PIN_POWER_ON, 1);
    gpio_set_level(PIN_LCD_RD, 1);
    gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_OFF_LEVEL);

    ESP_LOGI(TAG, "Initializing i8080 bus");
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = PIN_NUM_DC,
        .wr_gpio_num = PIN_NUM_PCLK,
        .data_gpio_nums = {
            PIN_NUM_DATA0,
            PIN_NUM_DATA1,
            PIN_NUM_DATA2,
            PIN_NUM_DATA3,
            PIN_NUM_DATA4,
            PIN_NUM_DATA5,
            PIN_NUM_DATA6,
            PIN_NUM_DATA7,
        },
        .bus_width = CONFIG_LCD_I80_BUS_WIDTH,
        .max_transfer_bytes = LCD_H_RES * (LCD_LINES_PER_TRANS+1) * 2,
        .psram_trans_align = PSRAM_DATA_ALIGNMENT,
        .sram_trans_align = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags = {
            .swap_color_bytes = 0,
        },
        .on_color_trans_done = notify_lcd_trans_done,
        .user_ctx = &lcd_context,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_LOGI(TAG, "Installing ST7789 LCD driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    // Set inversion, x/y coordinate order, x/y mirror according to your LCD module spec
    // the gap is LCD panel specific, even panels with the same driver IC, can have different gap value
    esp_lcd_panel_invert_color(panel_handle, true);
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, false, true); // The screen faces you, and the USB is on the left
    esp_lcd_panel_set_gap(panel_handle, 0, 35);

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_ON_LEVEL);

    void *fb = 0, *trans_buf = 0;
#ifdef CONFIG_LCD_I80_COLOR_IN_PSRAM
    fb = heap_caps_aligned_alloc(PSRAM_DATA_ALIGNMENT, LCD_H_RES * LCD_V_RES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    trans_buf = heap_caps_aligned_alloc(PSRAM_DATA_ALIGNMENT, LCD_H_RES * LCD_LINES_PER_TRANS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    fb = heap_caps_malloc(LCD_H_RES * LCD_V_RES, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    trans_buf = heap_caps_malloc(LCD_H_RES * LCD_LINES_PER_TRANS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    assert(fb);
    assert(trans_buf);
    ESP_LOGI(TAG, "fb@%p, buf@%p", fb, trans_buf);

    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    assert(sem);

    lcd_context = (lcd_context_t){
            .panel_handle = panel_handle,
            .panel_io_handle = io_handle,
            .sem = sem,
            .fb = fb,
            .trans_buf = trans_buf,
            .next_line = 0
        };

    int core = 1;
#ifdef CONFIG_FREERTOS_UNICORE
    core = 0;
#endif
    xTaskCreatePinnedToCore(&lcd_task, "lcd", 6000, NULL, 6, NULL, core);
}
