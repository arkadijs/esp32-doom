/* USB HID keyboard to PrBoom key-event translation. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "doomdef.h"
#include "i_usb.h"

#define HID_MOD_LEFT_CTRL    0x01
#define HID_MOD_LEFT_SHIFT   0x02
#define HID_MOD_LEFT_ALT     0x04
#define HID_MOD_RIGHT_CTRL   0x10
#define HID_MOD_RIGHT_SHIFT  0x20
#define HID_MOD_RIGHT_ALT    0x40

static uint8_t previous_report[8];

static int translate_key(uint8_t key)
{
    static const int keys[] = {
        0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f',
        'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
        'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        KEYD_ENTER, KEYD_ESCAPE, KEYD_BACKSPACE, KEYD_TAB, ' ',
        KEYD_MINUS, KEYD_EQUALS, '[', ']', '\\', 0, ';', '\'', '`', ',',
        '.', '/', KEYD_CAPSLOCK, KEYD_F1, KEYD_F2, KEYD_F3, KEYD_F4,
        KEYD_F5, KEYD_F6, KEYD_F7, KEYD_F8, KEYD_F9, KEYD_F10,
        KEYD_F11, KEYD_F12, 0, KEYD_SCROLLLOCK, KEYD_PAUSE, KEYD_INSERT,
        KEYD_HOME, KEYD_PAGEUP, KEYD_DEL, KEYD_END, KEYD_PAGEDOWN,
        KEYD_RIGHTARROW, KEYD_LEFTARROW, KEYD_DOWNARROW, KEYD_UPARROW,
        KEYD_NUMLOCK, KEYD_KEYPADDIVIDE, KEYD_KEYPADMULTIPLY,
        KEYD_KEYPADMINUS, KEYD_KEYPADPLUS, KEYD_KEYPADENTER,
        KEYD_KEYPAD1, KEYD_KEYPAD2, KEYD_KEYPAD3, KEYD_KEYPAD4,
        KEYD_KEYPAD5, KEYD_KEYPAD6, KEYD_KEYPAD7, KEYD_KEYPAD8,
        KEYD_KEYPAD9, KEYD_KEYPAD0, KEYD_KEYPADPERIOD, 0, 0, 0,
        KEYD_EQUALS,
    };

    switch (key) {
    case 224:
    case 228:
        return KEYD_RCTRL;
    case 225:
    case 229:
        return KEYD_RSHIFT;
    case 226:
    case 230:
        return KEYD_RALT;
    default:
        break;
    }
    if (key >= sizeof(keys) / sizeof(keys[0])) {
        return 0;
    }
    return keys[key];
}

static bool report_has_key(const uint8_t *report, uint8_t key)
{
    for (size_t i = 2; i < 8; ++i) {
        if (report[i] == key) {
            return true;
        }
    }
    return false;
}

static void post_key(uint8_t key, bool down, bool shifted)
{
    int doom_key = translate_key(key);
    if (!doom_key) {
        return;
    }

    event_t event = {
        .type = down ? ev_keydown : ev_keyup,
        .data1 = doom_key,
        .data2 = doom_key,
        .data3 = shifted && doom_key >= 'a' && doom_key <= 'z'
                     ? doom_key - 'a' + 'A' : doom_key,
    };
    I_USB_PostEvent(&event);
}

void I_KeyboardKeyDown(uint8_t key, bool shifted)
{
    post_key(key, true, shifted);
}

void I_KeyboardKeyUp(uint8_t key)
{
    post_key(key, false, false);
}

static void update_modifier(uint8_t current, uint8_t previous,
                            uint8_t mask, int doom_key)
{
    if ((current ^ previous) & mask) {
        event_t event = {
            .type = (current & mask) ? ev_keydown : ev_keyup,
            .data1 = doom_key,
            .data2 = (current & mask) ? doom_key : 0,
            .data3 = (current & mask) ? doom_key : 0,
        };
        I_USB_PostEvent(&event);
    }
}

void I_KeyboardReport(const uint8_t *report, size_t len)
{
    if (len < sizeof(previous_report)) {
        return;
    }

    bool shifted = report[0] & (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT);
    for (size_t i = 2; i < 8; ++i) {
        if (report[i] && !report_has_key(previous_report, report[i])) {
            post_key(report[i], true, shifted);
        }
        if (previous_report[i] && !report_has_key(report, previous_report[i])) {
            post_key(previous_report[i], false, false);
        }
    }

    update_modifier(report[0], previous_report[0],
                    HID_MOD_LEFT_CTRL | HID_MOD_RIGHT_CTRL, KEYD_RCTRL);
    update_modifier(report[0], previous_report[0],
                    HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT, KEYD_RSHIFT);
    update_modifier(report[0], previous_report[0],
                    HID_MOD_LEFT_ALT | HID_MOD_RIGHT_ALT, KEYD_RALT);
    memcpy(previous_report, report, sizeof(previous_report));
}

void I_KeyboardDisconnected(void)
{
    const uint8_t empty[8] = {0};
    I_KeyboardReport(empty, sizeof(empty));
}
