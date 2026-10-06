/*
 * H.264/MP4 playback via the PSP Media Engine NAL path.
 * Structure follows working homebrew (PMPlayer / pocketfin), adapted for SSSPlayer.
 */

#include "sss_video.h"
#include "sss_mp4.h"
#include "sss_ui.h"
#include "sss_player.h"

#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <psppower.h>
#include <pspmpeg.h>
#include <psputility.h>
#include <psputility_modules.h>
#include <psputils.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_STRIDE 512
#define VIDEO_HEIGHT 288
#define VIDEO_MODE_SD 1
#define VIDEO_DDRTOP_SIZE 0x400000u
#define VIDEO_DDRTOP_ALIGN 0x400000u
#define VIDEO_AU_OFFSET 0x100000u
#define VIDEO_WORK_MAX 0x20000u
#define VIDEO_MAX_IMAGES 4
#define VIDEO_SAMPLE_MAX (512 * 1024)
#define VIDEO_FRAME_FIRST 3
#define VIDEO_FRAME_NEXT 0

typedef struct {
	void *sps_buffer;
	SceInt32 sps_size;
	void *pps_buffer;
	SceInt32 pps_size;
	SceInt32 nal_prefix_size;
	void *nal_buffer;
	SceInt32 nal_size;
	SceInt32 mode;
} AvcNalStruct;

/* Declared by src/mpeg_nal.S (not in stock libpspmpeg). */
SceInt32 sceMpegGetAvcNalAu(SceMpeg *mpeg, AvcNalStruct *nal, SceMpegAu *au);

typedef struct {
	int did_init;
	int did_create;
	SceMpeg mpeg;
	SceMpegRingbuffer ring;
	int sps_size;
	int pps_size;
	int nal_prefix_size;
	char err[96];
} AvcDecoder;

static uint8_t g_param_sets[512] __attribute__((aligned(64)));
static uint8_t g_au[64] __attribute__((aligned(64)));
static uint8_t g_work[VIDEO_WORK_MAX] __attribute__((aligned(64)));
static uint8_t g_sample[VIDEO_SAMPLE_MAX] __attribute__((aligned(64)));

static void avc_fail(AvcDecoder *d, const char *msg)
{
	snprintf(d->err, sizeof d->err, "%s", msg);
}

static void avc_failc(AvcDecoder *d, const char *msg, int code)
{
	snprintf(d->err, sizeof d->err, "%s %08X", msg, (unsigned)code);
}

static void avc_close(AvcDecoder *d)
{
	if (d->did_create) {
		sceMpegDelete(&d->mpeg);
		d->did_create = 0;
	}
	if (d->did_init) {
		sceMpegFinish();
		d->did_init = 0;
	}
}

static int load_av(int id)
{
	int rc = sceUtilityLoadModule(id);
	if (rc == (int)SCE_ERROR_MODULE_ALREADY_LOADED)
		return 0;
	return rc;
}

static int avc_open(AvcDecoder *d, const uint8_t *sps, int sps_len,
                    const uint8_t *pps, int pps_len, int nal_len, void *ddrtop)
{
	SceMpegAu *au = (SceMpegAu *)(void *)g_au;
	SceMpegAvcMode mode;
	int rc;
	int size;

	memset(d, 0, sizeof *d);
	if (!sps || !pps || sps_len <= 0 || pps_len <= 0 || !ddrtop) {
		avc_fail(d, "Bad AVC params");
		return -1;
	}
	if ((uintptr_t)ddrtop & (VIDEO_DDRTOP_ALIGN - 1u)) {
		avc_fail(d, "DDR not aligned");
		return -1;
	}
	if ((unsigned)sps_len + (unsigned)pps_len > sizeof g_param_sets) {
		avc_fail(d, "SPS/PPS too big");
		return -1;
	}

	d->sps_size = sps_len;
	d->pps_size = pps_len;
	d->nal_prefix_size = nal_len;
	memcpy(g_param_sets, sps, (size_t)sps_len);
	memcpy(g_param_sets + sps_len, pps, (size_t)pps_len);
	sceKernelDcacheWritebackRange(g_param_sets,
	                              ((unsigned)sps_len + (unsigned)pps_len + 63u) &
	                                  ~63u);

	if (load_av(PSP_MODULE_AV_AVCODEC) < 0) {
		avc_fail(d, "Load AVCODEC");
		return -1;
	}
	if (load_av(PSP_MODULE_AV_MPEGBASE) < 0) {
		avc_fail(d, "Load MPEGBASE");
		return -1;
	}

	rc = sceMpegInit();
	if (rc != 0) {
		sceMpegFinish();
		rc = sceMpegInit();
	}
	if (rc != 0) {
		avc_failc(d, "MpegInit", rc);
		return -1;
	}
	d->did_init = 1;

	size = sceMpegQueryMemSize(VIDEO_MODE_SD);
	if (size <= 0 || (unsigned)size > sizeof g_work) {
		avc_failc(d, "MpegMem", size);
		avc_close(d);
		return -1;
	}

	rc = sceMpegCreate(&d->mpeg, g_work, size, &d->ring, VIDEO_STRIDE,
	                   VIDEO_MODE_SD, (SceInt32)(uintptr_t)ddrtop);
	if (rc != 0) {
		avc_failc(d, "MpegCreate", rc);
		avc_close(d);
		return -1;
	}
	d->did_create = 1;

	memset(g_au, 0xFF, sizeof g_au);
	rc = sceMpegInitAu(&d->mpeg, (uint8_t *)ddrtop + VIDEO_AU_OFFSET, au);
	if (rc != 0) {
		avc_failc(d, "MpegInitAu", rc);
		avc_close(d);
		return -1;
	}

	mode.iUnk0 = -1;
	mode.iPixelFormat = SCE_MPEG_AVC_FORMAT_8888;
	rc = sceMpegAvcDecodeMode(&d->mpeg, &mode);
	if (rc != 0) {
		avc_failc(d, "DecodeMode", rc);
		avc_close(d);
		return -1;
	}
	return 0;
}

static int avc_decode(AvcDecoder *d, const void *sample, int size, void **frames,
                      int frame_mode)
{
	SceMpegAu *au = (SceMpegAu *)(void *)g_au;
	AvcNalStruct nal;
	SceInt32 pic_num = 0;
	int rc;
	uintptr_t from;
	uintptr_t to;

	if (!d->did_create) {
		avc_fail(d, "Decoder closed");
		return -1;
	}
	if (!sample || size <= 0)
		return 0;

	nal.sps_buffer = g_param_sets;
	nal.sps_size = d->sps_size;
	nal.pps_buffer = g_param_sets + d->sps_size;
	nal.pps_size = d->pps_size;
	nal.nal_prefix_size = d->nal_prefix_size;
	nal.nal_buffer = (void *)(uintptr_t)sample;
	nal.nal_size = size;
	nal.mode = frame_mode;

	au->iPtsMSB = 0xFFFFFFFFu;
	au->iPts = 0xFFFFFFFFu;
	au->iDtsMSB = 0xFFFFFFFFu;
	au->iDts = 0xFFFFFFFFu;

	from = (uintptr_t)sample & ~(uintptr_t)63;
	to = ((uintptr_t)sample + (unsigned)size + 63u) & ~(uintptr_t)63;
	sceKernelDcacheWritebackRange((void *)from, (unsigned)(to - from));

	rc = sceMpegGetAvcNalAu(&d->mpeg, &nal, au);
	if (rc != 0) {
		avc_failc(d, "NalAu", rc);
		return -1;
	}
	rc = sceMpegAvcDecode(&d->mpeg, au, VIDEO_STRIDE, frames, &pic_num);
	if (rc != 0) {
		avc_failc(d, "AvcDecode", rc);
		return -1;
	}
	return (int)pic_num;
}

static void *uncached(void *p)
{
	return (void *)(0x40000000u | (uintptr_t)p);
}

static void show_message(const char *title, const char *body)
{
	int i;
	SceCtrlData pad;
	unsigned int prev = 0xFFFFFFFF;

	for (i = 0; i < 180; i++) {
		ui_begin();
		ui_fill(0, 0, UI_W, UI_H, UI_BG);
		ui_fill(24, 80, UI_W - 48, 100, UI_SURFACE);
		ui_text(40, 100, 1, UI_WARM, title);
		ui_text(40, 124, 1, UI_TEXT, body);
		ui_text(40, 150, 1, UI_DIM, "Press O / X to go back");
		ui_end();
		sceCtrlReadBufferPositive(&pad, 1);
		if ((pad.Buttons & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)) &&
		    !(prev & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)))
			break;
		prev = pad.Buttons;
	}
}

int video_play(const char *path)
{
	SssMp4 mp4;
	AvcDecoder dec;
	void *ddrtop = NULL;
	void *frame_bufs[VIDEO_MAX_IMAGES];
	void *vram;
	void *disp[2];
	int disp_i = 0;
	int first = 1;
	int paused = 0;
	int running = 1;
	int pics;
	int n;
	int k;
	unsigned int prev = 0xFFFFFFFF;
	SceCtrlData pad;
	char body[96];

	player_stop();
	sceKernelDelayThread(150 * 1000);
	scePowerSetClockFrequency(333, 333, 166);
	scePowerLock(0);
	scePowerTick(PSP_POWER_TICK_SUSPEND);

	memset(&mp4, 0, sizeof mp4);
	memset(&dec, 0, sizeof dec);

	if (sss_mp4_open(&mp4, path) < 0) {
		show_message("Cannot open video", mp4.error);
		goto done;
	}
	if (mp4.width == 0 || mp4.height == 0 || mp4.width > 480 ||
	    mp4.height > 272) {
		snprintf(body, sizeof body, "Need H.264 <= 480x272 (got %ux%u)",
		         (unsigned)mp4.width, (unsigned)mp4.height);
		show_message("Unsupported video", body);
		goto done;
	}

	ddrtop = memalign(VIDEO_DDRTOP_ALIGN, VIDEO_DDRTOP_SIZE);
	if (!ddrtop) {
		show_message("Video error", "Need more free RAM for decode");
		goto done;
	}
	memset(ddrtop, 0, 64);

	if (avc_open(&dec, mp4.sps, mp4.sps_len, mp4.pps, mp4.pps_len,
	             mp4.nal_length_size, ddrtop) < 0) {
		show_message("Decoder failed", dec.err);
		goto done;
	}

	vram = sceGeEdramGetAddr();
	disp[0] = vram;
	disp[1] = (uint8_t *)vram + VIDEO_STRIDE * VIDEO_HEIGHT * 4;
	for (k = 0; k < VIDEO_MAX_IMAGES; k++)
		frame_bufs[k] = uncached(disp[0]);

	sceDisplaySetMode(0, 480, 272);
	sceDisplaySetFrameBuf(disp[0], VIDEO_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888,
	                      PSP_DISPLAY_SETBUF_NEXTFRAME);

	ui_begin();
	ui_fill(0, 0, UI_W, UI_H, 0xFF000000);
	ui_text(16, 12, 1, UI_DIM, "Playing video — O stop");
	ui_end();

	while (running) {
		sceCtrlReadBufferPositive(&pad, 1);
		{
			unsigned int pressed = pad.Buttons & ~prev;
			if (pressed & PSP_CTRL_START) {
				running = 0;
				break;
			}
			if (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS)) {
				running = 0;
				break;
			}
			if (pressed & PSP_CTRL_SQUARE)
				paused = !paused;
		}
		prev = pad.Buttons;
		scePowerTick(PSP_POWER_TICK_SUSPEND);

		if (paused) {
			sceDisplayWaitVblankStart();
			continue;
		}

		n = sss_mp4_next_sample(&mp4, g_sample, sizeof g_sample);
		if (n < 0) {
			show_message("Read error", mp4.error);
			break;
		}
		if (n == 0)
			break;

		for (k = 0; k < VIDEO_MAX_IMAGES; k++)
			frame_bufs[k] = uncached(disp[disp_i]);

		pics = avc_decode(&dec, g_sample, n, frame_bufs,
		                  first ? VIDEO_FRAME_FIRST : VIDEO_FRAME_NEXT);
		if (pics < 0) {
			show_message("Decode error", dec.err);
			break;
		}
		if (pics > 0) {
			first = 0;
			sceKernelDcacheWritebackInvalidateRange(
			    disp[disp_i], (unsigned)(VIDEO_STRIDE * VIDEO_HEIGHT * 4));
			sceDisplaySetFrameBuf(disp[disp_i], VIDEO_STRIDE,
			                      PSP_DISPLAY_PIXEL_FORMAT_8888,
			                      PSP_DISPLAY_SETBUF_NEXTFRAME);
			disp_i ^= 1;
			sceDisplayWaitVblankStart();
		}
	}

done:
	avc_close(&dec);
	sss_mp4_close(&mp4);
	if (ddrtop)
		free(ddrtop);
	scePowerUnlock(0);
	scePowerSetClockFrequency(222, 222, 111);
	/* Restore GU UI path. */
	ui_shutdown();
	ui_init();
	return 0;
}
