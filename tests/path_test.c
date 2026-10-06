#include "sss_path.h"

#include <stdio.h>
#include <string.h>

static int g_failed;

static void expect(int cond, const char *msg)
{
	if (!cond) {
		fprintf(stderr, "FAIL: %s\n", msg);
		g_failed = 1;
	}
}

static void expect_str(const char *got, const char *want, const char *msg)
{
	if (strcmp(got, want) != 0) {
		fprintf(stderr, "FAIL: %s (got \"%s\", want \"%s\")\n", msg, got, want);
		g_failed = 1;
	}
}

int main(void)
{
	char path[SSS_PATH_MAX];
	char joined[SSS_PATH_MAX];
	char base[SSS_NAME_MAX];
	char tail[64];

	expect(sss_path_is_root("ms0:/"), "ms0:/ is root");
	expect(sss_path_is_root("ef0:"), "ef0: is root");
	expect(sss_path_is_root("ef0:/"), "ef0:/ is root");
	expect(!sss_path_is_root("ef0:/MUSIC"), "music is not root");
	expect(!sss_path_is_root("ms0:/MUSIC/"), "music slash is not root");

	snprintf(path, sizeof path, "ef0:/MUSIC/Rock");
	expect(sss_path_parent(path), "parent of nested");
	expect_str(path, "ef0:/MUSIC", "nested parent");
	expect(sss_path_parent(path), "parent of music");
	expect_str(path, "ef0:/", "device root parent");
	expect(!sss_path_parent(path), "root has no parent");

	snprintf(path, sizeof path, "ms0:/");
	expect(!sss_path_parent(path), "ms0 root stays");
	expect_str(path, "ms0:/", "ms0 root unchanged");

	sss_path_join(joined, sizeof joined, "ef0:/MUSIC", "a.mp3");
	expect_str(joined, "ef0:/MUSIC/a.mp3", "join file");
	sss_path_join(joined, sizeof joined, "ms0:/", "MUSIC");
	expect_str(joined, "ms0:/MUSIC", "join onto root");
	sss_path_join(joined, sizeof joined, "ef0:", "MUSIC");
	expect_str(joined, "ef0:/MUSIC", "join onto device");

	expect(sss_path_is_mp3("tema.mp3"), "lower mp3");
	expect(sss_path_is_mp3("TEMA.MP3"), "upper mp3");
	expect(sss_path_is_mp3("a.Mp3"), "mixed mp3");
	expect(!sss_path_is_mp3("tema.ogg"), "ogg");
	expect(!sss_path_is_mp3("mp3"), "no dot");
	expect(!sss_path_is_mp3("tema.mp3.bak"), "bak");
	expect(sss_path_is_video("clip.mp4"), "mp4");
	expect(sss_path_is_video("CLIP.MP4"), "MP4 upper");
	expect(sss_path_is_video("a.mpeg"), "mpeg");
	expect(!sss_path_is_video("tema.mp3"), "mp3 not video");

	sss_path_basename("ef0:/MUSIC/a.mp3", base, sizeof base);
	expect_str(base, "a.mp3", "basename file");
	sss_path_basename("ef0:/", base, sizeof base);
	expect_str(base, "", "basename root");

	sss_path_tail(tail, sizeof tail, "ef0:/MUSIC/un-nombre-largo.mp3", 22);
	expect_str(tail, "...un-nombre-largo.mp3", "tail clips the front");
	sss_path_tail(tail, sizeof tail, "ms0:/", 16);
	expect_str(tail, "ms0:/", "short tail unchanged");

	if (g_failed)
		return 1;
	printf("path tests ok\n");
	return 0;
}
