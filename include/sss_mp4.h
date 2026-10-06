#ifndef SSS_MP4_H
#define SSS_MP4_H

#include <stddef.h>
#include <stdint.h>

#define SSS_MP4_SPS_MAX 256
#define SSS_MP4_PPS_MAX 256

typedef struct SssMp4 {
	int fd;
	uint32_t width;
	uint32_t height;
	uint32_t timescale;
	uint32_t duration;
	uint8_t sps[SSS_MP4_SPS_MAX];
	uint16_t sps_len;
	uint8_t pps[SSS_MP4_PPS_MAX];
	uint16_t pps_len;
	uint8_t nal_length_size;
	uint32_t *sample_offsets;
	uint32_t *sample_sizes;
	int sample_count;
	int sample_index;
	char error[96];
} SssMp4;

int sss_mp4_open(SssMp4 *mp4, const char *path);
void sss_mp4_close(SssMp4 *mp4);
/* Read next video sample into dst. Returns bytes, 0 at EOF, <0 on error. */
int sss_mp4_next_sample(SssMp4 *mp4, void *dst, int dst_max);
void sss_mp4_rewind(SssMp4 *mp4);

#endif
