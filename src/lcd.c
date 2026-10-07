/*
 * Turn the LCD/backlight fully off from user mode on CFW (ARK/PRO/ME).
 * sceGuDisplay(false) only blanks pixels; brightness needs kernel privilege.
 */
#include "sss_lcd.h"

#include <kubridge.h>
#include <string.h>
#include <systemctrl.h>

/* sceDisplay_driver NIDs (6.xx / pspdev stubs). */
#define NID_SET_BRIGHTNESS 0xC66D3C9E
#define NID_GET_BRIGHTNESS 0xA8BAC431
#define NID_DISPLAY_ENABLE 0x3F132D43
#define NID_DISPLAY_DISABLE 0xA7E61E68

static void *g_set_bri;
static void *g_get_bri;
static void *g_enable;
static void *g_disable;
static int g_ready;
static int g_saved = -1;
static int g_hw_off;

static void *find_disp(unsigned int nid)
{
	void *p;

	p = (void *)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver",
	                                 nid);
	if (p)
		return p;
	return (void *)sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", nid);
}

static int kcall1(void *fn, unsigned int a0)
{
	KernelCallArg args;

	if (!fn)
		return -1;
	memset(&args, 0, sizeof args);
	args.arg1 = a0;
	return kuKernelCall(fn, &args);
}

static int kcall2(void *fn, unsigned int a0, unsigned int a1)
{
	KernelCallArg args;

	if (!fn)
		return -1;
	memset(&args, 0, sizeof args);
	args.arg1 = a0;
	args.arg2 = a1;
	return kuKernelCall(fn, &args);
}

static int kcall_get_bri(int *level)
{
	KernelCallArg args;
	int unk = 0;

	if (!g_get_bri || !level)
		return -1;
	memset(&args, 0, sizeof args);
	args.arg1 = (unsigned int)level;
	args.arg2 = (unsigned int)&unk;
	return kuKernelCall(g_get_bri, &args);
}

void lcd_init(void)
{
	g_set_bri = find_disp(NID_SET_BRIGHTNESS);
	g_get_bri = find_disp(NID_GET_BRIGHTNESS);
	g_enable = find_disp(NID_DISPLAY_ENABLE);
	g_disable = find_disp(NID_DISPLAY_DISABLE);
	g_ready = (g_set_bri != NULL);
}

int lcd_available(void)
{
	return g_ready;
}

void lcd_set(int on)
{
	if (!g_ready)
		return;

	if (on) {
		if (g_hw_off) {
			kcall1(g_enable, 0);
			g_hw_off = 0;
		}
		if (g_saved >= 0) {
			kcall2(g_set_bri, (unsigned int)g_saved, 0);
			g_saved = -1;
		}
	} else {
		int level = 0;

		if (g_saved < 0) {
			if (kcall_get_bri(&level) == 0 && level > 0 && level <= 100)
				g_saved = level;
			else
				g_saved = 68;
		}
		kcall2(g_set_bri, 0, 0);
		if (!g_hw_off) {
			kcall1(g_disable, 0);
			g_hw_off = 1;
		}
	}
}
