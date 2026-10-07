#ifndef SSS_LCD_H
#define SSS_LCD_H

void lcd_init(void);
int lcd_available(void);
/* 0 = backlight/LCD off, 1 = restore. Needs CFW (kubridge). */
void lcd_set(int on);

#endif
