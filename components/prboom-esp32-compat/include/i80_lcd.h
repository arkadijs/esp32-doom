#ifndef I80_LCD_H
#define I80_LCD_H

// #include "doomtype.h"
typedef unsigned char byte;

extern uint16_t lcdpal[256];

void i80_lcd_send(byte *screen);
void i80_lcd_init();

#endif // I80_LCD_H
