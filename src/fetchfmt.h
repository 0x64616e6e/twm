#ifndef TWM_FETCHFMT_H
#define TWM_FETCHFMT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "sysinfo.h"

/*
 * twmfetch's output (FET-1 … FET-4), pure: the facts in, text out. B9's
 * logo beside a column of facts, or JSON, or one line.
 */

#define FETCH_MAX_OUTPUTS 8

enum fetch_color {
	FC_GROUND, FC_SURFACE, FC_GRAPHITE, FC_HAIRLINE, FC_HAIRLINE_STRONG, FC_INK,
	FC_INK_2, FC_MUTED, FC_DIM, FC_ACCENT, FC_ON_ACCENT, FC_OK, FC_ALERT, FC_COUNT,
};

struct fetch_facts {
	char user[64], host[128], os[160];
	long uptime; /* seconds, -1 unknown */

	bool wm; /* twm answered its socket */
	char version[32];
	bool xwayland;
	int open, iconified, groups;
	int desktops, desktop, quiet;
	int noutputs;
	struct {
		char name[32];
		int width, height;
		double scale;
	} outputs[FETCH_MAX_OUTPUTS];
	char focus[16];
	int focus_delay;
	char font[96];
	int font_size;
	char twmrc_path[256];
	int twmrc_lines;      /* -1: no file */
	char twmrc_state[16]; /* "ok", "errors", "missing" */
	long long applied;    /* epoch seconds */
	time_t now;

	bool have_mem;
	struct sys_mem mem;
	bool have_pool;
	struct sys_pool pool;

	uint32_t colors[FC_COUNT]; /* 0xRRGGBB */
	char shot[512]; /* the fetch shot's path, if one was taken */
};

/* Colors from the built-in theme, everything else unknown. */
void fetch_init(struct fetch_facts *f);
/* Take twm's `info` answer (FET-3). */
void fetch_parse_info(const char *reply, struct fetch_facts *f);

/* The full fetch: `color` for 24-bit escapes, `logo` for B9's logo. */
void fetch_render(const struct fetch_facts *f, bool color, bool logo, char *out, size_t len);
void fetch_json(const struct fetch_facts *f, char *out, size_t len);
void fetch_short(const struct fetch_facts *f, char *out, size_t len);

#endif
