/*
 * SSSPlayer for PSP and PSP Go.
 * One EBOOT: Memory Stick is ms0:, PSP Go internal flash is ef0:.
 * The shell follows the Vita Terminus layout: header, storage list, mini player.
 */

#include "sss_browser.h"
#include "sss_player.h"
#include "sss_ui.h"
#include "sss_video.h"
#include "version.h"

#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <psputility.h>
#include <psputility_sysparam.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("SSSPlayer", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
/* Video decode needs a 4 MB aligned DDR workspace plus tables. */
PSP_HEAP_SIZE_KB(20480);

#define ROW_Y 60
#define ROW_H 20
#define ROW_N 7
#define MSG_Y 204
#define MINI_Y 222
#define HINT_Y 258
/* PSP Go slide-open bit (NewSlide). Not in the public enum; CFW often exposes it. */
#define SSS_CTRL_SLIDE_OPEN 0x20000000u

static volatile int g_running = 1;
static volatile int g_panel_power_off;
static int g_cross_accept = 1;
static int g_view_now;
static int g_blank_manual;
static int g_display_off;
static int g_is_go;
static int g_slide_bit_seen;
static int g_power_locked;
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

static int power_callback(int unknown, int pwrflags, void *common)
{
	(void)unknown;
	(void)common;
	/* Lid/power sleep request: we keep PowerLock so the console stays awake,
	 * but use the event to turn the LCD off while music continues. */
	if (pwrflags & (PSP_POWER_CB_POWER_SWITCH | PSP_POWER_CB_SUSPENDING |
	                PSP_POWER_CB_STANDBY))
		g_panel_power_off = 1;
	if (pwrflags & (PSP_POWER_CB_RESUMING | PSP_POWER_CB_RESUME_COMPLETE))
		g_panel_power_off = 0;
	return 0;
}

static int callback_thread(SceSize args, void *argp)
{
	int cbid;

	(void)args;
	(void)argp;
	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);
	cbid = sceKernelCreateCallback("Power Callback", power_callback, NULL);
	scePowerRegisterCallback(0, cbid);
	sceKernelSleepThreadCB();
	return 0;
}

static void detect_psp_go(void)
{
	SceUID fd = sceIoDopen("ef0:/");

	if (fd >= 0) {
		sceIoDclose(fd);
		g_is_go = 1;
	}
}

static int go_slide_closed(unsigned int buttons)
{
	if (!g_is_go)
		return 0;
	if (buttons & SSS_CTRL_SLIDE_OPEN)
		g_slide_bit_seen = 1;
	/* Only trust the bit after we have seen it once (avoids always-off on
	 * firmwares that never expose it to user mode). */
	if (!g_slide_bit_seen)
		return 0;
	return (buttons & SSS_CTRL_SLIDE_OPEN) == 0;
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

static void update_power(const SssPlayStatus *st)
{
	int audio_on = st->state == SSS_PLAY_PLAYING || st->state == SSS_PLAY_PAUSED;
	int want_fast = st->state == SSS_PLAY_PLAYING;

	if (want_fast != g_clock_fast) {
		if (want_fast)
			scePowerSetClockFrequency(333, 333, 166);
		else
			scePowerSetClockFrequency(222, 222, 111);
		g_clock_fast = want_fast;
	}
	/* Block auto-suspend and the Power-button sleep while audio is active.
	 * Do not tick DISPLAY so the panel can go black. */
	if (audio_on) {
		scePowerTick(PSP_POWER_TICK_SUSPEND);
		if (!g_power_locked) {
			scePowerLock(0);
			g_power_locked = 1;
		}
	} else if (g_power_locked) {
		scePowerUnlock(0);
		g_power_locked = 0;
	}
}

static void draw_brackets(int x, int y, int size)
{
	int mark = size / 4;
	if (mark < 4)
		mark = 4;
	ui_fill(x - 2, y - 2, mark, 2, UI_TEXT);
	ui_fill(x - 2, y - 2, 2, mark, UI_TEXT);
	ui_fill(x + size - mark + 2, y + size, mark, 2, UI_WARM);
	ui_fill(x + size, y + size - mark, 2, mark, UI_WARM);
}

static void draw_header(const char *scene)
{
	const char *by = "by SergioSSantiago";
	char bat[8];
	int pct = -1;
	int title_w;

	ui_fill(0, 0, UI_W, 36, UI_SURFACE);
	draw_brackets(8, 6, 24);
	ui_logo(8, 6, 24);
	ui_text(40, 6, 1, UI_TEXT, "SSSPlayer");
	title_w = ui_text_px("SSSPlayer", 1);
	ui_text(40 + title_w + 8, 6, 1, UI_WARM, by);
	ui_text(40, 20, 1, UI_DIM, scene);
	ui_fill(0, 35, UI_W, 1, UI_MUTED);
	ui_fill(300, 34, 120, 2, UI_TEXT);
	ui_fill(430, 34, 50, 2, UI_WARM);

	if (scePowerIsBatteryExist() == 1)
		pct = scePowerGetBatteryLifePercent();
	if (pct >= 0 && pct <= 100) {
		snprintf(bat, sizeof bat, "%d%%", pct);
		ui_text(UI_W - 8 - ui_text_px(bat, 1), 20, 1, UI_DIM, bat);
	}
}

static void draw_hint(const char *text)
{
	ui_fill(0, HINT_Y - 4, UI_W, UI_H - (HINT_Y - 4), UI_SURFACE);
	ui_fill(0, HINT_Y - 4, UI_W, 1, UI_MUTED);
	ui_text(8, HINT_Y, 1, UI_DIM, text);
}

static void draw_mini(const SssPlayStatus *st)
{
	char pos[16];
	char line[80];
	char name[24];
	int width = 0;

	ui_fill(8, MINI_Y, UI_W - 16, 30, UI_RAISED);
	ui_fill(8, MINI_Y, 3, 30, st->state == SSS_PLAY_PLAYING ? UI_TEXT : UI_DIM);
	if (st->error[0] != '\0') {
		sss_path_tail(line, sizeof line, st->error, 52);
		ui_text(18, MINI_Y + 8, 1, UI_DANGER, line);
		return;
	}
	if (st->state == SSS_PLAY_STOPPED && st->name[0] == '\0') {
		ui_text(18, MINI_Y + 11, 1, UI_DIM, "Nothing playing");
		return;
	}
	fmt_time(pos, sizeof pos, st->position_sec);
	sss_path_tail(name, sizeof name, st->name, 20);
	if (st->state == SSS_PLAY_PAUSED)
		snprintf(line, sizeof line, "Paused  %s  %s", pos, name);
	else if (st->state == SSS_PLAY_PLAYING)
		snprintf(line, sizeof line, "Play    %s  %s", pos, name);
	else
		snprintf(line, sizeof line, "Stop    %s", name);
	ui_text(18, MINI_Y + 4, 1, UI_TEXT, line);
	if (st->duration_sec > 0) {
		width = ((UI_W - 36) * st->position_sec) / st->duration_sec;
		if (width < 0)
			width = 0;
		if (width > UI_W - 36)
			width = UI_W - 36;
	}
	ui_fill(18, MINI_Y + 20, UI_W - 36, 4, UI_BG);
	if (width > 0)
		ui_fill(18, MINI_Y + 20, width, 4, UI_TEXT);
}

static void draw_browser(const SssPlayStatus *st, const char *accept,
                         const char *back)
{
	char crumb[80];
	char shown[40];
	char hint[80];
	int i;

	{
		char scene[32];
		snprintf(scene, sizeof scene, "Files  %s", SSSPLAYER_PSP_VERSION);
		draw_header(scene);
	}
	ui_fill(8, 40, UI_W - 16, 16, UI_RAISED);
	ui_fill(12, 43, 3, 10, UI_TEXT);
	if (browser_at_roots(&g_browser))
		snprintf(crumb, sizeof crumb, "Files");
	else {
		sss_path_tail(shown, sizeof shown, g_browser.path, 40);
		snprintf(crumb, sizeof crumb, "Files / %s", shown);
	}
	ui_text(20, 44, 1, UI_DIM, crumb);

	if (g_browser.cursor < g_list_top)
		g_list_top = g_browser.cursor;
	if (g_browser.cursor >= g_list_top + ROW_N)
		g_list_top = g_browser.cursor - ROW_N + 1;
	if (g_list_top < 0)
		g_list_top = 0;

	if (g_browser.count == 0 && g_browser.message[0]) {
		ui_fill(80, 100, 320, 40, UI_SURFACE);
		ui_text(96, 114, 1, UI_TEXT, g_browser.message);
	}

	for (i = 0; i < ROW_N; i++) {
		int index = g_list_top + i;
		const SssEntry *entry;
		int y = ROW_Y + i * ROW_H;
		unsigned int color = UI_TEXT;
		char label[48];

		if (index >= g_browser.count)
			break;
		entry = &g_browser.entries[index];
		ui_fill(8, y, UI_W - 16, ROW_H - 2,
		        index == g_browser.cursor ? UI_FOCUS : UI_SURFACE);
		if (index == g_browser.cursor)
			ui_fill(8, y, 3, ROW_H - 2, UI_TEXT);
		if (entry->is_dir == 3) {
			sss_path_tail(shown, sizeof shown, entry->name, 28);
			snprintf(label, sizeof label, "%s", shown);
			ui_text(UI_W - 16 - ui_text_px(g_browser.devices[index].id, 1),
			        y + 6, 1, UI_DIM, g_browser.devices[index].id);
		} else if (entry->is_dir == 1) {
			color = UI_TEXT;
			sss_path_tail(shown, sizeof shown, entry->name, 40);
			snprintf(label, sizeof label, "%s/", shown);
		} else if (entry->is_dir == 4) {
			color = UI_DIM;
			sss_path_tail(shown, sizeof shown, entry->name, 36);
			snprintf(label, sizeof label, "%s", shown);
			ui_text(UI_W - 16 - ui_text_px("video", 1), y + 6, 1, UI_MUTED,
			        "video");
		} else if (entry->is_dir == 2) {
			color = UI_DIM;
			sss_path_tail(shown, sizeof shown, entry->name, 40);
			snprintf(label, sizeof label, "%s", shown);
		} else {
			sss_path_tail(shown, sizeof shown, entry->name, 40);
			snprintf(label, sizeof label, "%s", shown);
		}
		ui_text(18, y + 6, 1, color, label);
	}

	if (g_browser.message[0] && g_browser.count > 0) {
		ui_fill(8, MSG_Y, UI_W - 16, 16, UI_RAISED);
		ui_text(18, MSG_Y + 4, 1, UI_WARM, g_browser.message);
	}

	draw_mini(st);
	snprintf(hint, sizeof hint,
	         "%s open  %s back  Square LCD  Triangle now  Start", accept,
	         back);
	draw_hint(hint);
}

static void draw_now(const SssPlayStatus *st, const char *accept, const char *back)
{
	char pos[16];
	char dur[16];
	char line[80];
	char hint[80];
	int width = 0;
	const char *state;

	if (st->state == SSS_PLAY_PAUSED)
		state = "Paused";
	else if (st->state == SSS_PLAY_PLAYING)
		state = "Now playing";
	else
		state = "Stopped";

	draw_header(state);
	sss_path_tail(line, sizeof line, st->name[0] ? st->name : "(no track)", 26);
	ui_text((UI_W - ui_text_px(line, 2)) / 2, 78, 2, UI_TEXT, line);

	fmt_time(pos, sizeof pos, st->position_sec);
	fmt_time(dur, sizeof dur, st->duration_sec);
	if (st->duration_sec > 0) {
		width = (400 * st->position_sec) / st->duration_sec;
		if (width < 0)
			width = 0;
		if (width > 400)
			width = 400;
	}
	ui_fill(40, 130, 400, 8, UI_SURFACE);
	if (width > 0)
		ui_fill(40, 130, width, 8, UI_TEXT);
	snprintf(line, sizeof line, "%s / %s", pos, dur);
	ui_text((UI_W - ui_text_px(line, 1)) / 2, 148, 1, UI_DIM, line);

	if (st->sample_rate > 0) {
		snprintf(line, sizeof line, "%d kbps   %d Hz   %s", st->bitrate_kbps,
		         st->sample_rate, st->channels == 1 ? "mono" : "stereo");
		ui_text((UI_W - ui_text_px(line, 1)) / 2, 168, 1, UI_MUTED, line);
	}
	if (st->error[0]) {
		sss_path_tail(line, sizeof line, st->error, 52);
		ui_text(32, 190, 1, UI_DANGER, line);
	}

	if (st->state == SSS_PLAY_STOPPED)
		snprintf(hint, sizeof hint,
		         "%s play  %s files  Square LCD  Triangle files  Start",
		         accept, back);
	else if (st->state == SSS_PLAY_PAUSED)
		snprintf(hint, sizeof hint,
		         "%s play  %s stop  Square LCD  Triangle files  Start", accept,
		         back);
	else
		snprintf(hint, sizeof hint,
		         "%s pause  %s stop  Square LCD  Triangle files  Start",
		         accept, back);
	draw_hint(hint);
}

static void draw_splash(void)
{
	const char *title = "SSSPlayer";
	const char *by = "by SergioSSantiago";
	const char *plat = "PSP and PSP Go";
	char ver[16];

	ui_begin();
	draw_brackets(192, 64, 96);
	ui_logo(192, 64, 96);
	ui_text((UI_W - ui_text_px(title, 2)) / 2, 168, 2, UI_TEXT, title);
	snprintf(ver, sizeof ver, "%s", SSSPLAYER_PSP_VERSION);
	ui_text((UI_W - ui_text_px(ver, 1)) / 2, 196, 1, UI_DIM, ver);
	ui_text((UI_W - ui_text_px(by, 1)) / 2, 216, 1, UI_WARM, by);
	ui_text((UI_W - ui_text_px(plat, 1)) / 2, 236, 1, UI_MUTED, plat);
	ui_end();
}

static void set_screen_off(int off)
{
	if (off == g_display_off)
		return;
	g_display_off = off;
	/* Real LCD off (not a lit black framebuffer). Console stays awake. */
	ui_display(off ? 0 : 1);
}

static void keep_screen_off(void)
{
	if (!g_display_off)
		set_screen_off(1);
	sceDisplayWaitVblankStart();
}

static void play_selection(void)
{
	int start = -1;
	int count;

	count = browser_mp3_list(&g_browser, g_names, SSS_PL_MAX, &start);
	if (start < 0 || count <= 0)
		return;
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
	static int seek_cool;

	if (seek_cool > 0)
		seek_cool--;

	if (pressed & PSP_CTRL_START) {
		g_running = 0;
		return;
	}
	if (pressed & PSP_CTRL_SQUARE)
		g_blank_manual = !g_blank_manual;
	if (pressed & PSP_CTRL_TRIANGLE) {
		g_view_now = !g_view_now;
		g_blank_manual = 0;
		g_panel_power_off = 0;
	}
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

	/* Seek only on edge press, with a short cool-down between seeks. */
	if ((pressed & PSP_CTRL_LEFT) && seek_cool == 0) {
		player_seek(-10);
		seek_cool = 18;
	}
	if ((pressed & PSP_CTRL_RIGHT) && seek_cool == 0) {
		player_seek(10);
		seek_cool = 18;
	}

	if (!pressed_accept(pressed) && !pressed_back(pressed))
		return;

	if (g_view_now) {
		if (pressed_accept(pressed))
			player_toggle_pause();
		if (pressed_back(pressed)) {
			player_stop();
			g_view_now = 0;
			g_blank_manual = 0;
			g_panel_power_off = 0;
		}
		return;
	}

	if (pressed_back(pressed))
		browser_up(&g_browser);
	if (pressed_accept(pressed)) {
		int action = browser_open(&g_browser);
		if (action == 2)
			play_selection();
		else if (action == 3) {
			const SssEntry *entry = &g_browser.entries[g_browser.cursor];
			char full[SSS_PATH_MAX];
			sss_path_join(full, sizeof full, g_browser.path, entry->name);
			g_view_now = 0;
			g_blank_manual = 0;
			g_panel_power_off = 0;
			set_screen_off(0);
			video_play(full);
		}
	}
}

int main(int argc, char *argv[])
{
	unsigned int prev_buttons = 0xFFFFFFFF;
	const char *launched = NULL;
	int splash;
	SceCtrlData pad;

	setup_callbacks();
	detect_psp_go();
	ui_init();
	draw_splash();

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	read_button_swap();
	scePowerSetClockFrequency(222, 222, 111);

	if (argc > 0 && argv && argv[0])
		launched = argv[0];
	browser_init(&g_browser, launched);
	player_init();

	for (splash = 0; splash < 90 && g_running; splash++) {
		sceCtrlReadBufferPositive(&pad, 1);
		if (pad.Buttons & ~SSS_CTRL_SLIDE_OPEN)
			break;
		if (pad.Buttons & SSS_CTRL_SLIDE_OPEN)
			g_slide_bit_seen = 1;
		draw_splash();
	}
	while (g_running) {
		sceCtrlPeekBufferPositive(&pad, 1);
		if (pad.Buttons & SSS_CTRL_SLIDE_OPEN)
			g_slide_bit_seen = 1;
		if (!(pad.Buttons & ~SSS_CTRL_SLIDE_OPEN))
			break;
		draw_splash();
	}
	prev_buttons = pad.Buttons;

	while (g_running) {
		SssPlayStatus status;
		const char *accept = g_cross_accept ? "X" : "O";
		const char *back = g_cross_accept ? "O" : "X";
		int audio_on;
		int slide_closed;
		int blank;

		sceCtrlReadBufferPositive(&pad, 1);
		player_get_status(&status);
		update_power(&status);
		audio_on = status.state == SSS_PLAY_PLAYING ||
		           status.state == SSS_PLAY_PAUSED;
		slide_closed = go_slide_closed(pad.Buttons);
		if (!slide_closed && (pad.Buttons & SSS_CTRL_SLIDE_OPEN))
			g_panel_power_off = 0;

		/* HOLD locks buttons (system) and turns the LCD off. */
		if (!(pad.Buttons & PSP_CTRL_HOLD))
			handle_input(pad.Buttons, prev_buttons);
		prev_buttons = pad.Buttons;
		if (!g_running)
			break;

		if (!audio_on) {
			g_blank_manual = 0;
			g_panel_power_off = 0;
		}

		/* LCD off only on HOLD, Square, Go slide closed, or panel power event.
		 * No idle auto-blank while the panel is open. */
		blank = (pad.Buttons & PSP_CTRL_HOLD) != 0 ||
		        (audio_on && g_blank_manual) ||
		        (audio_on && slide_closed) ||
		        (audio_on && g_panel_power_off);

		if (blank) {
			keep_screen_off();
			continue;
		}

		set_screen_off(0);
		ui_begin();
		if (g_view_now)
			draw_now(&status, accept, back);
		else
			draw_browser(&status, accept, back);
		ui_end();
	}

	set_screen_off(0);

	if (g_power_locked) {
		scePowerUnlock(0);
		g_power_locked = 0;
	}
	player_shutdown();
	scePowerSetClockFrequency(222, 222, 111);
	ui_shutdown();
	sceKernelExitGame();
	return 0;
}
