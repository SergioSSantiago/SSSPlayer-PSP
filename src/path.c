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

static int match_ext(const char *name, const char *ext)
{
	size_t n;
	size_t el;
	size_t i;

	if (!name || !ext)
		return 0;
	n = strlen(name);
	el = strlen(ext);
	if (n < el + 1)
		return 0;
	if (name[n - el - 1] != '.')
		return 0;
	for (i = 0; i < el; i++) {
		char a = name[n - el + i];
		char b = ext[i];
		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b)
			return 0;
	}
	return 1;
}

int sss_path_is_mp3(const char *name)
{
	return match_ext(name, "mp3");
}

int sss_path_is_video(const char *name)
{
	return match_ext(name, "mp4") || match_ext(name, "m4v") ||
	       match_ext(name, "avi") || match_ext(name, "mpg") ||
	       match_ext(name, "mpeg") || match_ext(name, "pmf") ||
	       match_ext(name, "pmp");
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
