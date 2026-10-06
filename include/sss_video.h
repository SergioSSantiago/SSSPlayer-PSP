#ifndef SSS_VIDEO_H
#define SSS_VIDEO_H

/* Blocking H.264/MP4 playback. Returns 0 on normal exit, <0 on hard failure. */
int video_play(const char *path);

#endif
