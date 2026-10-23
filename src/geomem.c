#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "geomem.h"

static void copy_field(char *dst, size_t len, const char *src) {
	/* Tabs and newlines would break the line format. */
	size_t i = 0;
	for (; src[i] && i + 1 < len; i++) {
		dst[i] = (src[i] == '\t' || src[i] == '\n' || src[i] == '\r') ? ' ' : src[i];
	}
	dst[i] = '\0';
}

void geomem_clear(struct geomem *gm) {
	free(gm->entries);
	gm->entries = NULL;
	gm->n = 0;
}

void geomem_parse(struct geomem *gm, const char *text) {
	geomem_clear(gm);
	gm->entries = calloc(GEOMEM_MAX, sizeof(*gm->entries));
	if (!gm->entries) {
		return;
	}
	const char *line = text;
	while (line && *line && gm->n < GEOMEM_MAX) {
		const char *end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		char buf[512];
		if (len < sizeof(buf)) {
			memcpy(buf, line, len);
			buf[len] = '\0';
			char *tab1 = strchr(buf, '\t');
			char *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
			struct geomem_entry e = {.from_disk = true};
			char tail;
			if (tab1 && tab2 && tab1 != buf && tab2 != tab1 + 1) {
				*tab1 = *tab2 = '\0';
				if (sscanf(tab2 + 1, "%d %d %d %d%c", &e.x, &e.y, &e.w, &e.h, &tail) == 4 &&
						e.w > 0 && e.h > 0 && !geomem_find(gm, buf)) {
					copy_field(e.app_id, sizeof(e.app_id), buf);
					copy_field(e.output, sizeof(e.output), tab1 + 1);
					gm->entries[gm->n++] = e;
				}
			}
		}
		line = end ? end + 1 : NULL;
	}
}

char *geomem_serialize(const struct geomem *gm) {
	size_t cap = gm->n * 220 + 64;
	char *out = malloc(cap);
	if (!out) {
		return NULL;
	}
	size_t o = 0;
	for (size_t i = 0; i < gm->n; i++) {
		const struct geomem_entry *e = &gm->entries[i];
		o += (size_t)snprintf(out + o, cap - o, "%s\t%s\t%d %d %d %d\n",
			e->app_id, e->output, e->x, e->y, e->w, e->h);
	}
	out[o] = '\0';
	return out;
}

const struct geomem_entry *geomem_find(const struct geomem *gm, const char *app_id) {
	for (size_t i = 0; i < gm->n; i++) {
		if (strcmp(gm->entries[i].app_id, app_id) == 0) {
			return &gm->entries[i];
		}
	}
	return NULL;
}

void geomem_set(struct geomem *gm, const char *app_id, const char *output,
		int x, int y, int w, int h) {
	if (!app_id || !*app_id || w <= 0 || h <= 0) {
		return;
	}
	if (!gm->entries) {
		gm->entries = calloc(GEOMEM_MAX, sizeof(*gm->entries));
		if (!gm->entries) {
			return;
		}
	}
	struct geomem_entry e = {.x = x, .y = y, .w = w, .h = h};
	copy_field(e.app_id, sizeof(e.app_id), app_id);
	copy_field(e.output, sizeof(e.output), output ? output : "");
	/* Most recent first; the oldest falls off past GEOMEM_MAX. */
	size_t at = gm->n < GEOMEM_MAX ? gm->n : GEOMEM_MAX - 1;
	for (size_t i = 0; i < gm->n; i++) {
		if (strcmp(gm->entries[i].app_id, e.app_id) == 0) {
			at = i;
			break;
		}
	}
	if (at == gm->n) {
		gm->n++;
	}
	memmove(&gm->entries[1], &gm->entries[0], at * sizeof(*gm->entries));
	gm->entries[0] = e;
}
