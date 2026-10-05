#include "sss_path.h"

#include <stdio.h>
#include <string.h>

int sss_path_is_root(const char *path)
{
	const char *colon;

	if (!path)
		return 0;
	colon = strchr(path, ':');
	if (!colon)
		return 0;
	colon++;
	if (*colon == '/')
		colon++;
	return *colon == '\0';
}

int sss_path_parent(char *path)
{
	size_t len;

	if (!path || sss_path_is_root(path))
		return 0;

	len = strlen(path);
	while (len > 0 && path[len - 1] == '/')
		len--;
	while (len > 0 && path[len - 1] != '/')
		len--;
	if (len == 0)
		return 0;

	/* len sits just past the slash that separates the parent. */
	path[len - 1] = '\0';
	len = strlen(path);
	if (len > 0 && path[len - 1] == ':') {
		if (len + 1 >= SSS_PATH_MAX)
			return 0;
		path[len] = '/';
		path[len + 1] = '\0';
	}
	return 1;
}

void sss_path_join(char *dst, size_t dst_n, const char *dir, const char *name)
{
	size_t len;
	int need_slash;

	if (!dst || dst_n == 0)
		return;
	if (!dir)
		dir = "";
	if (!name)
		name = "";

	len = strlen(dir);
	need_slash = !(len > 0 && dir[len - 1] == '/');
	if (need_slash)
		snprintf(dst, dst_n, "%s/%s", dir, name);
	else
		snprintf(dst, dst_n, "%s%s", dir, name);
}

int sss_path_is_mp3(const char *name)
{
	size_t n;
	const char *ext;

	if (!name)
		return 0;
	n = strlen(name);
	if (n < 4)
		return 0;
	ext = name + n - 4;
	if (ext[0] != '.')
		return 0;
	if (ext[1] != 'm' && ext[1] != 'M')
		return 0;
	if (ext[2] != 'p' && ext[2] != 'P')
		return 0;
	return ext[3] == '3';
}

void sss_path_basename(const char *path, char *dst, size_t dst_n)
{
	const char *base;

	if (!dst || dst_n == 0)
		return;
	if (!path)
		path = "";
	base = strrchr(path, '/');
	base = base ? base + 1 : path;
	snprintf(dst, dst_n, "%s", base);
}

void sss_path_tail(char *dst, size_t dst_n, const char *src, int cols)
{
	int len;

	if (!dst || dst_n == 0)
		return;
	if (!src)
		src = "";
	if (cols < 4)
		cols = 4;
	if ((size_t)cols >= dst_n)
		cols = (int)dst_n - 1;

	len = (int)strlen(src);
	if (len <= cols) {
		snprintf(dst, dst_n, "%s", src);
		return;
	}
	snprintf(dst, dst_n, "...%s", src + (len - (cols - 3)));
}
