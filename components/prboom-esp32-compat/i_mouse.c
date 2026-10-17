/* USB boot-mouse and Sandio O2 multi-axis report processing. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_timer.h"

#include "doomdef.h"
#include "doomstat.h"
#include "i_usb.h"

static uint8_t previous_buttons;
static uint16_t sandio_active;
static bool wheel_key_down;
static unsigned wheel_hold_polls;
static int64_t wheel_last_us;
static int64_t sandio_last_report_us;

/* One physical wheel click can arrive as several reports. */
#define WHEEL_DEBOUNCE_US 250000
#define WHEEL_HOLD_POLLS 2
/* The O2 resends its button state continuously; silence means keys are up. */
#define SANDIO_SILENCE_US 400000

static void post_mouse(uint8_t buttons, int x, int y)
{
    I_USB_AccumulateMouse(buttons & 7, x, -y);
}

static void mouse_update(uint8_t buttons, int x, int y, int wheel);

void I_MouseReport(const uint8_t *report, size_t len)
{
    if (len < 3) {
        return;
    }
    mouse_update(report[0], (int8_t)report[1], (int8_t)report[2],
                 len >= 4 ? (int8_t)report[3] : 0);
}

/* Report-protocol mouse without the report ID: 16 button bits, signed 12-bit
   X and Y packed in three bytes, wheel, horizontal wheel. */
void I_MouseReport12(const uint8_t *report, size_t len)
{
    if (len < 6) {
        return;
    }
    int x = report[2] | ((report[3] & 0x0f) << 8);
    int y = (report[3] >> 4) | (report[4] << 4);
    if (x & 0x800) {
        x -= 0x1000;
    }
    if (y & 0x800) {
        y -= 0x1000;
    }
    mouse_update(report[0], x, y, (int8_t)report[5]);
}

static void mouse_update(uint8_t buttons, int x, int y, int wheel)
{
    if (x || y || buttons != previous_buttons) {
        post_mouse(buttons, x, y);
    }

    /* This PrBoom version has no next/previous weapon actions. Treat either
       wheel direction as its "best weapon" toggle instead of dropping it. */
    int64_t now = esp_timer_get_time();
    if (wheel && !wheel_key_down &&
        now - wheel_last_us > WHEEL_DEBOUNCE_US) {
        I_KeyboardKeyDown(39, false); /* HID 0 -> key_weapontoggle default */
        wheel_key_down = true;
        wheel_hold_polls = WHEEL_HOLD_POLLS;
        wheel_last_us = now;
    }
    previous_buttons = buttons;
}

/* Called once per game poll; releases the wheel key after the game has seen it
   for a couple of frames, independent of further mouse reports. */
void I_MouseTick(void)
{
    /* A lost release report would otherwise leave a key stuck down. */
    if (sandio_active &&
        esp_timer_get_time() - sandio_last_report_us > SANDIO_SILENCE_US) {
        I_SandioDisconnected();
    }

    if (wheel_key_down && --wheel_hold_polls == 0) {
        I_KeyboardKeyUp(39);
        wheel_key_down = false;
    }
}

void I_MouseDisconnected(void)
{
    if (wheel_key_down) {
        I_KeyboardKeyUp(39);
        wheel_key_down = false;
    }
    if (previous_buttons) {
        post_mouse(0, 0, 0);
        previous_buttons = 0;
    }
}

enum {
    SANDIO_TOP_RIGHT = 0x001,
    SANDIO_TOP_FORWARD = 0x002,
    SANDIO_TOP_LEFT = 0x004,
    SANDIO_TOP_BACK = 0x008,
    SANDIO_RIGHT_UP = 0x010,
    SANDIO_RIGHT_DOWN = 0x020,
    SANDIO_RIGHT_FORWARD = 0x040,
    SANDIO_RIGHT_BACK = 0x080,
    SANDIO_LEFT_FORWARD = 0x100,
    SANDIO_LEFT_DOWN = 0x200,
    SANDIO_LEFT_BACK = 0x400,
    SANDIO_LEFT_UP = 0x800,
};

typedef struct {
    uint16_t mask;
    uint8_t hid_key;
} sandio_rule_t;

static const sandio_rule_t sandio_rules[] = {
    {SANDIO_TOP_FORWARD, 82}, /* up */
    {SANDIO_TOP_BACK, 81},    /* down */
    {SANDIO_TOP_LEFT, 54},    /* comma */
    {SANDIO_TOP_RIGHT, 55},   /* period */
    {SANDIO_RIGHT_FORWARD, 44}, /* space/use */
    {SANDIO_RIGHT_BACK, 43},    /* tab/map */
    {SANDIO_RIGHT_DOWN, 40},    /* enter */
    {SANDIO_RIGHT_UP, 41},      /* escape */
    {SANDIO_LEFT_FORWARD, 56},  /* slash */
    {SANDIO_LEFT_BACK, 52},     /* apostrophe */
    {SANDIO_LEFT_DOWN, 28},     /* y */
    {SANDIO_LEFT_UP, 17},       /* n */
};

static uint8_t sandio_decode_button(uint8_t encoded, uint8_t offset)
{
    uint8_t value = encoded - offset;
    return (value & 0xf0) ? 0 : value & 0x0f;
}

void I_SandioReport(const uint8_t *report, size_t len)
{
    if (len != 7) {
        return;
    }

    sandio_last_report_us = esp_timer_get_time();
    uint16_t magic = ((uint16_t)report[5] << 8) | report[6];
    uint8_t offset = 240 * ((21 * (uint32_t)magic + 7) % 2048) / 2048;
    uint16_t state = sandio_decode_button(report[2], offset)
                   | (sandio_decode_button(report[3], offset) << 4)
                   | (sandio_decode_button(report[4], offset) << 8);

    for (size_t i = 0; i < sizeof(sandio_rules) / sizeof(sandio_rules[0]); ++i) {
        const sandio_rule_t *rule = &sandio_rules[i];
        if ((state & rule->mask) && !(sandio_active & rule->mask)) {
            I_KeyboardKeyDown(rule->hid_key, false);
            sandio_active |= rule->mask;
        } else if (!(state & rule->mask) && (sandio_active & rule->mask)) {
            I_KeyboardKeyUp(rule->hid_key);
            sandio_active &= ~rule->mask;
        }
    }
}

void I_SandioConnected(void)
{
    extern int mousebstrafe;

    /* PrBoom has separate horizontal/vertical sensitivity and no mouse-use binding. */
    mouseSensitivity_horiz = 40;
    mouseSensitivity_vert = 0;
    mousebstrafe = -1;
}

void I_SandioDisconnected(void)
{
    for (size_t i = 0; i < sizeof(sandio_rules) / sizeof(sandio_rules[0]); ++i) {
        if (sandio_active & sandio_rules[i].mask) {
            I_KeyboardKeyUp(sandio_rules[i].hid_key);
        }
    }
    sandio_active = 0;
}
