#include "sss_browser.h"

#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int dir_exists(const char *path)
{
	SceUID fd;

	fd = sceIoDopen(path);
	if (fd < 0)
		return 0;
	sceIoDclose(fd);
	return 1;
}

static int entry_rank(int kind)
{
	/* Directories first, then MP3, then other files (video, etc.). */
	if (kind == 1 || kind == 3)
		return 0;
	if (kind == 0)
		return 1;
	return 2;
}

static int cmp_entry(const void *va, const void *vb)
{
	const SssEntry *a = va;
	const SssEntry *b = vb;
	int rank;

	rank = entry_rank(a->is_dir) - entry_rank(b->is_dir);
	if (rank != 0)
		return rank;
	return strcasecmp(a->name, b->name);
}

static int name_ok(const char *name)
{
	if (!name || name[0] == '\0')
		return 0;
	if (name[0] == '.')
		return 0;
	if (strchr(name, '/') || strchr(name, ':'))
		return 0;
	return 1;
}

static void add_device(SssBrowser *browser, const char *id, const char *label)
{
	char root[16];
	SssDevice *dev;

	if (browser->device_count >= SSS_DEV_MAX)
		return;
	snprintf(root, sizeof root, "%s/", id);
	if (!dir_exists(root))
		return;

	dev = &browser->devices[browser->device_count];
	snprintf(dev->id, sizeof dev->id, "%s", id);
	snprintf(dev->label, sizeof dev->label, "%s", label);
	browser->device_count++;
}

int browser_at_roots(const SssBrowser *browser)
{
	return browser && browser->path[0] == '\0';
}

void browser_reload(SssBrowser *browser)
{
	SceUID fd;
	SceIoDirent ent;
	int i;

	browser->count = 0;
	browser->truncated = 0;
	browser->message[0] = '\0';

	if (browser_at_roots(browser)) {
		for (i = 0; i < browser->device_count && browser->count < SSS_LIST_MAX; i++) {
			snprintf(browser->entries[browser->count].name, SSS_NAME_MAX, "%s",
			         browser->devices[i].label);
			browser->entries[browser->count].is_dir = 3;
			browser->count++;
		}
		if (browser->count == 0)
			snprintf(browser->message, sizeof browser->message,
			         "No ms0: or ef0: storage");
		if (browser->cursor < 0)
			browser->cursor = 0;
		if (browser->count == 0)
			browser->cursor = 0;
		else if (browser->cursor >= browser->count)
			browser->cursor = browser->count - 1;
		return;
	}

	fd = sceIoDopen(browser->path);
	if (fd < 0) {
		snprintf(browser->message, sizeof browser->message,
		         "Cannot open folder");
		return;
	}

	memset(&ent, 0, sizeof ent);
	while (sceIoDread(fd, &ent) > 0) {
		int is_dir;

		ent.d_name[sizeof ent.d_name - 1] = '\0';
		if (!name_ok(ent.d_name)) {
			memset(&ent, 0, sizeof ent);
			continue;
		}

		is_dir = FIO_S_ISDIR(ent.d_stat.st_mode) || FIO_SO_ISDIR(ent.d_stat.st_attr);
		if (browser->count >= SSS_LIST_MAX) {
			browser->truncated = 1;
			break;
		}

		snprintf(browser->entries[browser->count].name, SSS_NAME_MAX, "%s",
		         ent.d_name);
		if (is_dir)
			browser->entries[browser->count].is_dir = 1;
		else if (sss_path_is_mp3(ent.d_name))
			browser->entries[browser->count].is_dir = 0;
		else
			browser->entries[browser->count].is_dir = 2;
		browser->count++;
		memset(&ent, 0, sizeof ent);
	}
	sceIoDclose(fd);

	if (browser->count > 1)
		qsort(browser->entries, (size_t)browser->count, sizeof(SssEntry),
		      cmp_entry);

	if (browser->cursor < 0)
		browser->cursor = 0;
	if (browser->count == 0)
		browser->cursor = 0;
	else if (browser->cursor >= browser->count)
		browser->cursor = browser->count - 1;

	if (browser->count == 0)
		snprintf(browser->message, sizeof browser->message, "Empty folder");
	else if (browser->truncated)
		snprintf(browser->message, sizeof browser->message,
		         "List capped at %d entries", SSS_LIST_MAX);
}

void browser_init(SssBrowser *browser, const char *launched_from)
{
	char launch_dev[8];
	int i;

	memset(browser, 0, sizeof *browser);
	add_device(browser, "ef0:", "Internal (PSP Go)");
	add_device(browser, "ms0:", "Memory Stick");

	launch_dev[0] = '\0';
	if (launched_from && launched_from[0]) {
		const char *slash = strchr(launched_from, '/');
		size_t n = slash ? (size_t)(slash - launched_from) : strlen(launched_from);
		if (n >= sizeof launch_dev)
			n = sizeof launch_dev - 1;
		memcpy(launch_dev, launched_from, n);
		launch_dev[n] = '\0';
	}

	browser->device_index = 0;
	for (i = 0; i < browser->device_count; i++) {
		if (strcmp(browser->devices[i].id, launch_dev) == 0) {
			browser->device_index = i;
			break;
		}
	}

	browser->path[0] = '\0';
	browser_reload(browser);
	if (browser->device_count == 0) {
		snprintf(browser->message, sizeof browser->message,
		         "No ms0: or ef0: storage");
		return;
	}
	browser->cursor = browser->device_index;
	if (browser->cursor < 0 || browser->cursor >= browser->count)
		browser->cursor = 0;
}

void browser_move(SssBrowser *browser, int delta)
{
	int next;

	if (browser->count <= 0 || delta == 0)
		return;
	next = browser->cursor + delta;
	if (next < 0)
		next = 0;
	if (next >= browser->count)
		next = browser->count - 1;
	browser->cursor = next;
}

int browser_open(SssBrowser *browser)
{
	const SssEntry *entry;
	char next[SSS_PATH_MAX];

	if (browser->count <= 0 || browser->cursor < 0 || browser->cursor >= browser->count)
		return 0;
	entry = &browser->entries[browser->cursor];

	if (entry->is_dir == 3) {
		if (browser->cursor < 0 || browser->cursor >= browser->device_count)
			return 0;
		browser->device_index = browser->cursor;
		snprintf(browser->path, sizeof browser->path, "%s/",
		         browser->devices[browser->device_index].id);
		browser->cursor = 0;
		browser_reload(browser);
		return 1;
	}

	if (entry->is_dir == 1) {
		sss_path_join(next, sizeof next, browser->path, entry->name);
		snprintf(browser->path, sizeof browser->path, "%s", next);
		browser->cursor = 0;
		browser_reload(browser);
		return 1;
	}

	if (entry->is_dir == 0 && sss_path_is_mp3(entry->name)) {
		browser->message[0] = '\0';
		return 2;
	}

	if (entry->is_dir == 2) {
		snprintf(browser->message, sizeof browser->message,
		         "Only MP3 audio for now");
		return 0;
	}
	return 0;
}

int browser_up(SssBrowser *browser)
{
	char previous[SSS_NAME_MAX];
	int i;

	if (browser->device_count <= 0 || browser_at_roots(browser))
		return 0;

	if (sss_path_is_root(browser->path)) {
		int device = browser->device_index;
		browser->path[0] = '\0';
		browser_reload(browser);
		browser->cursor = device;
		if (browser->cursor < 0 || browser->cursor >= browser->count)
			browser->cursor = 0;
		return 1;
	}

	sss_path_basename(browser->path, previous, sizeof previous);
	if (!sss_path_parent(browser->path))
		return 0;

	browser_reload(browser);
	browser->cursor = 0;
	if (previous[0] != '\0') {
		for (i = 0; i < browser->count; i++) {
			if (strcmp(browser->entries[i].name, previous) == 0) {
				browser->cursor = i;
				break;
			}
		}
	}
	return 1;
}

int browser_mp3_list(const SssBrowser *browser, char names[][SSS_NAME_MAX],
                     int max_names, int *start_index)
{
	int i;
	int count = 0;

	if (start_index)
		*start_index = -1;
	if (!browser || !names || max_names <= 0)
		return 0;

	for (i = 0; i < browser->count; i++) {
		if (browser->entries[i].is_dir != 0)
			continue;
		if (!sss_path_is_mp3(browser->entries[i].name))
			continue;
		if (count >= max_names) {
			if (start_index && i == browser->cursor)
				*start_index = -1;
			break;
		}
		if (start_index && i == browser->cursor)
			*start_index = count;
		snprintf(names[count], SSS_NAME_MAX, "%s", browser->entries[i].name);
		count++;
	}
	return count;
}
