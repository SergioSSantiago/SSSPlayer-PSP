#ifndef SSS_UI_H
#define SSS_UI_H

/* Terminus palette from the Vita shell, stored as PSP ABGR. */
#define UI_BG      0xFF040A04
#define UI_SURFACE 0xFF0A1A0A
#define UI_RAISED  0xFF0E280E
#define UI_FOCUS   0xFF1A5A1A
#define UI_TEXT    0xFF44FF44
#define UI_DIM     0xFF1A8A1A
#define UI_MUTED   0xFF146414
#define UI_WARM    0xFF4E94D5
#define UI_DANGER  0xFF525FEF

#define UI_W 480
#define UI_H 272

void ui_init(void);
void ui_shutdown(void);
/* GU_FALSE turns the LCD/backlight off without suspending the console. */
void ui_display(int on);
void ui_begin(void);
void ui_end(void);
void ui_fill(int x, int y, int w, int h, unsigned int color);
void ui_text(int x, int y, int scale, unsigned int color, const char *text);
void ui_logo(int x, int y, int size);
int ui_text_px(const char *text, int scale);

#endif
