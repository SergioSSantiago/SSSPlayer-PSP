/*
 * Minimal MP4 demuxer for one H.264 (avc1) video track.
 * Enough to feed the PSP Media Engine NAL path.
 */

#include "sss_mp4.h"

#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int parse_avcc(SssMp4 *mp4, const uint8_t *data, int size)
{
	int pos;
	int nals;
	int len;

	if (size < 8)
		return fail(mp4, "avcC too small");
	mp4->nal_length_size = (uint8_t)((data[4] & 3) + 1);
	nals = data[5] & 0x1f;
	pos = 6;
	if (nals < 1)
		return fail(mp4, "avcC missing SPS");
	if (pos + 2 > size)
		return fail(mp4, "avcC SPS header");
	len = rd16(data + pos);
	pos += 2;
	if (pos + len > size || len > SSS_MP4_SPS_MAX)
		return fail(mp4, "avcC SPS size");
	memcpy(mp4->sps, data + pos, (size_t)len);
	mp4->sps_len = (uint16_t)len;
	pos += len;
	if (pos >= size)
		return fail(mp4, "avcC missing PPS");
	nals = data[pos++];
	if (nals < 1)
		return fail(mp4, "avcC missing PPS count");
	if (pos + 2 > size)
		return fail(mp4, "avcC PPS header");
	len = rd16(data + pos);
	pos += 2;
	if (pos + len > size || len > SSS_MP4_PPS_MAX)
		return fail(mp4, "avcC PPS size");
	memcpy(mp4->pps, data + pos, (size_t)len);
	mp4->pps_len = (uint16_t)len;
	return 0;
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
	uint32_t width;
	uint32_t height;
	uint32_t timescale;
	uint32_t duration;
	uint32_t handler;
	int has_avc;
} TrackBuild;

static void track_free(TrackBuild *t)
{
	free(t->stsc_first);
	free(t->stsc_spc);
	free(t->stsz);
	free(t->stco);
	memset(t, 0, sizeof *t);
}

static int build_samples(SssMp4 *mp4, TrackBuild *t)
{
	int sample = 0;
	int chunk;
	int sc = 0;
	int i;

	if (t->stsz_count <= 0 || t->stco_count <= 0 || t->stsc_count <= 0)
		return fail(mp4, "incomplete sample tables");

	mp4->sample_offsets = calloc((size_t)t->stsz_count, sizeof(uint32_t));
	mp4->sample_sizes = calloc((size_t)t->stsz_count, sizeof(uint32_t));
	if (!mp4->sample_offsets || !mp4->sample_sizes)
		return fail(mp4, "out of memory");

	for (chunk = 0; chunk < t->stco_count && sample < t->stsz_count; chunk++) {
		uint32_t off = t->stco[chunk];
		int samples_in_chunk;

		while (sc + 1 < t->stsc_count && (int)t->stsc_first[sc + 1] - 1 <= chunk)
			sc++;
		samples_in_chunk = (int)t->stsc_spc[sc];
		for (i = 0; i < samples_in_chunk && sample < t->stsz_count; i++) {
			uint32_t sz = t->stsz_default ? t->stsz_default : t->stsz[sample];
			mp4->sample_offsets[sample] = off;
			mp4->sample_sizes[sample] = sz;
			off += sz;
			sample++;
		}
	}
	mp4->sample_count = sample;
	mp4->width = t->width;
	mp4->height = t->height;
	mp4->timescale = t->timescale ? t->timescale : 1;
	mp4->duration = t->duration;
	return sample > 0 ? 0 : fail(mp4, "no video samples");
}

static int parse_stsd(SssMp4 *mp4, TrackBuild *t, int fd, SceOff end)
{
	uint8_t h[8];
	uint8_t entry[8];
	uint32_t entry_size;
	uint32_t entry_type;
	SceOff entry_end;

	if (read_fully(fd, h, 8) < 0)
		return -1;
	if (read_fully(fd, entry, 8) < 0)
		return -1;
	entry_size = rd32(entry);
	entry_type = rd32(entry + 4);
	entry_end = sceIoLseek(fd, 0, PSP_SEEK_CUR) + (SceOff)entry_size - 8;
	if (entry_end > end)
		entry_end = end;
	if (entry_type != 0x61766331u) /* avc1 */
		return 0;
	{
		uint8_t vis[78];
		if (read_fully(fd, vis, 78) < 0)
			return -1;
		t->width = rd16(vis + 16);
		t->height = rd16(vis + 18);
	}
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
		if (stype == 0x61766343u) { /* avcC */
			payload = (int)ssize - 8;
			if (payload <= 0 || payload > 1024)
				return fail(mp4, "avcC payload");
			buf = malloc((size_t)payload);
			if (!buf)
				return fail(mp4, "avcC alloc");
			if (read_fully(fd, buf, payload) < 0) {
				free(buf);
				return -1;
			}
			if (parse_avcc(mp4, buf, payload) < 0) {
				free(buf);
				return -1;
			}
			t->has_avc = 1;
			free(buf);
		}
		sceIoLseek(fd, send, PSP_SEEK_SET);
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
		} else if (type == 0x7374636Fu || type == 0x636F3634u) { /* stco/co64 */
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
		} else if (type == 0x73747364u) { /* stsd */
			if (parse_stsd(mp4, t, fd, box_end) < 0)
				return -1;
		}

		sceIoLseek(fd, box_end, PSP_SEEK_SET);
	}
	return 0;
}

static int parse_container(SssMp4 *mp4, int fd, SceOff end, TrackBuild *track);

static int parse_trak(SssMp4 *mp4, int fd, SceOff end, TrackBuild *out)
{
	TrackBuild local;

	memset(&local, 0, sizeof local);
	if (parse_container(mp4, fd, end, &local) < 0) {
		track_free(&local);
		return -1;
	}
	if (local.handler == 0x76696465u && local.has_avc) {
		track_free(out);
		*out = local;
	} else {
		track_free(&local);
	}
	return 0;
}

static int parse_container(SssMp4 *mp4, int fd, SceOff end, TrackBuild *track)
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

		if (type == 0x6D6F6F76u || type == 0x6D646961u || type == 0x6D696E66u) {
			if (parse_container(mp4, fd, box_end, track) < 0)
				return -1;
		} else if (type == 0x7472616Bu) {
			if (parse_trak(mp4, fd, box_end, track) < 0)
				return -1;
		} else if (type == 0x7374626Cu) {
			if (parse_stbl(mp4, track, fd, box_end) < 0)
				return -1;
		} else if (type == 0x68646C72u) { /* hdlr */
			uint8_t h[20];
			if (read_fully(fd, h, 20) < 0)
				return -1;
			track->handler = rd32(h + 8);
		} else if (type == 0x6D646864u) { /* mdhd */
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

int sss_mp4_open(SssMp4 *mp4, const char *path)
{
	SceOff file_end;
	TrackBuild video;

	memset(mp4, 0, sizeof *mp4);
	memset(&video, 0, sizeof video);
	mp4->fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
	if (mp4->fd < 0)
		return fail(mp4, "Could not open MP4");

	file_end = sceIoLseek(mp4->fd, 0, PSP_SEEK_END);
	sceIoLseek(mp4->fd, 0, PSP_SEEK_SET);
	if (parse_container(mp4, mp4->fd, file_end, &video) < 0) {
		track_free(&video);
		sss_mp4_close(mp4);
		return -1;
	}
	if (!video.has_avc) {
		track_free(&video);
		sss_mp4_close(mp4);
		return fail(mp4, "No H.264 video track");
	}
	if (build_samples(mp4, &video) < 0) {
		track_free(&video);
		sss_mp4_close(mp4);
		return -1;
	}
	track_free(&video);
	mp4->sample_index = 0;
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
	free(mp4->sample_offsets);
	free(mp4->sample_sizes);
	mp4->sample_offsets = NULL;
	mp4->sample_sizes = NULL;
	mp4->sample_count = 0;
}

void sss_mp4_rewind(SssMp4 *mp4)
{
	if (mp4)
		mp4->sample_index = 0;
}

int sss_mp4_next_sample(SssMp4 *mp4, void *dst, int dst_max)
{
	uint32_t off;
	uint32_t size;

	if (!mp4 || mp4->sample_index >= mp4->sample_count)
		return 0;
	off = mp4->sample_offsets[mp4->sample_index];
	size = mp4->sample_sizes[mp4->sample_index];
	if ((int)size > dst_max)
		return fail(mp4, "Sample too large");
	if (sceIoLseek(mp4->fd, (SceOff)off, PSP_SEEK_SET) < 0)
		return fail(mp4, "Seek sample failed");
	if (read_fully(mp4->fd, dst, (int)size) < 0)
		return fail(mp4, "Read sample failed");
	mp4->sample_index++;
	return (int)size;
}
