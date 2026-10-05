#include "sss_ui.h"

#include "font8x8_basic.h"
#include "sss_logo.h"

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <string.h>

#define BUF_WIDTH 512
#define FRAME_SIZE (BUF_WIDTH * UI_H * 4)

static unsigned int __attribute__((aligned(16))) list[262144];
static unsigned int __attribute__((aligned(16))) font_tex[128 * 64];
static int font_ready;

typedef struct Vertex {
	float u, v;
	unsigned int color;
	short x, y, z;
	short pad;
} Vertex;

static void build_font(void)
{
	int c;
	int px;
	int py;

	memset(font_tex, 0, sizeof font_tex);
	for (c = 32; c < 128; c++) {
		int index = c - 32;
		int col = index % 16;
		int row = index / 16;

		for (py = 0; py < 8; py++) {
			unsigned char bits = font8x8_basic[c][py];
			for (px = 0; px < 8; px++) {
				int tx = col * 8 + px;
				int ty = row * 8 + py;
				if (bits & (1u << px))
					font_tex[ty * 128 + tx] = 0xFFFFFFFF;
			}
		}
	}
	sceKernelDcacheWritebackRange(font_tex, sizeof font_tex);
	sceKernelDcacheWritebackRange((void *)sss_logo_64, sizeof sss_logo_64);
	font_ready = 1;
}

static void bind_font(void)
{
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_8888, 0, 0, 0);
	sceGuTexImage(0, 128, 64, 128, font_tex);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	sceGuTexScale(1.0f, 1.0f);
	sceGuTexOffset(0.0f, 0.0f);
}

static void sprite(float u0, float v0, float u1, float v1, int x, int y,
                   int w, int h, unsigned int color)
{
	Vertex *v = sceGuGetMemory(2 * (int)sizeof(Vertex));

	if (!v || w <= 0 || h <= 0)
		return;
	v[0].u = u0;
	v[0].v = v0;
	v[0].color = color;
	v[0].x = (short)x;
	v[0].y = (short)y;
	v[0].z = 0;
	v[0].pad = 0;
	v[1].u = u1;
	v[1].v = v1;
	v[1].color = color;
	v[1].x = (short)(x + w);
	v[1].y = (short)(y + h);
	v[1].z = 0;
	v[1].pad = 0;
	sceGuDrawArray(GU_SPRITES,
	               GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_16BIT |
	                   GU_TRANSFORM_2D,
	               2, NULL, v);
}

void ui_init(void)
{
	build_font();
	sceGuInit();
	sceGuStart(GU_DIRECT, list);
	sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_WIDTH);
	sceGuDispBuffer(UI_W, UI_H, (void *)FRAME_SIZE, BUF_WIDTH);
	sceGuDepthBuffer((void *)(FRAME_SIZE * 2), BUF_WIDTH);
	sceGuOffset(2048 - (UI_W / 2), 2048 - (UI_H / 2));
	sceGuViewport(2048, 2048, UI_W, UI_H);
	sceGuScissor(0, 0, UI_W, UI_H);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuEnable(GU_BLEND);
	sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
	sceGuFinish();
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
}

void ui_shutdown(void)
{
	sceGuTerm();
}

void ui_begin(void)
{
	sceGuStart(GU_DIRECT, list);
	sceGuClearColor(UI_BG);
	sceGuClear(GU_COLOR_BUFFER_BIT);
	sceGuEnable(GU_BLEND);
	sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
	if (font_ready)
		bind_font();
}

void ui_end(void)
{
	sceGuFinish();
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
	sceDisplayWaitVblankStart();
	sceGuSwapBuffers();
}

void ui_fill(int x, int y, int w, int h, unsigned int color)
{
	sceGuDisable(GU_TEXTURE_2D);
	sprite(0, 0, 0, 0, x, y, w, h, color);
}

void ui_logo(int x, int y, int size)
{
	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_8888, 0, 0, 0);
	sceGuTexImage(0, 64, 64, 64, (void *)sss_logo_64);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sprite(0, 0, 64, 64, x, y, size, size, 0xFFFFFFFF);
	bind_font();
}

int ui_text_px(const char *text, int scale)
{
	if (!text || scale < 1)
		return 0;
	return (int)strlen(text) * 8 * scale;
}

void ui_text(int x, int y, int scale, unsigned int color, const char *text)
{
	const unsigned char *s;

	if (!text || scale < 1)
		return;
	bind_font();
	for (s = (const unsigned char *)text; *s; s++) {
		unsigned char c = *s;
		int index;
		int col;
		int row;
		int gw = 8 * scale;

		if (x >= UI_W - 4)
			break;
		if (c < 32 || c > 127)
			c = '?';
		index = c - 32;
		col = index % 16;
		row = index / 16;
		sprite((float)(col * 8), (float)(row * 8), (float)(col * 8 + 8),
		       (float)(row * 8 + 8), x, y, gw, 8 * scale, color);
		x += gw;
	}
}
