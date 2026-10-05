#ifndef SSS_PLAYER_H
#define SSS_PLAYER_H

#include "sss_path.h"

#define SSS_PL_MAX 400

typedef enum SssPlayState {
	SSS_PLAY_STOPPED = 0,
	SSS_PLAY_PLAYING = 1,
	SSS_PLAY_PAUSED = 2
} SssPlayState;

typedef struct SssPlayStatus {
	SssPlayState state;
	int position_sec;
	int duration_sec;
	int bitrate_kbps;
	int sample_rate;
	int channels;
	char name[SSS_NAME_MAX];
	char error[96];
} SssPlayStatus;

int player_init(void);
void player_shutdown(void);
int player_play_list(const char *dir, char names[][SSS_NAME_MAX], int count,
                      int index);
void player_toggle_pause(void);
void player_stop(void);
void player_seek(int delta_sec);
void player_next(void);
void player_prev(void);
void player_get_status(SssPlayStatus *out);

#endif
