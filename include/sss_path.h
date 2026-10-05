#ifndef SSS_PATH_H
#define SSS_PATH_H

#include <stddef.h>

#define SSS_PATH_MAX 512
#define SSS_NAME_MAX 256

int sss_path_is_root(const char *path);
int sss_path_parent(char *path);
void sss_path_join(char *dst, size_t dst_n, const char *dir, const char *name);
int sss_path_is_mp3(const char *name);
void sss_path_basename(const char *path, char *dst, size_t dst_n);
void sss_path_tail(char *dst, size_t dst_n, const char *src, int cols);

#endif
