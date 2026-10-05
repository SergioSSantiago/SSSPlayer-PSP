/*
 * SSSPlayer for PSP and PSP Go.
 * One EBOOT: Memory Stick is ms0:, PSP Go internal flash is ef0:.
 */

#include "sss_browser.h"
#include "sss_player.h"
#include "version.h"

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <psputility.h>
#include <psputility_sysparam.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("SSSPlayer", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(8192);

#define printf pspDebugScreenPrintf

#define COL_TEXT 0xffffffff
#define COL_DIM 0xffaaaaaa
#define COL_DIR 0xffffccaa
#define COL_PICK 0xff66ccff
#define COL_OK 0xff66ff88
#define COL_WARN 0xff00ffff
#define COL_ERR 0xff4444ff
#define COL_BG 0xff20140c

#define LIST_ROWS 16

static volatile int g_running = 1;
static int g_cross_accept = 1;
static int g_view_now;
static int g_list_top;
static int g_clock_fast;
static SssBrowser g_browser;
static char g_names[SSS_PL_MAX][SSS_NAME_MAX];

static int exit_callback(int arg1, int arg2, void *common)
{
	(void)arg1;
	(void)arg2;
	(void)common;
	g_running = 0;
	return 0;
}

static int callback_thread(SceSize args, void *argp)
{
	int cbid;

	(void)args;
	(void)argp;
	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);
	sceKernelSleepThreadCB();
	return 0;
}

static void setup_callbacks(void)
{
	int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11,
	                                 0xFA0, 0, 0);
	if (thid >= 0)
		sceKernelStartThread(thid, 0, 0);
}

static void read_button_swap(void)
{
	int swap = PSP_UTILITY_ACCEPT_CROSS;

	if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP, &swap) < 0)
		swap = PSP_UTILITY_ACCEPT_CROSS;
	g_cross_accept = (swap == PSP_UTILITY_ACCEPT_CROSS);
}

static void fmt_time(char *dst, size_t n, int sec)
{
	if (sec < 0)
		sec = 0;
	if (sec >= 3600)
		snprintf(dst, n, "%d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
	else
		snprintf(dst, n, "%d:%02d", sec / 60, sec % 60);
}

static void draw_at(int x, int y, unsigned int color, const char *text)
{
	pspDebugScreenSetTextColor(color);
	pspDebugScreenSetXY(x, y);
	printf("%s", text);
}

static void update_power(const SssPlayStatus *st)
{
	int want_fast = st->state == SSS_PLAY_PLAYING;

	if (want_fast != g_clock_fast) {
		if (want_fast)
			scePowerSetClockFrequency(333, 333, 166);
		else
			scePowerSetClockFrequency(222, 222, 111);
		g_clock_fast = want_fast;
	}
	if (want_fast)
		scePowerTick(PSP_POWER_TICK_SUSPEND);
}

static void draw_status_line(int y, const SssPlayStatus *st)
{
	char pos[16];
	char name[40];
	char line[68];

	if (st->error[0] != '\0') {
		sss_path_tail(line, sizeof line, st->error, 60);
		draw_at(0, y, COL_ERR, line);
		return;
	}
	if (st->state == SSS_PLAY_STOPPED && st->name[0] == '\0') {
		draw_at(0, y, COL_DIM, "Nothing playing");
		return;
	}

	fmt_time(pos, sizeof pos, st->position_sec);
	sss_path_tail(name, sizeof name, st->name, 32);
	if (st->state == SSS_PLAY_PAUSED)
		snprintf(line, sizeof line, "Paused %s  %s", pos, name);
	else if (st->state == SSS_PLAY_PLAYING)
		snprintf(line, sizeof line, "Play   %s  %s", pos, name);
	else
		snprintf(line, sizeof line, "Stop   %s", name);
	draw_at(0, y, st->state == SSS_PLAY_PAUSED ? COL_WARN : COL_OK, line);
}

static void draw_browser(const SssPlayStatus *st, const char *accept,
                         const char *back)
{
	char line[96];
	char shown[80];
	int y;
	int i;
	int pct;

	sss_path_tail(shown, sizeof shown, g_browser.path[0] ? g_browser.path : "(no storage)", 58);
	draw_at(0, 1, COL_TEXT, shown);

	if (g_browser.message[0])
		draw_at(0, 2, COL_WARN, g_browser.message);

	if (g_browser.cursor < g_list_top)
		g_list_top = g_browser.cursor;
	if (g_browser.cursor >= g_list_top + LIST_ROWS)
		g_list_top = g_browser.cursor - LIST_ROWS + 1;
	if (g_list_top < 0)
		g_list_top = 0;

	for (i = 0; i < LIST_ROWS; i++) {
		int index = g_list_top + i;
		const SssEntry *entry;
		unsigned int color = COL_TEXT;
		char mark = ' ';

		y = 4 + i;
		if (index >= g_browser.count) {
			draw_at(0, y, COL_TEXT, "");
			continue;
		}
		entry = &g_browser.entries[index];
		if (index == g_browser.cursor) {
			mark = '>';
			color = COL_PICK;
		} else if (entry->is_dir == 1) {
			color = COL_DIR;
		} else if (entry->is_dir == 2) {
			color = COL_WARN;
		}

		sss_path_tail(shown, sizeof shown, entry->name, 54);
		if (entry->is_dir == 1)
			snprintf(line, sizeof line, "%c %s/", mark, shown);
		else
			snprintf(line, sizeof line, "%c %s", mark, shown);
		draw_at(0, y, color, line);
	}

	draw_status_line(21, st);

	snprintf(line, sizeof line, "%s open   %s back   Tri now   Start quit",
	         accept, back);
	draw_at(0, 23, COL_DIM, line);
	draw_at(0, 24, COL_DIM, "L/R track   Select pause   Left/Right 10s");

	pct = -1;
	if (scePowerIsBatteryExist() == 1)
		pct = scePowerGetBatteryLifePercent();
	if (pct >= 0 && pct <= 100) {
		snprintf(line, sizeof line, "%d%%", pct);
		draw_at(54, 0, COL_DIM, line);
	}
}

static void draw_now(const SssPlayStatus *st, const char *accept, const char *back)
{
	char pos[16];
	char dur[16];
	char line[96];
	char bar[40];
	int filled = 0;
	int i;
	const char *state;

	if (st->state == SSS_PLAY_PAUSED)
		state = "Paused";
	else if (st->state == SSS_PLAY_PLAYING)
		state = "Now playing";
	else
		state = "Stopped";

	draw_at(0, 2, COL_TEXT, state);
	sss_path_tail(line, sizeof line, st->name[0] ? st->name : "(no track)", 60);
	draw_at(0, 4, COL_PICK, line);

	fmt_time(pos, sizeof pos, st->position_sec);
	fmt_time(dur, sizeof dur, st->duration_sec);
	if (st->duration_sec > 0) {
		filled = (st->position_sec * 30) / st->duration_sec;
		if (filled > 30)
			filled = 30;
		if (filled < 0)
			filled = 0;
	}
	bar[0] = '[';
	for (i = 0; i < 30; i++)
		bar[1 + i] = (i < filled) ? '=' : '-';
	bar[31] = ']';
	bar[32] = '\0';
	snprintf(line, sizeof line, "%s  %s / %s", bar, pos, dur);
	draw_at(0, 6, COL_OK, line);

	if (st->sample_rate > 0) {
		snprintf(line, sizeof line, "%d kbps   %d Hz   %s",
		         st->bitrate_kbps, st->sample_rate,
		         st->channels == 1 ? "mono" : "stereo");
		draw_at(0, 8, COL_DIM, line);
	}

	if (st->error[0])
		draw_at(0, 10, COL_ERR, st->error);

	snprintf(line, sizeof line, "%s pause   %s stop   Tri files", accept, back);
	draw_at(0, 22, COL_DIM, line);
	draw_at(0, 23, COL_DIM, "Left/Right 10s   L prev   R next");
	draw_at(0, 24, COL_DIM, "Select pause   Start quit");
}

static void play_selection(void)
{
	int start = -1;
	int count;

	count = browser_mp3_list(&g_browser, g_names, SSS_PL_MAX, &start);
	if (start < 0 || count <= 0) {
		return;
	}
	if (player_play_list(g_browser.path, g_names, count, start) == 0)
		g_view_now = 1;
}

static int pressed_accept(unsigned int pressed)
{
	if (g_cross_accept)
		return (pressed & PSP_CTRL_CROSS) != 0;
	return (pressed & PSP_CTRL_CIRCLE) != 0;
}

static int pressed_back(unsigned int pressed)
{
	if (g_cross_accept)
		return (pressed & PSP_CTRL_CIRCLE) != 0;
	return (pressed & PSP_CTRL_CROSS) != 0;
}

static void handle_input(unsigned int buttons, unsigned int prev)
{
	unsigned int pressed = buttons & ~prev;
	static int up_hold;
	static int down_hold;
	static int left_hold;
	static int right_hold;

	if (pressed & PSP_CTRL_START) {
		g_running = 0;
		return;
	}
	if (pressed & PSP_CTRL_TRIANGLE)
		g_view_now = !g_view_now;
	if (pressed & PSP_CTRL_SELECT)
		player_toggle_pause();
	if (pressed & PSP_CTRL_LTRIGGER)
		player_prev();
	if (pressed & PSP_CTRL_RTRIGGER)
		player_next();

	if (buttons & PSP_CTRL_UP) {
		if (up_hold == 0 || (up_hold >= 14 && (up_hold % 3) == 0)) {
			if (!g_view_now)
				browser_move(&g_browser, -1);
		}
		up_hold++;
	} else {
		up_hold = 0;
	}

	if (buttons & PSP_CTRL_DOWN) {
		if (down_hold == 0 || (down_hold >= 14 && (down_hold % 3) == 0)) {
			if (!g_view_now)
				browser_move(&g_browser, 1);
		}
		down_hold++;
	} else {
		down_hold = 0;
	}

	if (buttons & PSP_CTRL_LEFT) {
		if (left_hold == 0 || (left_hold >= 12 && (left_hold % 6) == 0))
			player_seek(-10);
		left_hold++;
	} else {
		left_hold = 0;
	}

	if (buttons & PSP_CTRL_RIGHT) {
		if (right_hold == 0 || (right_hold >= 12 && (right_hold % 6) == 0))
			player_seek(10);
		right_hold++;
	} else {
		right_hold = 0;
	}

	if (!pressed_accept(pressed) && !pressed_back(pressed))
		return;

	if (g_view_now) {
		if (pressed_accept(pressed))
			player_toggle_pause();
		if (pressed_back(pressed)) {
			player_stop();
			g_view_now = 0;
		}
		return;
	}

	if (pressed_back(pressed))
		browser_up(&g_browser);
	if (pressed_accept(pressed)) {
		int action = browser_open(&g_browser);
		if (action == 2)
			play_selection();
	}
}

int main(int argc, char *argv[])
{
	unsigned int prev_buttons = 0;
	const char *launched = NULL;

	setup_callbacks();
	pspDebugScreenInit();
	pspDebugScreenSetBackColor(COL_BG);
	pspDebugScreenSetTextColor(COL_TEXT);
	pspDebugScreenClear();
	draw_at(0, 0, COL_TEXT, SSSPLAYER_PSP_NAME);
	draw_at(0, 2, COL_DIM, "Loading...");

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	read_button_swap();
	scePowerSetClockFrequency(222, 222, 111);

	if (argc > 0 && argv && argv[0])
		launched = argv[0];
	browser_init(&g_browser, launched);
	player_init();

	while (g_running) {
		SceCtrlData pad;
		SssPlayStatus status;
		const char *accept = g_cross_accept ? "X" : "O";
		const char *back = g_cross_accept ? "O" : "X";
		char title[64];

		sceDisplayWaitVblankStart();
		sceCtrlReadBufferPositive(&pad, 1);
		player_get_status(&status);
		update_power(&status);
		handle_input(pad.Buttons, prev_buttons);
		prev_buttons = pad.Buttons;
		if (!g_running)
			break;

		pspDebugScreenSetBackColor(COL_BG);
		pspDebugScreenClear();
		snprintf(title, sizeof title, "%s %s", SSSPLAYER_PSP_NAME,
		         SSSPLAYER_PSP_VERSION);
		draw_at(0, 0, COL_TEXT, title);

		if (g_view_now)
			draw_now(&status, accept, back);
		else
			draw_browser(&status, accept, back);
	}

	player_shutdown();
	scePowerSetClockFrequency(222, 222, 111);
	sceKernelExitGame();
	return 0;
}
