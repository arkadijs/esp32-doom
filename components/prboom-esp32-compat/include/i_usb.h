#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "d_event.h"

void usb_init(void);
void I_USB_Poll(void);
void I_USB_AccumulateMouse(uint8_t buttons, int dx, int dy);
bool I_USB_PostEvent(const event_t *event);

void I_KeyboardReport(const uint8_t *report, size_t len);
void I_KeyboardDisconnected(void);
void I_KeyboardKeyDown(uint8_t hid_keycode, bool shifted);
void I_KeyboardKeyUp(uint8_t hid_keycode);

void I_MouseReport12(const uint8_t *report, size_t len);
void I_MouseReport(const uint8_t *report, size_t len);
void I_MouseDisconnected(void);
void I_MouseTick(void);
void I_SandioReport(const uint8_t *report, size_t len);
void I_SandioConnected(void);
void I_SandioDisconnected(void);
