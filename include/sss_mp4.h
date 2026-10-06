#ifndef SSS_MP4_H
#define SSS_MP4_H

#include <stddef.h>
#include <stdint.h>

#define SSS_MP4_SPS_MAX 256
#define SSS_MP4_PPS_MAX 256
#define SSS_MP4_ASC_MAX 64

typedef struct SssMp4Track {
	uint32_t *offsets;
	uint32_t *sizes;
	uint32_t *dts; /* cumulative decode time in track timescale */
	int count;
	int index;
	uint32_t timescale;
} SssMp4Track;

typedef struct SssMp4 {
	int fd;
	/* Video (H.264) */
	uint32_t width;
	uint32_t height;
	uint8_t sps[SSS_MP4_SPS_MAX];
	uint16_t sps_len;
	uint8_t pps[SSS_MP4_PPS_MAX];
	uint16_t pps_len;
	uint8_t nal_length_size;
	SssMp4Track video;
	/* Audio (AAC) */
	int has_audio;
	uint32_t audio_rate;
	uint32_t audio_channels;
	uint8_t asc[SSS_MP4_ASC_MAX];
	uint16_t asc_len;
	SssMp4Track audio;
	char error[96];
} SssMp4;

int sss_mp4_open(SssMp4 *mp4, const char *path);
void sss_mp4_close(SssMp4 *mp4);
/* Next sample. Returns bytes, 0 at EOF, <0 on error. Optional dts out. */
int sss_mp4_next_video(SssMp4 *mp4, void *dst, int dst_max, uint32_t *dts);
int sss_mp4_next_audio(SssMp4 *mp4, void *dst, int dst_max, uint32_t *dts);
void sss_mp4_rewind(SssMp4 *mp4);

#endif
