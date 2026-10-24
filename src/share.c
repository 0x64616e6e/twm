#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "look.h"
#include "png.h"
#include "rc.h"
#include "rootline.h"
#include "server.h"
#include "setwin.h"
#include "share.h"

#define MAX_IMAGE (128u << 20)

static const char *base(const char *path) {
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

static uint8_t *read_file(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	struct stat st;
	if (fstat(fileno(f), &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > MAX_IMAGE) {
		fclose(f);
		errno = EFBIG;
		return NULL;
	}
	uint8_t *d = malloc((size_t)st.st_size);
	*len = d ? fread(d, 1, (size_t)st.st_size, f) : 0;
	fclose(f);
	return d;
}

bool share_absorb(struct twm_server *server, const char *path, char *reply, size_t len) {
	const char *name = base(path);
	size_t n = 0;
	uint8_t *data = read_file(path, &n);
	if (!data) {
		snprintf(reply, len, "error: %s: %s\n", name, strerror(errno));
		return false;
	}
	char *shared = png_text_get(data, n, "twm:twmrc");
	bool png = png_is_png(data, n);
	free(data);
	if (!shared) {
		/* MRK-2: the mark in the pixels isn't read yet */
		snprintf(reply, len, "error: %s carries no look (%s)\n", name,
			png ? "no twm:twmrc in it" : "not a PNG");
		rootline_post(server, ROOTLINE_INFO, "look", "%s carries no look", name);
		return false;
	}
	struct look look;
	look_extract(shared, &look);
	free(shared);
	if (look.n == 0) {
		snprintf(reply, len, "error: %s has no appearance keys to take (%d left out)\n",
			name, look.refused);
		rootline_post(server, ROOTLINE_INFO, "look", "%s: nothing to take", name);
		return false;
	}
	char *mine = NULL;
	size_t mlen;
	bool missing;
	if (!server->config_path || !rc_read(server->config_path, &mine, &mlen, &missing)) {
		mine = strdup("");
	}
	char *merged = mine ? look_merge(mine, &look) : NULL;
	free(mine);
	if (!merged) {
		/* SHR-3: it must pass the check here, and change only the look */
		snprintf(reply, len, "error: the look from %s doesn't pass twm --check here\n", name);
		rootline_post(server, ROOTLINE_ERROR, "look", "%s doesn't pass the check here", name);
		return false;
	}
	free(merged);
	setwin_propose(server, &look);
	char left[48] = "";
	if (look.refused) {
		snprintf(left, sizeof(left), " \xc2\xb7 %d left out", look.refused);
	}
	rootline_post(server, ROOTLINE_INFO, "look", "from %s \xc2\xb7 %d key%s taken%s \xc2\xb7 Write applies it",
		name, look.n, look.n == 1 ? "" : "s", left);
	snprintf(reply, len, "look from %s: %d key%s taken%s; the diff is in the Settings window, "
		"Write applies it\n", name, look.n, look.n == 1 ? "" : "s", left);
	return true;
}
