#include "sss_player.h"

#include <pspkernel.h>
#include <pspaudio.h>
#include <pspmp3.h>
#include <pspiofilemgr.h>
#include <psputility.h>
#include <psputility_modules.h>
#include <stdio.h>
#include <string.h>

#define MP3_EOF 0x80671402u

static unsigned char mp3_buf[16 * 1024] __attribute__((aligned(64)));
static unsigned char pcm_buf[16 * (1152 / 2)] __attribute__((aligned(64)));
static short out_buf[1152 * 2] __attribute__((aligned(64)));

enum {
	CMD_NONE = 0,
	CMD_PLAY = 1,
	CMD_STOP = 2,
	CMD_PAUSE = 3,
	CMD_NEXT = 4,
	CMD_PREV = 5,
	CMD_SEEK = 6
};

static SceUID g_lock = -1;
static SceUID g_thid = -1;
static volatile int g_quit;
static int g_ready;

static int g_cmd;
static int g_seek_delta;
static char g_req_dir[SSS_PATH_MAX];
static char g_req_names[SSS_PL_MAX][SSS_NAME_MAX];
static int g_req_count;
static int g_req_index;

static char g_pl_dir[SSS_PATH_MAX];
static char g_pl_names[SSS_PL_MAX][SSS_NAME_MAX];
static int g_pl_count;
static int g_pl_index;

static SceUID g_fd = -1;
static int g_handle = -1;
static int g_src_ready;
static int g_src_samples;
static int g_paused;
static int g_rate;
static int g_channels;
static int g_bitrate;
static int g_spf;
static int g_duration;
static int g_played_samples;

static SssPlayStatus g_status;

static void lock(void)
{
	if (g_lock >= 0)
		sceKernelWaitSema(g_lock, 1, NULL);
}

static void unlock(void)
{
	if (g_lock >= 0)
		sceKernelSignalSema(g_lock, 1);
}

static void publish(int state, const char *name, const char *error, int pos,
                    int dur)
{
	lock();
	g_status.state = (SssPlayState)state;
	g_status.position_sec = pos;
	g_status.duration_sec = dur;
	g_status.bitrate_kbps = g_bitrate;
	g_status.sample_rate = g_rate;
	g_status.channels = g_channels;
	if (name)
		snprintf(g_status.name, sizeof g_status.name, "%s", name);
	if (error)
		snprintf(g_status.error, sizeof g_status.error, "%s", error);
	else
		g_status.error[0] = '\0';
	unlock();
}

static int is_eof(int code)
{
	return code == 0 || (unsigned)code == MP3_EOF;
}

static int fill_stream(void)
{
	unsigned char *dst = NULL;
	SceInt32 to_write = 0;
	SceInt32 src_pos = 0;
	int status;
	int got;

	status = sceMp3GetInfoToAddStreamData(g_handle, &dst, &to_write, &src_pos);
	if (status < 0)
		return status;
	if (to_write <= 0 || !dst)
		return 0;

	if (sceIoLseek(g_fd, (SceOff)src_pos, PSP_SEEK_SET) < 0)
		return -1;

	got = sceIoRead(g_fd, dst, (SceSize)to_write);
	if (got < 0)
		return got;
	if (got == 0)
		return 0;

	status = sceMp3NotifyAddStreamData(g_handle, got);
	if (status < 0)
		return status;
	return got;
}

static void release_handle(void)
{
	if (g_handle >= 0) {
		sceMp3ReleaseMp3Handle(g_handle);
		g_handle = -1;
	}
}

static int setup_handle(SceOff start, SceOff end)
{
	SceMp3InitArg arg;
	int status;

	memset(&arg, 0, sizeof arg);
	arg.mp3StreamStart = start;
	arg.mp3StreamEnd = end;
	arg.mp3Buf = mp3_buf;
	arg.mp3BufSize = (SceInt32)sizeof mp3_buf;
	arg.pcmBuf = pcm_buf;
	arg.pcmBufSize = (SceInt32)sizeof pcm_buf;

	g_handle = sceMp3ReserveMp3Handle(&arg);
	if (g_handle < 0)
		return g_handle;

	if (fill_stream() <= 0) {
		release_handle();
		return -1;
	}

	status = sceMp3Init(g_handle);
	if (status < 0) {
		release_handle();
		return status;
	}
	sceMp3SetLoopNum(g_handle, 0);
	return 0;
}

static void stream_bounds(SceOff size, SceOff *start, SceOff *end)
{
	unsigned char hdr[10];
	unsigned char tag[3];

	*start = 0;
	*end = size;

	if (sceIoLseek(g_fd, 0, PSP_SEEK_SET) < 0)
		return;
	if (sceIoRead(g_fd, hdr, sizeof hdr) == (int)sizeof hdr &&
	    hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
		int tag_size = ((hdr[6] & 0x7f) << 21) | ((hdr[7] & 0x7f) << 14) |
		               ((hdr[8] & 0x7f) << 7) | (hdr[9] & 0x7f);
		*start = 10 + tag_size;
		if (hdr[5] & 0x10)
			*start += 10;
	}

	if (size > 128) {
		if (sceIoLseek(g_fd, size - 128, PSP_SEEK_SET) >= 0 &&
		    sceIoRead(g_fd, tag, sizeof tag) == (int)sizeof tag &&
		    tag[0] == 'T' && tag[1] == 'A' && tag[2] == 'G')
			*end = size - 128;
	}

	if (*start < 0 || *start >= size)
		*start = 0;
	if (*end > size || *end < *start)
		*end = size;
}

static int samples_per_frame(void)
{
	int version = sceMp3GetMPEGVersion(g_handle);

	/* MPEG header version bits: 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5. */
	if (version == 3)
		return 1152;
	if (version == 2 || version == 0)
		return 576;
	return 1152;
}

static void audio_close(void)
{
	release_handle();
	if (g_fd >= 0) {
		sceIoClose(g_fd);
		g_fd = -1;
	}
	if (g_src_ready) {
		sceAudioSRCChRelease();
		g_src_ready = 0;
	}
	g_paused = 0;
	g_src_samples = 0;
}

static void refill_after_seek(void)
{
	int guard = 0;

	while (guard < 8 && sceMp3CheckStreamDataNeeded(g_handle) > 0) {
		if (fill_stream() <= 0)
			break;
		guard++;
	}
}

static int open_current(void)
{
	char full[SSS_PATH_MAX];
	SceOff size;
	SceOff start;
	SceOff end;
	int status;
	int frames;
	const char *name;

	if (g_pl_index < 0 || g_pl_index >= g_pl_count)
		return -1;

	audio_close();
	name = g_pl_names[g_pl_index];
	sss_path_join(full, sizeof full, g_pl_dir, name);

	g_fd = sceIoOpen(full, PSP_O_RDONLY, 0777);
	if (g_fd < 0) {
		publish(SSS_PLAY_STOPPED, name, "Could not open", 0, 0);
		return -1;
	}

	size = sceIoLseek(g_fd, 0, PSP_SEEK_END);
	if (size <= 0) {
		audio_close();
		publish(SSS_PLAY_STOPPED, name, "Empty file", 0, 0);
		return -1;
	}

	stream_bounds(size, &start, &end);
	status = setup_handle(start, end);
	if (status < 0 && start != 0) {
		sceIoLseek(g_fd, 0, PSP_SEEK_SET);
		status = setup_handle(0, size);
	}
	if (status < 0) {
		char err[96];
		audio_close();
		snprintf(err, sizeof err, "Invalid MP3 %08X", (unsigned)status);
		publish(SSS_PLAY_STOPPED, name, err, 0, 0);
		return -1;
	}

	g_rate = sceMp3GetSamplingRate(g_handle);
	g_channels = sceMp3GetMp3ChannelNum(g_handle);
	g_bitrate = sceMp3GetBitRate(g_handle);
	g_spf = samples_per_frame();
	if (g_channels != 1 && g_channels != 2)
		g_channels = 2;
	if (g_rate < 0)
		g_rate = 0;
	if (g_bitrate < 0)
		g_bitrate = 0;

	frames = sceMp3GetFrameNum(g_handle);
	if (frames > 0 && g_rate > 0 && g_spf > 0)
		g_duration = (int)(((long long)frames * g_spf) / g_rate);
	else
		g_duration = 0;

	g_played_samples = 0;
	g_paused = 0;
	publish(SSS_PLAY_PLAYING, name, NULL, 0, g_duration);
	return 0;
}

static void output_pcm(const short *pcm, int samples)
{
	int i;
	int copy;
	int rc;

	if (!g_src_ready) {
		int reserve = samples;
		if (reserve < 17)
			reserve = 17;
		if (reserve > 1152)
			reserve = 1152;
		rc = sceAudioSRCChReserve(reserve, g_rate, 2);
		if (rc < 0) {
			char err[96];
			char name[SSS_NAME_MAX];
			snprintf(name, sizeof name, "%s", g_status.name);
			snprintf(err, sizeof err, "Audio %08X", (unsigned)rc);
			audio_close();
			publish(SSS_PLAY_STOPPED, name, err, 0, g_duration);
			return;
		}
		g_src_ready = 1;
		g_src_samples = reserve;
	}

	copy = samples;
	if (copy > g_src_samples)
		copy = g_src_samples;

	if (g_channels == 1) {
		for (i = 0; i < copy; i++) {
			out_buf[i * 2] = pcm[i];
			out_buf[i * 2 + 1] = pcm[i];
		}
	} else {
		for (i = 0; i < copy; i++) {
			out_buf[i * 2] = pcm[i * 2];
			out_buf[i * 2 + 1] = pcm[i * 2 + 1];
		}
	}
	for (i = copy; i < g_src_samples; i++) {
		out_buf[i * 2] = 0;
		out_buf[i * 2 + 1] = 0;
	}

	sceKernelDcacheWritebackRange(out_buf,
	                              (unsigned)(g_src_samples * 2 * (int)sizeof(short)));
	sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, out_buf);
}

static void output_silence(void)
{
	if (!g_src_ready || g_src_samples <= 0) {
		sceKernelDelayThread(20 * 1000);
		return;
	}
	memset(out_buf, 0, sizeof out_buf);
	sceKernelDcacheWritebackRange(out_buf,
	                              (unsigned)(g_src_samples * 2 * (int)sizeof(short)));
	sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, out_buf);
}

static int decode_frame(void)
{
	short *pcm = NULL;
	int bytes;
	int samples;

	if (sceMp3CheckStreamDataNeeded(g_handle) > 0)
		fill_stream();

	bytes = sceMp3Decode(g_handle, &pcm);
	if (is_eof(bytes)) {
		if (fill_stream() > 0)
			bytes = sceMp3Decode(g_handle, &pcm);
	}
	if (is_eof(bytes))
		return 0;
	if (bytes < 0)
		return bytes;
	if (!pcm)
		return 0;

	samples = bytes / (2 * g_channels);
	if (samples <= 0)
		return 0;

	output_pcm(pcm, samples);
	if (g_handle < 0)
		return -1;
	g_played_samples += samples;
	if (g_rate > 0)
		publish(g_paused ? SSS_PLAY_PAUSED : SSS_PLAY_PLAYING, NULL, NULL,
		        g_played_samples / g_rate, g_duration);
	return 1;
}

static void cmd_seek(int delta)
{
	int pos;
	int target;
	int frame;
	int frames;
	int status;

	if (g_handle < 0 || g_rate <= 0 || g_spf <= 0 || delta == 0)
		return;

	pos = g_played_samples / g_rate;
	target = pos + delta;
	if (target < 0)
		target = 0;

	frames = sceMp3GetFrameNum(g_handle);
	frame = (int)(((long long)target * g_rate) / g_spf);
	if (frame < 0)
		frame = 0;
	if (frames > 0 && frame >= frames)
		frame = frames - 1;

	status = sceMp3ResetPlayPositionByFrame(g_handle, (SceUInt32)frame);
	if (status < 0)
		return;
	g_played_samples = (int)((long long)frame * g_spf);
	refill_after_seek();
	publish(g_paused ? SSS_PLAY_PAUSED : SSS_PLAY_PLAYING, NULL, NULL,
	        g_rate > 0 ? g_played_samples / g_rate : 0, g_duration);
}

static void cmd_restart(void)
{
	if (g_handle < 0)
		return;
	if (sceMp3ResetPlayPosition(g_handle) < 0)
		return;
	g_played_samples = 0;
	refill_after_seek();
	publish(g_paused ? SSS_PLAY_PAUSED : SSS_PLAY_PLAYING, NULL, NULL, 0,
	        g_duration);
}

static void cmd_next(void)
{
	if (g_pl_index + 1 >= g_pl_count) {
		char name[SSS_NAME_MAX];
		snprintf(name, sizeof name, "%s", g_pl_index >= 0 ? g_pl_names[g_pl_index] : "");
		audio_close();
		publish(SSS_PLAY_STOPPED, name, "End of playlist", g_duration, g_duration);
		return;
	}
	g_pl_index++;
	open_current();
}

static void cmd_prev(void)
{
	int pos = (g_rate > 0) ? g_played_samples / g_rate : 0;

	if (g_handle >= 0 && (pos > 3 || g_pl_index <= 0)) {
		cmd_restart();
		return;
	}
	if (g_pl_index <= 0)
		return;
	g_pl_index--;
	open_current();
}

static int take_command(int *seek)
{
	int cmd;

	lock();
	cmd = g_cmd;
	*seek = g_seek_delta;
	g_cmd = 0;
	g_seek_delta = 0;
	if (cmd == CMD_PLAY) {
		memcpy(g_pl_dir, g_req_dir, sizeof g_pl_dir);
		memcpy(g_pl_names, g_req_names, sizeof g_pl_names);
		g_pl_count = g_req_count;
		g_pl_index = g_req_index;
	}
	unlock();
	return cmd;
}

static int audio_thread(SceSize args, void *argp)
{
	(void)args;
	(void)argp;

	while (!g_quit) {
		int seek = 0;
		int cmd = take_command(&seek);
		int decoded;

		if (cmd == CMD_PLAY)
			open_current();
		else if (cmd == CMD_STOP) {
			char name[SSS_NAME_MAX];
			snprintf(name, sizeof name, "%s", g_status.name);
			audio_close();
			publish(SSS_PLAY_STOPPED, name, NULL, 0, 0);
		} else if (cmd == CMD_PAUSE && g_handle >= 0) {
			g_paused = !g_paused;
			publish(g_paused ? SSS_PLAY_PAUSED : SSS_PLAY_PLAYING, NULL, NULL,
			        g_rate > 0 ? g_played_samples / g_rate : 0, g_duration);
		} else if (cmd == CMD_NEXT)
			cmd_next();
		else if (cmd == CMD_PREV)
			cmd_prev();
		else if (cmd == CMD_SEEK)
			cmd_seek(seek);

		if (g_quit)
			break;
		if (g_handle < 0) {
			sceKernelDelayThread(20 * 1000);
			continue;
		}
		if (g_paused) {
			output_silence();
			continue;
		}

		decoded = decode_frame();
		if (decoded == 0)
			cmd_next();
		else if (decoded < 0 && g_handle >= 0) {
			char err[96];
			char name[SSS_NAME_MAX];
			snprintf(name, sizeof name, "%s", g_pl_names[g_pl_index]);
			snprintf(err, sizeof err, "Decode %08X", (unsigned)decoded);
			audio_close();
			publish(SSS_PLAY_STOPPED, name, err, 0, 0);
		}
	}

	audio_close();
	sceKernelExitThread(0);
	return 0;
}

static int load_module(int id)
{
	int status = sceUtilityLoadModule(id);
	if (status == (int)SCE_ERROR_MODULE_ALREADY_LOADED)
		return 0;
	return status;
}

static void set_init_error(const char *text)
{
	lock();
	snprintf(g_status.error, sizeof g_status.error, "%s", text);
	g_status.state = SSS_PLAY_STOPPED;
	unlock();
}

int player_init(void)
{
	int status;
	char err[96];

	memset(&g_status, 0, sizeof g_status);
	g_lock = sceKernelCreateSema("sss_pl", 0, 1, 1, NULL);
	if (g_lock < 0)
		return g_lock;

	status = load_module(PSP_MODULE_AV_AVCODEC);
	if (status < 0) {
		snprintf(err, sizeof err, "Codec AV %08X", (unsigned)status);
		set_init_error(err);
		return status;
	}
	status = load_module(PSP_MODULE_AV_MP3);
	if (status < 0) {
		snprintf(err, sizeof err, "Codec MP3 %08X", (unsigned)status);
		set_init_error(err);
		return status;
	}
	status = sceMp3InitResource();
	if (status < 0) {
		snprintf(err, sizeof err, "MP3 init %08X", (unsigned)status);
		set_init_error(err);
		return status;
	}

	g_thid = sceKernelCreateThread("sss_audio", audio_thread, 0x12, 0x10000,
	                               THREAD_ATTR_USER, NULL);
	if (g_thid < 0) {
		sceMp3TermResource();
		set_init_error("No audio thread");
		return g_thid;
	}
	status = sceKernelStartThread(g_thid, 0, NULL);
	if (status < 0) {
		sceKernelDeleteThread(g_thid);
		g_thid = -1;
		sceMp3TermResource();
		set_init_error("Audio did not start");
		return status;
	}

	g_ready = 1;
	return 0;
}

void player_shutdown(void)
{
	g_quit = 1;
	if (g_thid >= 0) {
		sceKernelWaitThreadEnd(g_thid, NULL);
		sceKernelDeleteThread(g_thid);
		g_thid = -1;
	}
	if (g_ready || g_handle >= 0)
		sceMp3TermResource();
	g_ready = 0;
	if (g_lock >= 0) {
		sceKernelDeleteSema(g_lock);
		g_lock = -1;
	}
}

int player_play_list(const char *dir, char names[][SSS_NAME_MAX], int count,
                      int index)
{
	if (!g_ready || !dir || !names || count <= 0 || index < 0 || index >= count)
		return -1;
	if (count > SSS_PL_MAX)
		count = SSS_PL_MAX;
	if (index >= count)
		return -1;

	lock();
	snprintf(g_req_dir, sizeof g_req_dir, "%s", dir);
	memcpy(g_req_names, names, (size_t)count * SSS_NAME_MAX);
	g_req_count = count;
	g_req_index = index;
	g_cmd = CMD_PLAY;
	g_seek_delta = 0;
	unlock();
	return 0;
}

void player_toggle_pause(void)
{
	lock();
	if (g_cmd == CMD_NONE || g_cmd == CMD_PAUSE)
		g_cmd = CMD_PAUSE;
	unlock();
}

void player_stop(void)
{
	lock();
	g_cmd = CMD_STOP;
	g_seek_delta = 0;
	unlock();
}

void player_seek(int delta_sec)
{
	if (delta_sec == 0)
		return;
	lock();
	if (g_cmd == CMD_NONE || g_cmd == CMD_SEEK) {
		g_cmd = CMD_SEEK;
		g_seek_delta += delta_sec;
	}
	unlock();
}

void player_next(void)
{
	lock();
	if (g_cmd == CMD_NONE || g_cmd == CMD_NEXT)
		g_cmd = CMD_NEXT;
	unlock();
}

void player_prev(void)
{
	lock();
	if (g_cmd == CMD_NONE || g_cmd == CMD_PREV)
		g_cmd = CMD_PREV;
	unlock();
}

void player_get_status(SssPlayStatus *out)
{
	if (!out)
		return;
	lock();
	*out = g_status;
	unlock();
}
