#ifndef SSS_BROWSER_H
#define SSS_BROWSER_H

#include "sss_path.h"

#define SSS_LIST_MAX 512
#define SSS_DEV_MAX 4

/* is_dir: 0 = mp3 file, 1 = directory, 2 = switch to the other memory. */
typedef struct SssEntry {
	char name[SSS_NAME_MAX];
	int is_dir;
} SssEntry;

typedef struct SssDevice {
	char id[8];
	char label[32];
} SssDevice;

typedef struct SssBrowser {
	SssDevice devices[SSS_DEV_MAX];
	int device_count;
	int device_index;
	char path[SSS_PATH_MAX];
	SssEntry entries[SSS_LIST_MAX];
	int count;
	int cursor;
	int truncated;
	char message[96];
} SssBrowser;

void browser_init(SssBrowser *browser, const char *launched_from);
void browser_reload(SssBrowser *browser);
void browser_move(SssBrowser *browser, int delta);
/* 0 = stayed, 1 = entered a directory or switched memory, 2 = mp3 file. */
int browser_open(SssBrowser *browser);
int browser_up(SssBrowser *browser);
int browser_mp3_list(const SssBrowser *browser, char names[][SSS_NAME_MAX],
                     int max_names, int *start_index);

#endif
