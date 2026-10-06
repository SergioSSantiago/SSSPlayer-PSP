/*
 * Minimal MP4 demuxer: H.264 video + AAC audio tracks.
 */

#include "sss_mp4.h"

#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int g_aac_rates[] = { 96000, 88200, 64000, 48000, 44100, 32000,
                                   24000, 22050, 16000, 12000, 11025, 8000, 7350 };

static uint32_t rd32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static int read_fully(int fd, void *dst, int n)
{
	uint8_t *p = dst;
	int got = 0;

	while (got < n) {
		int r = sceIoRead(fd, p + got, (SceSize)(n - got));
		if (r <= 0)
			return -1;
		got += r;
	}
	return 0;
}

static int fail(SssMp4 *mp4, const char *msg)
{
	snprintf(mp4->error, sizeof mp4->error, "%s", msg);
	return -1;
}

static void track_clear(SssMp4Track *t)
{
	free(t->offsets);
	free(t->sizes);
	free(t->dts);
	memset(t, 0, sizeof *t);
}

typedef struct {
	uint32_t *stsc_first;
	uint32_t *stsc_spc;
	int stsc_count;
	uint32_t *stsz;
	uint32_t stsz_default;
	int stsz_count;
	uint32_t *stco;
	int stco_count;
	uint32_t *stts_count;
	uint32_t *stts_delta;
	int stts_entries;
	uint32_t width;
	uint32_t height;
	uint32_t timescale;
	uint32_t duration;
	uint32_t handler;
	uint32_t sample_rate;
	uint32_t channels;
	uint8_t sps[SSS_MP4_SPS_MAX];
	uint16_t sps_len;
	uint8_t pps[SSS_MP4_PPS_MAX];
	uint16_t pps_len;
	uint8_t nal_length_size;
	uint8_t asc[SSS_MP4_ASC_MAX];
	uint16_t asc_len;
	int has_avc;
	int has_aac;
} TrackBuild;

static void build_free(TrackBuild *t)
{
	free(t->stsc_first);
	free(t->stsc_spc);
	free(t->stsz);
	free(t->stco);
	free(t->stts_count);
	free(t->stts_delta);
	memset(t, 0, sizeof *t);
}

static int parse_avcc(TrackBuild *t, const uint8_t *data, int size)
{
	int pos;
	int nals;
	int len;

	if (size < 8)
		return -1;
	t->nal_length_size = (uint8_t)((data[4] & 3) + 1);
	nals = data[5] & 0x1f;
	pos = 6;
	if (nals < 1 || pos + 2 > size)
		return -1;
	len = rd16(data + pos);
	pos += 2;
	if (pos + len > size || len > SSS_MP4_SPS_MAX)
		return -1;
	memcpy(t->sps, data + pos, (size_t)len);
	t->sps_len = (uint16_t)len;
	pos += len;
	if (pos >= size)
		return -1;
	nals = data[pos++];
	if (nals < 1 || pos + 2 > size)
		return -1;
	len = rd16(data + pos);
	pos += 2;
	if (pos + len > size || len > SSS_MP4_PPS_MAX)
		return -1;
	memcpy(t->pps, data + pos, (size_t)len);
	t->pps_len = (uint16_t)len;
	t->has_avc = 1;
	return 0;
}

static uint32_t read_descr_len(const uint8_t *p, int *consumed, int max)
{
	uint32_t len = 0;
	int i;

	*consumed = 0;
	for (i = 0; i < 4 && i < max; i++) {
		uint8_t b = p[i];
		(*consumed)++;
		len = (len << 7) | (b & 0x7f);
		if (!(b & 0x80))
			break;
	}
	return len;
}

static int parse_esds(TrackBuild *t, const uint8_t *data, int size)
{
	int pos = 0;
	uint8_t tag;
	int n;
	uint32_t len;

	if (size < 4)
		return -1;
	pos = 4; /* version + flags */
	if (pos >= size)
		return -1;
	tag = data[pos++];
	if (tag == 0x03) {
		len = read_descr_len(data + pos, &n, size - pos);
		pos += n;
		if (pos + 3 > size)
			return -1;
		pos += 3;
	} else {
		if (pos + 2 > size)
			return -1;
		pos += 2;
	}
	if (pos >= size || data[pos++] != 0x04)
		return -1;
	len = read_descr_len(data + pos, &n, size - pos);
	pos += n;
	if (pos + 13 > size)
		return -1;
	pos += 13; /* objectType + stream type + bitrates */
	if (pos >= size || data[pos++] != 0x05)
		return -1;
	len = read_descr_len(data + pos, &n, size - pos);
	pos += n;
	if (len == 0 || pos + (int)len > size || len > SSS_MP4_ASC_MAX)
		return -1;
	memcpy(t->asc, data + pos, len);
	t->asc_len = (uint16_t)len;
	if (len >= 2) {
		int freq = ((t->asc[0] & 7) << 1) | (t->asc[1] >> 7);
		int ch = (t->asc[1] >> 3) & 0x0f;
		if (freq >= 0 && freq < (int)(sizeof g_aac_rates / sizeof g_aac_rates[0]))
			t->sample_rate = (uint32_t)g_aac_rates[freq];
		if (ch >= 1 && ch <= 7)
			t->channels = (uint32_t)ch;
	}
	t->has_aac = 1;
	return 0;
}

static int build_track_samples(SssMp4 *mp4, TrackBuild *b, SssMp4Track *out)
{
	int sample = 0;
	int chunk;
	int sc = 0;
	int i;
	uint32_t dts = 0;
	int stts_i = 0;
	uint32_t stts_left = 0;

	if (b->stsz_count <= 0 || b->stco_count <= 0 || b->stsc_count <= 0)
		return fail(mp4, "incomplete sample tables");

	out->offsets = calloc((size_t)b->stsz_count, sizeof(uint32_t));
	out->sizes = calloc((size_t)b->stsz_count, sizeof(uint32_t));
	out->dts = calloc((size_t)b->stsz_count, sizeof(uint32_t));
	if (!out->offsets || !out->sizes || !out->dts)
		return fail(mp4, "out of memory");

	if (b->stts_entries > 0) {
		stts_left = b->stts_count[0];
		stts_i = 0;
	}

	for (chunk = 0; chunk < b->stco_count && sample < b->stsz_count; chunk++) {
		uint32_t off = b->stco[chunk];
		int samples_in_chunk;

		while (sc + 1 < b->stsc_count && (int)b->stsc_first[sc + 1] - 1 <= chunk)
			sc++;
		samples_in_chunk = (int)b->stsc_spc[sc];
		for (i = 0; i < samples_in_chunk && sample < b->stsz_count; i++) {
			uint32_t sz = b->stsz_default ? b->stsz_default : b->stsz[sample];
			uint32_t delta = 0;

			out->offsets[sample] = off;
			out->sizes[sample] = sz;
			out->dts[sample] = dts;
			off += sz;
			if (b->stts_entries > 0) {
				if (stts_left == 0 && stts_i + 1 < b->stts_entries) {
					stts_i++;
					stts_left = b->stts_count[stts_i];
				}
				if (stts_left > 0) {
					delta = b->stts_delta[stts_i];
					stts_left--;
				}
			}
			dts += delta;
			sample++;
		}
	}
	out->count = sample;
	out->index = 0;
	out->timescale = b->timescale ? b->timescale : 1;
	return sample > 0 ? 0 : fail(mp4, "no samples");
}

static int parse_stsd(SssMp4 *mp4, TrackBuild *t, int fd, SceOff end)
{
	uint8_t h[8];
	uint8_t entry[8];
	uint32_t entry_size;
	uint32_t entry_type;
	SceOff entry_end;

	if (read_fully(fd, h, 8) < 0 || read_fully(fd, entry, 8) < 0)
		return -1;
	entry_size = rd32(entry);
	entry_type = rd32(entry + 4);
	entry_end = sceIoLseek(fd, 0, PSP_SEEK_CUR) + (SceOff)entry_size - 8;
	if (entry_end > end)
		entry_end = end;

	if (entry_type == 0x61766331u) { /* avc1 */
		uint8_t vis[78];
		if (read_fully(fd, vis, 78) < 0)
			return -1;
		t->width = rd16(vis + 16);
		t->height = rd16(vis + 18);
		while (sceIoLseek(fd, 0, PSP_SEEK_CUR) + 8 <= entry_end) {
			uint8_t sh[8];
			uint32_t ssize;
			uint32_t stype;
			SceOff send;
			int payload;
			uint8_t *buf;

			if (read_fully(fd, sh, 8) < 0)
				return -1;
			ssize = rd32(sh);
			stype = rd32(sh + 4);
			if (ssize < 8)
				break;
			send = sceIoLseek(fd, 0, PSP_SEEK_CUR) + (SceOff)ssize - 8;
			if (stype == 0x61766343u) {
				payload = (int)ssize - 8;
				if (payload <= 0 || payload > 1024)
					return fail(mp4, "avcC payload");
				buf = malloc((size_t)payload);
				if (!buf || read_fully(fd, buf, payload) < 0) {
					free(buf);
					return -1;
				}
				if (parse_avcc(t, buf, payload) < 0) {
					free(buf);
					return fail(mp4, "bad avcC");
				}
				free(buf);
			}
			sceIoLseek(fd, send, PSP_SEEK_SET);
		}
	} else if (entry_type == 0x6D703461u) { /* mp4a */
		uint8_t aud[28];
		if (read_fully(fd, aud, 28) < 0)
			return -1;
		t->channels = rd16(aud + 16);
		t->sample_rate = rd32(aud + 22) >> 16;
		while (sceIoLseek(fd, 0, PSP_SEEK_CUR) + 8 <= entry_end) {
			uint8_t sh[8];
			uint32_t ssize;
			uint32_t stype;
			SceOff send;
			int payload;
			uint8_t *buf;

			if (read_fully(fd, sh, 8) < 0)
				return -1;
			ssize = rd32(sh);
			stype = rd32(sh + 4);
			if (ssize < 8)
				break;
			send = sceIoLseek(fd, 0, PSP_SEEK_CUR) + (SceOff)ssize - 8;
			if (stype == 0x65736473u) { /* esds */
				payload = (int)ssize - 8;
				if (payload <= 0 || payload > 2048)
					return fail(mp4, "esds payload");
				buf = malloc((size_t)payload);
				if (!buf || read_fully(fd, buf, payload) < 0) {
					free(buf);
					return -1;
				}
				if (parse_esds(t, buf, payload) < 0) {
					free(buf);
					return fail(mp4, "bad esds");
				}
				free(buf);
			}
			sceIoLseek(fd, send, PSP_SEEK_SET);
		}
		if (t->asc_len > 0)
			t->has_aac = 1;
	}
	return 0;
}

static int parse_stbl(SssMp4 *mp4, TrackBuild *t, int fd, SceOff end)
{
	while (sceIoLseek(fd, 0, PSP_SEEK_CUR) + 8 <= end) {
		uint8_t hdr[8];
		uint32_t size;
		uint32_t type;
		SceOff box_end;

		if (read_fully(fd, hdr, 8) < 0)
			return -1;
		size = rd32(hdr);
		type = rd32(hdr + 4);
		if (size < 8)
			return fail(mp4, "bad stbl box");
		box_end = sceIoLseek(fd, 0, PSP_SEEK_CUR) + (SceOff)size - 8;
		if (box_end > end)
			return fail(mp4, "stbl overrun");

		if (type == 0x7374737Au) { /* stsz */
			uint8_t h[12];
			int i;
			if (read_fully(fd, h, 12) < 0)
				return -1;
			t->stsz_default = rd32(h + 4);
			t->stsz_count = (int)rd32(h + 8);
			if (t->stsz_count < 0 || t->stsz_count > 500000)
				return fail(mp4, "stsz too large");
			if (!t->stsz_default) {
				t->stsz = malloc((size_t)t->stsz_count * 4);
				if (!t->stsz)
					return fail(mp4, "stsz alloc");
				for (i = 0; i < t->stsz_count; i++) {
					uint8_t b[4];
					if (read_fully(fd, b, 4) < 0)
						return -1;
					t->stsz[i] = rd32(b);
				}
			}
		} else if (type == 0x73747363u) { /* stsc */
			uint8_t h[8];
			int i;
			if (read_fully(fd, h, 8) < 0)
				return -1;
			t->stsc_count = (int)rd32(h + 4);
			if (t->stsc_count <= 0 || t->stsc_count > 100000)
				return fail(mp4, "stsc bad");
			t->stsc_first = malloc((size_t)t->stsc_count * 4);
			t->stsc_spc = malloc((size_t)t->stsc_count * 4);
			if (!t->stsc_first || !t->stsc_spc)
				return fail(mp4, "stsc alloc");
			for (i = 0; i < t->stsc_count; i++) {
				uint8_t e[12];
				if (read_fully(fd, e, 12) < 0)
					return -1;
				t->stsc_first[i] = rd32(e);
				t->stsc_spc[i] = rd32(e + 4);
			}
		} else if (type == 0x7374636Fu || type == 0x636F3634u) {
			uint8_t h[8];
			int i;
			int is64 = (type == 0x636F3634u);
			if (read_fully(fd, h, 8) < 0)
				return -1;
			t->stco_count = (int)rd32(h + 4);
			if (t->stco_count <= 0 || t->stco_count > 500000)
				return fail(mp4, "stco bad");
			t->stco = malloc((size_t)t->stco_count * 4);
			if (!t->stco)
				return fail(mp4, "stco alloc");
			for (i = 0; i < t->stco_count; i++) {
				if (is64) {
					uint8_t e[8];
					if (read_fully(fd, e, 8) < 0)
						return -1;
					t->stco[i] = rd32(e + 4);
				} else {
					uint8_t e[4];
					if (read_fully(fd, e, 4) < 0)
						return -1;
					t->stco[i] = rd32(e);
				}
			}
		} else if (type == 0x73747473u) { /* stts */
			uint8_t h[8];
			int i;
			if (read_fully(fd, h, 8) < 0)
				return -1;
			t->stts_entries = (int)rd32(h + 4);
			if (t->stts_entries <= 0 || t->stts_entries > 100000)
				return fail(mp4, "stts bad");
			t->stts_count = malloc((size_t)t->stts_entries * 4);
			t->stts_delta = malloc((size_t)t->stts_entries * 4);
			if (!t->stts_count || !t->stts_delta)
				return fail(mp4, "stts alloc");
			for (i = 0; i < t->stts_entries; i++) {
				uint8_t e[8];
				if (read_fully(fd, e, 8) < 0)
					return -1;
				t->stts_count[i] = rd32(e);
				t->stts_delta[i] = rd32(e + 4);
			}
		} else if (type == 0x73747364u) {
			if (parse_stsd(mp4, t, fd, box_end) < 0)
				return -1;
		}

		sceIoLseek(fd, box_end, PSP_SEEK_SET);
	}
	return 0;
}

/* trak-local parse: only fill one TrackBuild */
static int parse_trak_body(SssMp4 *mp4, int fd, SceOff end, TrackBuild *track)
{
	while (sceIoLseek(fd, 0, PSP_SEEK_CUR) + 8 <= end) {
		uint8_t hdr[8];
		uint32_t size;
		uint32_t type;
		SceOff box_end;
		int header = 8;

		if (read_fully(fd, hdr, 8) < 0)
			return -1;
		size = rd32(hdr);
		type = rd32(hdr + 4);
		if (size == 1) {
			uint8_t ext[8];
			if (read_fully(fd, ext, 8) < 0)
				return -1;
			size = rd32(ext + 4);
			header = 16;
		}
		if (size < (uint32_t)header)
			return fail(mp4, "bad box size");
		box_end = sceIoLseek(fd, 0, PSP_SEEK_CUR) + (SceOff)size - header;
		if (box_end > end)
			box_end = end;

		if (type == 0x6D646961u || type == 0x6D696E66u) {
			if (parse_trak_body(mp4, fd, box_end, track) < 0)
				return -1;
		} else if (type == 0x7374626Cu) {
			if (parse_stbl(mp4, track, fd, box_end) < 0)
				return -1;
		} else if (type == 0x68646C72u) {
			uint8_t h[20];
			if (read_fully(fd, h, 20) < 0)
				return -1;
			track->handler = rd32(h + 8);
		} else if (type == 0x6D646864u) {
			uint8_t ver;
			if (read_fully(fd, &ver, 1) < 0)
				return -1;
			if (ver == 0) {
				uint8_t b[19];
				if (read_fully(fd, b, 19) < 0)
					return -1;
				track->timescale = rd32(b + 11);
				track->duration = rd32(b + 15);
			} else {
				uint8_t b[31];
				if (read_fully(fd, b, 31) < 0)
					return -1;
				track->timescale = rd32(b + 19);
				track->duration = rd32(b + 23);
			}
		}
		sceIoLseek(fd, box_end, PSP_SEEK_SET);
	}
	return 0;
}

static int parse_container(SssMp4 *mp4, int fd, SceOff end, TrackBuild *video,
                           TrackBuild *audio)
{
	while (sceIoLseek(fd, 0, PSP_SEEK_CUR) + 8 <= end) {
		uint8_t hdr[8];
		uint32_t size;
		uint32_t type;
		SceOff box_end;
		int header = 8;

		if (read_fully(fd, hdr, 8) < 0)
			return -1;
		size = rd32(hdr);
		type = rd32(hdr + 4);
		if (size == 1) {
			uint8_t ext[8];
			if (read_fully(fd, ext, 8) < 0)
				return -1;
			size = rd32(ext + 4);
			header = 16;
		}
		if (size < (uint32_t)header)
			return fail(mp4, "bad box size");
		box_end = sceIoLseek(fd, 0, PSP_SEEK_CUR) + (SceOff)size - header;
		if (box_end > end)
			box_end = end;

		if (type == 0x6D6F6F76u) {
			if (parse_container(mp4, fd, box_end, video, audio) < 0)
				return -1;
		} else if (type == 0x7472616Bu) {
			TrackBuild local;
			memset(&local, 0, sizeof local);
			if (parse_trak_body(mp4, fd, box_end, &local) < 0) {
				build_free(&local);
				return -1;
			}
			if (local.handler == 0x76696465u && local.has_avc) {
				build_free(video);
				*video = local;
			} else if (local.handler == 0x736F756Eu && local.has_aac) {
				build_free(audio);
				*audio = local;
			} else {
				build_free(&local);
			}
		}
		sceIoLseek(fd, box_end, PSP_SEEK_SET);
	}
	return 0;
}

static int read_sample(SssMp4 *mp4, SssMp4Track *tr, void *dst, int dst_max,
                       uint32_t *dts)
{
	uint32_t off;
	uint32_t size;

	if (!tr || tr->index >= tr->count)
		return 0;
	off = tr->offsets[tr->index];
	size = tr->sizes[tr->index];
	if (dts)
		*dts = tr->dts[tr->index];
	if ((int)size > dst_max)
		return fail(mp4, "Sample too large");
	if (sceIoLseek(mp4->fd, (SceOff)off, PSP_SEEK_SET) < 0)
		return fail(mp4, "Seek sample failed");
	if (read_fully(mp4->fd, dst, (int)size) < 0)
		return fail(mp4, "Read sample failed");
	tr->index++;
	return (int)size;
}

int sss_mp4_open(SssMp4 *mp4, const char *path)
{
	SceOff file_end;
	TrackBuild video;
	TrackBuild audio;

	memset(mp4, 0, sizeof *mp4);
	memset(&video, 0, sizeof video);
	memset(&audio, 0, sizeof audio);
	mp4->fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
	if (mp4->fd < 0)
		return fail(mp4, "Could not open MP4");

	file_end = sceIoLseek(mp4->fd, 0, PSP_SEEK_END);
	sceIoLseek(mp4->fd, 0, PSP_SEEK_SET);
	if (parse_container(mp4, mp4->fd, file_end, &video, &audio) < 0) {
		build_free(&video);
		build_free(&audio);
		sss_mp4_close(mp4);
		return -1;
	}
	if (!video.has_avc) {
		build_free(&video);
		build_free(&audio);
		sss_mp4_close(mp4);
		return fail(mp4, "No H.264 video track");
	}
	mp4->width = video.width;
	mp4->height = video.height;
	mp4->sps_len = video.sps_len;
	mp4->pps_len = video.pps_len;
	mp4->nal_length_size = video.nal_length_size;
	memcpy(mp4->sps, video.sps, video.sps_len);
	memcpy(mp4->pps, video.pps, video.pps_len);
	if (build_track_samples(mp4, &video, &mp4->video) < 0) {
		build_free(&video);
		build_free(&audio);
		sss_mp4_close(mp4);
		return -1;
	}
	build_free(&video);

	if (audio.has_aac && audio.sample_rate > 0) {
		if (build_track_samples(mp4, &audio, &mp4->audio) == 0) {
			mp4->has_audio = 1;
			mp4->audio_rate = audio.sample_rate;
			mp4->audio_channels = audio.channels ? audio.channels : 2;
			mp4->asc_len = audio.asc_len;
			memcpy(mp4->asc, audio.asc, audio.asc_len);
		} else {
			track_clear(&mp4->audio);
			mp4->error[0] = '\0';
		}
	}
	build_free(&audio);
	mp4->error[0] = '\0';
	return 0;
}

void sss_mp4_close(SssMp4 *mp4)
{
	if (!mp4)
		return;
	if (mp4->fd >= 0) {
		sceIoClose(mp4->fd);
		mp4->fd = -1;
	}
	track_clear(&mp4->video);
	track_clear(&mp4->audio);
	mp4->has_audio = 0;
}

void sss_mp4_rewind(SssMp4 *mp4)
{
	if (!mp4)
		return;
	mp4->video.index = 0;
	mp4->audio.index = 0;
}

int sss_mp4_next_video(SssMp4 *mp4, void *dst, int dst_max, uint32_t *dts)
{
	return read_sample(mp4, &mp4->video, dst, dst_max, dts);
}

int sss_mp4_next_audio(SssMp4 *mp4, void *dst, int dst_max, uint32_t *dts)
{
	if (!mp4 || !mp4->has_audio)
		return 0;
	return read_sample(mp4, &mp4->audio, dst, dst_max, dts);
}
