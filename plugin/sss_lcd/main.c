/*
 * Tiny kernel helper: turn the PSP LCD/backlight fully off/on.
 * User homebrew cannot do this reliably; Square/HOLD load this PRX.
 */
#include <pspkernel.h>
#include <pspdisplay_kernel.h>
#include <pspsdk.h>

void sceDisplayEnable(void);
void sceDisplayDisable(void);

PSP_MODULE_INFO("SssLcd", 0x1007, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

static int g_saved = -1;
static int g_off;

int sssLcdSet(int on)
{
	unsigned int k1 = pspSdkSetK1(0);

	if (on) {
		if (g_off) {
			sceDisplayEnable();
			g_off = 0;
		}
		if (g_saved >= 0) {
			sceDisplaySetBrightness(g_saved, 0);
			g_saved = -1;
		}
	} else {
		int level = 0;
		int unk = 0;

		if (g_saved < 0) {
			sceDisplayGetBrightness(&level, &unk);
			g_saved = (level > 0 && level <= 100) ? level : 68;
		}
		sceDisplaySetBrightness(0, 0);
		if (!g_off) {
			sceDisplayDisable();
			g_off = 1;
		}
	}

	pspSdkSetK1(k1);
	return 0;
}

int module_start(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	return 0;
}

int module_stop(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	sssLcdSet(1);
	return 0;
}
