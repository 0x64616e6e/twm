#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "actions.h"
#include "desktop.h"
#include "draw.h"
#include "group.h"
#include "icons.h"
#include "imgr.h"
#include "internal.h"
#include "pixbuf.h"
#include "rootline.h"
#include "settings.h"
#include "text.h"
#include "view.h"

/*
 * The Icon Manager (IMG-1 … IMG-10): desktops, windows, iconified windows,
 * and, when you type at it, a filter over windows then commands. B0 sizes at
 * rest; the filter's sizes are B5's scaled by 26/30.
 */

#define IM_W 234          /* content width; the frame adds 1 px each side */
#define DESK_PAD 10
#define DESK_GAP 6
#define DESK_H 28
#define LIST_PAD 6
#define ROW_H 30
#define ROW_PAD 12
#define ROW_GAP 10
#define MARK 8
#define TEXT 12
#define DIV_MARGIN 6
#define FILTER_PAD 10
#define INPUT_H 33
#define INPUT_TEXT 13
#define HEAD_TEXT 10
#define HEAD_H 22
#define RES_H 35
#define META_TEXT 11
#define FOOT_TEXT 11
#define FOOT_PAD 10
#define BAR 3
#define MAX_WINDOWS 6
#define MAX_RUN 4
#define EXEC_RESCAN_S 60

enum im_hit_kind { IM_HIT_DESKTOP, IM_HIT_WINDOW, IM_HIT_RESULT };

struct im_hit {
	int y0, y1, x0, x1;
	enum im_hit_kind kind;
	int index;
	uint64_t view_id;
};

enum result_kind { RES_WINDOW, RES_MENU, RES_EXEC };

struct result {
	enum result_kind kind;
	uint64_t view_id;          /* RES_WINDOW */
	const struct twm_func *func; /* RES_MENU: points into the config */
	char text[128];
	char meta[48];
	size_t match;              /* byte offset of the match in text */
};

struct imgr {
	struct twm_server *server;
	struct twm_internal_view *iv;
	char filter[128];
	struct result results[MAX_WINDOWS + MAX_RUN];
	size_t nresults, nwindows, sel;
	struct im_hit hits[128];
	size_t nhits;
	char **execs;
	size_t nexecs;
	time_t execs_at;
	uint32_t config_gen; /* results hold pointers into the config */
};

static struct imgr *imgr_of(struct twm_internal_view *iv) {
	return iv->data;
}

static struct twm_view *view_by_id(struct twm_server *server, uint64_t id) {
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (v->id == id) {
			return v;
		}
	}
	return NULL;
}

static const char *ci_strstr(const char *hay, const char *needle) {
	size_t n = strlen(needle);
	for (const char *p = hay; *p; p++) {
		if (strncasecmp(p, needle, n) == 0) {
			return p;
		}
	}
	return n == 0 ? hay : NULL;
}

/* --- $PATH executables for RUN (IMG-7) --- */

static int cmp_str(const void *a, const void *b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static void free_execs(struct imgr *im) {
	for (size_t i = 0; i < im->nexecs; i++) {
		free(im->execs[i]);
	}
	free(im->execs);
	im->execs = NULL;
	im->nexecs = 0;
}

static void scan_execs(struct imgr *im) {
	time_t now = time(NULL);
	if (im->execs && now - im->execs_at < EXEC_RESCAN_S) {
		return;
	}
	free_execs(im);
	im->execs_at = now;
	const char *path = getenv("PATH");
	if (!path) {
		return;
	}
	char *copy = strdup(path);
	size_t cap = 0;
	for (char *dir = strtok(copy, ":"); dir; dir = strtok(NULL, ":")) {
		DIR *d = opendir(dir);
		if (!d) {
			continue;
		}
		struct dirent *e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.') {
				continue;
			}
			char full[4096];
			snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
			struct stat st;
			if (stat(full, &st) != 0 || !S_ISREG(st.st_mode) || access(full, X_OK) != 0) {
				continue;
			}
			if (im->nexecs == cap) {
				cap = cap ? cap * 2 : 256;
				im->execs = realloc(im->execs, cap * sizeof(*im->execs));
			}
			im->execs[im->nexecs++] = strdup(e->d_name);
		}
		closedir(d);
	}
	free(copy);
	qsort(im->execs, im->nexecs, sizeof(*im->execs), cmp_str);
	/* Drop duplicates from several PATH directories. */
	size_t k = 0;
	for (size_t i = 0; i < im->nexecs; i++) {
		if (k && strcmp(im->execs[k - 1], im->execs[i]) == 0) {
			free(im->execs[i]);
		} else {
			im->execs[k++] = im->execs[i];
		}
	}
	im->nexecs = k;
}

/* --- results (IMG-7) --- */

static int window_rank(const char *title, const char *f) {
	const char *m = ci_strstr(title, f);
	return !m ? -1 : (m == title ? 0 : 1); /* prefix matches first */
}

static void compute_results(struct imgr *im) {
	struct twm_server *server = im->server;
	im->nresults = im->nwindows = 0;
	im->config_gen = server->theme_gen;
	const char *f = im->filter;
	/* WINDOWS: every desktop, iconified ones too; prefix first, then recent. */
	for (int pass = 0; pass < 2; pass++) {
		struct twm_view *best[64];
		size_t nb = 0;
		struct twm_view *v;
		wl_list_for_each(v, &server->views, link) {
			if (v->type == TWM_VIEW_INTERNAL || nb == 64) {
				continue;
			}
			if (window_rank(view_title(v), f) == pass) {
				best[nb++] = v;
			}
		}
		for (size_t i = 0; i < nb; i++) { /* most recently focused first */
			for (size_t j = i + 1; j < nb; j++) {
				if (best[j]->focus_seq > best[i]->focus_seq) {
					struct twm_view *t = best[i];
					best[i] = best[j];
					best[j] = t;
				}
			}
		}
		for (size_t i = 0; i < nb && im->nwindows < MAX_WINDOWS; i++) {
			struct result *r = &im->results[im->nresults++];
			*r = (struct result){.kind = RES_WINDOW, .view_id = best[i]->id};
			snprintf(r->text, sizeof(r->text), "%s", view_title(best[i]));
			r->match = (size_t)(ci_strstr(r->text, f) - r->text);
			if (best[i]->iconified) {
				snprintf(r->meta, sizeof(r->meta), "iconified");
			} else {
				snprintf(r->meta, sizeof(r->meta), "desktop %d", best[i]->desktop);
			}
			im->nwindows++;
		}
	}
	/* RUN: menu entries, then executables on $PATH. */
	size_t nrun = 0;
	const struct twm_config *cfg = server->config;
	for (size_t m = 0; m < cfg->nmenus && nrun < MAX_RUN; m++) {
		for (size_t i = 0; i < cfg->menus[m].nitems && nrun < MAX_RUN; i++) {
			const struct twm_menu_item *it = &cfg->menus[m].items[i];
			if (it->func.kind == TWM_F_SEPARATOR || it->func.kind == TWM_F_MENU ||
					!*it->label || !ci_strstr(it->label, f)) {
				continue;
			}
			bool dup = false;
			for (size_t k = im->nwindows; k < im->nresults; k++) {
				dup |= strcmp(im->results[k].text, it->label) == 0;
			}
			if (dup) {
				continue;
			}
			struct result *r = &im->results[im->nresults++];
			*r = (struct result){.kind = RES_MENU, .func = &it->func};
			snprintf(r->text, sizeof(r->text), "%s", it->label);
			r->match = (size_t)(ci_strstr(r->text, f) - r->text);
			snprintf(r->meta, sizeof(r->meta), "menu %s", cfg->menus[m].name);
			nrun++;
		}
	}
	scan_execs(im);
	char first[64];
	sscanf(f, "%63s", first);
	for (size_t i = 0; i < im->nexecs && nrun < MAX_RUN; i++) {
		if (strncmp(im->execs[i], first, strlen(first)) != 0) {
			continue;
		}
		struct result *r = &im->results[im->nresults++];
		*r = (struct result){.kind = RES_EXEC};
		snprintf(r->text, sizeof(r->text), "%s \xe2\x80\xa6", im->execs[i]); /* B5 "zpool …" */
		r->match = 0;
		char term[32];
		sscanf(cfg->terminal, "%31s", term);
		snprintf(r->meta, sizeof(r->meta), "in %s", term);
		nrun++;
	}
	if (im->sel >= im->nresults) {
		im->sel = im->nresults ? im->nresults - 1 : 0;
	}
}

/* --- drawing --- */

#define S(v) ((int)lroundf((float)(v) * scale))

static uint32_t argb(uint32_t rgb) {
	return rgba_argb32(rgba_hex(rgb, 1));
}

static void add_hit(struct imgr *im, int x0, int y0, int x1, int y1,
		enum im_hit_kind kind, int index, uint64_t view_id) {
	if (im->nhits < sizeof(im->hits) / sizeof(im->hits[0])) {
		im->hits[im->nhits++] = (struct im_hit){y0, y1, x0, x1, kind, index, view_id};
	}
}

/* Text with its matched part in accent semibold (IMG-7). */
static void draw_matched(pixman_image_t *img, struct twm_font *reg, struct twm_font *bold,
		const char *text, size_t match, size_t mlen, int x, int base, uint32_t color,
		uint32_t accent, int max_w) {
	char pre[128], mid[128], post[128];
	size_t tl = strlen(text);
	if (match > tl) {
		match = tl;
	}
	if (match + mlen > tl) {
		mlen = tl - match;
	}
	snprintf(pre, sizeof(pre), "%.*s", (int)match, text);
	snprintf(mid, sizeof(mid), "%.*s", (int)mlen, text + match);
	snprintf(post, sizeof(post), "%s", text + match + mlen);
	int used = text_draw(img, reg, pre, x, base, color, 0, max_w);
	if (used >= max_w && max_w > 0) {
		return;
	}
	int w = text_draw(img, bold, mid, x + used, base, accent, 0, max_w > 0 ? max_w - used : 0);
	used += w;
	if (max_w <= 0 || used < max_w) {
		text_draw(img, reg, post, x + used, base, color, 0, max_w > 0 ? max_w - used : 0);
	}
}

/* Window-state marker (IMG-3, IMG-4): filled, outlined, dashed, or a bar. */
static void draw_marker(pixman_image_t *img, struct twm_view *v, bool focused, int x, int y,
		float scale, const struct twm_theme *t) {
	int stroke = draw_stroke(scale);
	if (v && v->iconified) {
		draw_fill(img, argb(v->urgent ? t->accent : t->dim), S(x), S(y + MARK / 2 - 1),
			S(MARK), S(2));
	} else if (focused) {
		draw_fill(img, argb(t->accent), S(x), S(y), S(MARK), S(MARK));
	} else if (v && (v->urgent || v->placing)) {
		uint32_t c = argb(v->urgent ? t->accent : t->muted);
		for (int i = 0; i < MARK; i += 2) { /* dashed outline */
			draw_fill(img, c, S(x + i), S(y), stroke, stroke);
			draw_fill(img, c, S(x + i), S(y + MARK) - stroke, stroke, stroke);
			draw_fill(img, c, S(x), S(y + i), stroke, stroke);
			draw_fill(img, c, S(x + MARK) - stroke, S(y + i), stroke, stroke);
		}
	} else {
		draw_box(img, argb(t->muted), S(x), S(y), S(MARK), S(MARK), stroke);
	}
}

static int cmp_name(const void *a, const void *b) {
	struct twm_view *x = *(struct twm_view *const *)a, *y = *(struct twm_view *const *)b;
	return strcasecmp(view_title(x), view_title(y));
}

static int cmp_age(const void *a, const void *b) {
	struct twm_view *x = *(struct twm_view *const *)a, *y = *(struct twm_view *const *)b;
	return x->id < y->id ? -1 : x->id > y->id;
}

/* Windows for the rest list: placing ones too (IMG-3). */
static size_t list_views(struct twm_server *server, struct twm_view **out, size_t cap,
		bool iconified) {
	size_t n = 0;
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (n < cap && v->type != TWM_VIEW_INTERNAL && v->iconified == iconified &&
				!view_hidden_member(v) &&
				(iconified || v->sticky || v->desktop == server->desktop)) {
			out[n++] = v;
		}
	}
	if (!iconified) {
		wl_list_for_each(v, &server->place_queue, place_link) {
			if (n < cap) {
				out[n++] = v;
			}
		}
	}
	enum twm_sort sort = server->config->iconmgr_sort;
	if (sort == TWM_SORT_NAME) {
		qsort(out, n, sizeof(*out), cmp_name);
	} else if (sort == TWM_SORT_AGE) {
		qsort(out, n, sizeof(*out), cmp_age);
	}
	return n;
}

/* A count from the title, like ROOT-9's "(4)". */
/* IMG-3, GRP-7: a group is one row, "foot ×2". */
static const char *row_title(struct twm_view *v, char *buf, size_t len) {
	if (v->group && group_size(v->group) > 1) {
		const char *id = v->impl->get_app_id(v);
		snprintf(buf, len, "%s \xc3\x97%zu", id && *id ? id : view_title(v), group_size(v->group));
		return buf;
	}
	return view_title(v);
}

static void right_label(struct twm_view *v, char *buf, size_t len) {
	buf[0] = '\0';
	if (v->group && group_size(v->group) > 1 && !v->urgent) {
		snprintf(buf, len, "tabs");
		return;
	}
	char label[64];
	icon_label(v->impl->get_title(v), NULL, false, label, sizeof(label));
	const char *dot = strstr(label, " \xc2\xb7 ");
	if (v->urgent) {
		snprintf(buf, len, "!");
	} else if (v->placing) {
		snprintf(buf, len, "placing");
	} else if (dot) {
		snprintf(buf, len, "%s", dot + 4);
	}
}

static void quiet_glyph(pixman_image_t *img, int cx, int cy, float scale, uint32_t c) {
	/* A small bell with a slash (B7), on a 14 px grid. */
	int stroke = draw_stroke(scale);
	draw_box(img, c, S(cx - 4), S(cy - 4), S(8), S(7), stroke);
	draw_fill(img, c, S(cx - 6), S(cy + 3), S(12), stroke);
	draw_fill(img, c, S(cx - 1), S(cy + 5), S(3), stroke);
	for (int i = -6; i <= 6; i++) {
		draw_fill(img, c, S(cx + i), S(cy + i), stroke, stroke);
	}
}

static int draw_desktops(struct imgr *im, pixman_image_t *img, int y, float scale,
		struct twm_font *font, struct twm_font *bold) {
	struct twm_server *server = im->server;
	const struct twm_theme *t = &server->theme;
	int n = desktop_count(server);
	int per_row = n < 4 ? n : 4;
	int bw = (IM_W - 2 * DESK_PAD - (per_row - 1) * DESK_GAP) / per_row;
	int rows = (n + 3) / 4;
	int stroke = draw_stroke(scale);
	for (int i = 0; i < n; i++) {
		int x = DESK_PAD + (i % 4) * (bw + DESK_GAP);
		int by = y + DESK_PAD + (i / 4) * (DESK_H + DESK_GAP);
		bool current = i + 1 == server->desktop;
		bool quiet = server->config->quiet & (1u << i);
		bool empty = desktop_window_count(server, i + 1) == 0;
		uint32_t fg = current ? t->on_accent : empty ? t->dim : t->ink_2;
		if (current) {
			draw_fill(img, argb(t->accent), S(x), S(by), S(bw), S(DESK_H));
		} else {
			draw_box(img, argb(t->hairline), S(x), S(by), S(bw), S(DESK_H), stroke);
		}
		if (quiet && !current) {
			quiet_glyph(img, x + bw / 2, by + DESK_H / 2, scale, argb(t->dim));
		} else {
			char num[12];
			snprintf(num, sizeof(num), "%d", i + 1);
			struct twm_font *f = current ? bold : font;
			int w = text_width(f, num, 0);
			text_draw(img, f, num, S(x + bw / 2) - w / 2,
				text_baseline_centered(f, S(by), S(DESK_H)), argb(fg), 0, 0);
		}
		add_hit(im, x, by, x + bw, by + DESK_H, IM_HIT_DESKTOP, i + 1, 0);
	}
	int h = 2 * DESK_PAD + rows * DESK_H + (rows - 1) * DESK_GAP;
	draw_fill(img, argb(t->hairline), 0, S(y + h), S(IM_W), stroke);
	return h + 1;
}

static int rest_height(struct twm_server *server) {
	struct twm_view *tmp[128];
	size_t a = list_views(server, tmp, 128, false), b = list_views(server, tmp, 128, true);
	int rows = (desktop_count(server) + 3) / 4;
	int h = 2 * DESK_PAD + rows * DESK_H + (rows - 1) * DESK_GAP + 1;
	h += 2 * LIST_PAD + (int)(a + b) * ROW_H;
	if (a && b) {
		h += 2 * DIV_MARGIN + 1;
	}
	return h;
}

static void draw_rest(struct imgr *im, pixman_image_t *img, float scale) {
	struct twm_server *server = im->server;
	const struct twm_theme *t = &server->theme;
	struct twm_font *font = font_get(t->ui_font, TEXT, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *bold = font_get(t->ui_font, TEXT, TWM_WEIGHT_SEMIBOLD, scale);
	int y = draw_desktops(im, img, 0, scale, font, bold);
	y += LIST_PAD;
	struct twm_view *views[128];
	for (int pass = 0; pass < 2; pass++) {
		size_t n = list_views(server, views, 128, pass == 1);
		if (pass == 1 && n && y > 0) {
			struct twm_view *tmp[128];
			if (list_views(server, tmp, 128, false)) {
				draw_fill(img, argb(t->hairline), S(ROW_PAD), S(y + DIV_MARGIN),
					S(IM_W - 2 * ROW_PAD), draw_stroke(scale));
				y += 2 * DIV_MARGIN + 1;
			}
		}
		for (size_t i = 0; i < n; i++) {
			struct twm_view *v = views[i];
			bool focused = server->focused == v;
			uint32_t fg = pass == 1 ? (v->urgent ? t->accent : t->dim) :
				v->urgent ? t->accent : focused ? t->ink : t->ink_2;
			if (focused && pass == 0) {
				struct twm_rgba wash = rgba_over(rgba_hex(t->accent, 0.10f),
					rgba_hex(t->surface, 1));
				draw_fill(img, rgba_argb32(wash), 0, S(y), S(IM_W), S(ROW_H));
			}
			if (v->urgent && pass == 0) {
				draw_fill(img, argb(t->accent), 0, S(y), S(BAR), S(ROW_H));
			}
			draw_marker(img, v, focused && pass == 0, ROW_PAD, y + (ROW_H - MARK) / 2,
				scale, t);
			char right[32];
			right_label(v, right, sizeof(right));
			int rw = *right ? text_width(font, right, 0) : 0;
			int base = text_baseline_centered(font, S(y), S(ROW_H));
			int tx = S(ROW_PAD + MARK + ROW_GAP);
			char rt[96];
			text_draw(img, font, row_title(v, rt, sizeof(rt)), tx, base, argb(fg), 0,
				S(IM_W - ROW_PAD) - tx - (rw ? rw + S(ROW_GAP) : 0));
			if (*right) {
				uint32_t rc = v->urgent ? t->accent : t->dim;
				text_draw(img, font, right, S(IM_W - ROW_PAD) - rw, base, argb(rc), 0, 0);
			}
			add_hit(im, 0, y, IM_W, y + ROW_H, IM_HIT_WINDOW, 0, v->id);
			y += ROW_H;
		}
	}
}

static int filter_height(struct imgr *im, int foot_lines) {
	int h = 2 * FILTER_PAD + INPUT_H + 1;
	h += 2 * LIST_PAD;
	if (im->nwindows) {
		h += HEAD_H + (int)im->nwindows * RES_H;
	}
	if (im->nresults > im->nwindows) {
		h += HEAD_H + (int)(im->nresults - im->nwindows) * RES_H;
	}
	if (!im->nresults) {
		h += RES_H;
	}
	h += 1 + 2 * FOOT_PAD + foot_lines * 16;
	return h;
}

/* Footer hints (IMG-8), wrapped to the width; returns lines used. */
static int draw_footer(struct imgr *im, pixman_image_t *img, int y, float scale,
		struct twm_font *f, bool draw) {
	const struct twm_theme *t = &im->server->theme;
	static const char *const parts[][2] = {
		{"\xe2\x86\xb5", " raise"}, {"M-\xe2\x86\xb5", " bring here"},
		{"Tab", " complete"}, {"Esc", " clear"},
	};
	int x = FOOT_PAD + 2, line = 0;
	for (int i = 0; i < 4; i++) {
		int kw = (int)ceilf(text_width(f, parts[i][0], 0) / scale);
		int lw = (int)ceilf(text_width(f, parts[i][1], 0) / scale);
		if (x + kw + lw > IM_W - FOOT_PAD && x > FOOT_PAD + 2) {
			line++;
			x = FOOT_PAD + 2;
		}
		if (draw) {
			int base = S(y + FOOT_PAD + line * 16) + f->ascent;
			text_draw(img, f, parts[i][0], S(x), base, argb(t->accent), 0, 0);
			text_draw(img, f, parts[i][1], S(x + kw), base, argb(t->muted), 0, 0);
		}
		x += kw + lw + 14;
	}
	return line + 1;
}

static void draw_filter(struct imgr *im, pixman_image_t *img, float scale, int foot_lines) {
	struct twm_server *server = im->server;
	const struct twm_theme *t = &server->theme;
	struct twm_font *font = font_get(t->ui_font, TEXT, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *bold = font_get(t->ui_font, TEXT, TWM_WEIGHT_SEMIBOLD, scale);
	struct twm_font *small = font_get(t->ui_font, META_TEXT, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *input = font_get(t->ui_font, INPUT_TEXT, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *head = font_get(t->ui_font, HEAD_TEXT, TWM_WEIGHT_REGULAR, scale);
	int stroke = draw_stroke(scale);

	/* Filter row: label, input, count (IMG-6). */
	int y = FILTER_PAD;
	int lw = (int)ceilf(text_width(small, "filter", 0) / scale);
	text_draw(img, small, "filter", S(FILTER_PAD), text_baseline_centered(small, S(y), S(INPUT_H)),
		argb(t->muted), 0, 0);
	char count[16];
	snprintf(count, sizeof(count), "%zu", im->nresults);
	int cw = (int)ceilf(text_width(small, count, 0) / scale);
	int ix = FILTER_PAD + lw + 10, iw = IM_W - FILTER_PAD - cw - 10 - ix;
	draw_fill(img, argb(t->ground), S(ix), S(y), S(iw), S(INPUT_H));
	draw_box(img, argb(t->hairline_strong), S(ix), S(y), S(iw), S(INPUT_H), stroke);
	int tw = text_draw(img, input, im->filter, S(ix + 10),
		text_baseline_centered(input, S(y), S(INPUT_H)), argb(t->ink), 0, S(iw - 24));
	draw_fill(img, argb(t->accent), S(ix + 10) + tw + S(1), S(y + 8), S(7), S(INPUT_H - 16));
	text_draw(img, small, count, S(IM_W - FILTER_PAD - cw),
		text_baseline_centered(small, S(y), S(INPUT_H)), argb(t->dim), 0, 0);
	y += INPUT_H + FILTER_PAD;
	draw_fill(img, argb(t->hairline), 0, S(y), S(IM_W), stroke);
	y += 1 + LIST_PAD;

	size_t mlen = strlen(im->filter);
	for (size_t i = 0; i <= im->nresults; i++) {
		bool head_here = (i == 0 && im->nwindows) || (i == im->nwindows && i < im->nresults);
		if (head_here) {
			const char *h = i == 0 && im->nwindows ? "WINDOWS" : "RUN";
			text_draw(img, head, h, S(ROW_PAD), text_baseline_centered(head, S(y), S(HEAD_H)),
				argb(t->dim), 1.5f * scale, 0);
			y += HEAD_H;
		}
		if (i == im->nresults) {
			break;
		}
		struct result *r = &im->results[i];
		if (i == im->sel) {
			struct twm_rgba wash = rgba_over(rgba_hex(t->accent, 0.10f), rgba_hex(t->surface, 1));
			draw_fill(img, rgba_argb32(wash), 0, S(y), S(IM_W), S(RES_H));
			draw_fill(img, argb(t->accent), 0, S(y), S(BAR), S(RES_H));
		}
		int my = y + (RES_H - MARK) / 2;
		struct twm_view *v = r->kind == RES_WINDOW ? view_by_id(server, r->view_id) : NULL;
		if (r->kind == RES_WINDOW) {
			draw_marker(img, v, false, ROW_PAD, my, scale, t);
		} else {
			text_draw(img, font, "!", S(ROW_PAD + 1), text_baseline_centered(font, S(y), S(RES_H)),
				argb(t->dim), 0, 0);
		}
		int mw = (int)ceilf(text_width(small, r->meta, 0) / scale);
		int base = text_baseline_centered(font, S(y), S(RES_H));
		int tx = ROW_PAD + MARK + ROW_GAP;
		uint32_t fg = i == im->sel ? t->ink : t->ink_2;
		draw_matched(img, font, bold, r->text, r->match, r->kind == RES_EXEC ?
			strcspn(im->filter, " ") : mlen, S(tx), base, argb(fg), argb(t->accent),
			S(IM_W - ROW_PAD - mw - ROW_GAP - tx));
		text_draw(img, small, r->meta, S(IM_W - ROW_PAD - mw),
			text_baseline_centered(small, S(y), S(RES_H)),
			argb(i == im->sel ? t->muted : t->dim), 0, 0);
		add_hit(im, 0, y, IM_W, y + RES_H, IM_HIT_RESULT, (int)i, 0);
		y += RES_H;
	}
	if (!im->nresults) {
		text_draw(img, font, "nothing matches", S(ROW_PAD), text_baseline_centered(font, S(y),
			S(RES_H)), argb(t->dim), 0, 0);
		y += RES_H;
	}
	y += LIST_PAD;
	draw_fill(img, argb(t->hairline), 0, S(y), S(IM_W), stroke);
	struct twm_font *foot = font_get(t->ui_font, FOOT_TEXT, TWM_WEIGHT_REGULAR, scale);
	draw_footer(im, img, y + 1, scale, foot, true);
	(void)foot_lines;
}

static void render(struct twm_internal_view *iv, float scale, struct twm_pixbuf **out,
		int *lw, int *lh) {
	struct imgr *im = imgr_of(iv);
	const struct twm_theme *t = &im->server->theme;
	im->nhits = 0;
	bool filtering = im->filter[0] != '\0';
	int foot_lines = 1;
	if (filtering) {
		if (im->config_gen != im->server->theme_gen) {
			compute_results(im); /* the config changed under us */
		}
		struct twm_font *foot = font_get(t->ui_font, FOOT_TEXT, TWM_WEIGHT_REGULAR, scale);
		foot_lines = draw_footer(im, NULL, 0, scale, foot, false);
	}
	*lw = IM_W;
	*lh = filtering ? filter_height(im, foot_lines) : rest_height(im->server);
	struct twm_pixbuf *buf = pixbuf_create(S(*lw), S(*lh));
	if (!buf) {
		return;
	}
	draw_fill(buf->image, argb(t->surface), 0, 0, S(*lw), S(*lh));
	if (filtering) {
		draw_filter(im, buf->image, scale, foot_lines);
	} else {
		draw_rest(im, buf->image, scale);
	}
	*out = buf;
}

#undef S

/* --- actions --- */

static void clear_filter(struct imgr *im) {
	im->filter[0] = '\0';
	im->nresults = im->nwindows = im->sel = 0;
}

static void activate(struct imgr *im, size_t i, bool bring) {
	struct twm_server *server = im->server;
	if (i >= im->nresults) {
		return;
	}
	struct result r = im->results[i];
	char filter[sizeof(im->filter)];
	snprintf(filter, sizeof(filter), "%s", im->filter);
	clear_filter(im);
	switch (r.kind) {
	case RES_WINDOW: {
		struct twm_view *v = view_by_id(server, r.view_id);
		if (v) {
			bring ? view_bring_here(v) : view_present(v); /* IMG-8 */
		}
		break;
	}
	case RES_MENU: {
		struct action_ctx ctx = {.context = TWM_CTX_ROOT};
		action_run(server, r.func, &ctx);
		break;
	}
	case RES_EXEC: {
		/* IMG-9: the typed line if it starts with the command, else the
		 * command alone, in the terminal. */
		char name[128];
		snprintf(name, sizeof(name), "%.*s", (int)strcspn(r.text, " "), r.text);
		action_run_in_terminal(server, strncmp(filter, name, strlen(name)) == 0 &&
			(filter[strlen(name)] == ' ' || !filter[strlen(name)]) ? filter : name);
		break;
	}
	}
	internal_view_redraw(im->iv);
}

static bool on_button(struct twm_internal_view *iv, uint32_t button, bool pressed,
		double x, double y) {
	struct imgr *im = imgr_of(iv);
	struct twm_server *server = im->server;
	if (!pressed) {
		return true;
	}
	for (size_t i = 0; i < im->nhits; i++) {
		struct im_hit *h = &im->hits[i];
		if (x < h->x0 || x >= h->x1 || y < h->y0 || y >= h->y1) {
			continue;
		}
		switch (h->kind) {
		case IM_HIT_DESKTOP:
			if (button == TWM_BTN_LEFT) {
				desktop_switch(server, h->index); /* IMG-2 */
			} else if (button == TWM_BTN_RIGHT) {
				rootline_post(server, ROOTLINE_INFO, "twm", "quiet desktops arrive in M6");
			}
			return true;
		case IM_HIT_WINDOW: {
			struct twm_view *v = view_by_id(server, h->view_id);
			if (v && button == TWM_BTN_LEFT) {
				view_present(v); /* IMG-5 */
			}
			return true;
		}
		case IM_HIT_RESULT:
			if (button == TWM_BTN_LEFT) {
				activate(im, (size_t)h->index, false);
			}
			return true;
		}
	}
	return true;
}

static bool wants_keys_first(struct twm_internal_view *iv) {
	return imgr_of(iv)->filter[0] != '\0';
}

static void complete(struct imgr *im) {
	if (im->sel >= im->nresults) {
		return;
	}
	struct result *r = &im->results[im->sel];
	if (r->kind == RES_EXEC) {
		snprintf(im->filter, sizeof(im->filter), "%.*s ", (int)strcspn(r->text, " "), r->text);
	} else {
		snprintf(im->filter, sizeof(im->filter), "%s", r->text);
	}
}

static bool on_key(struct twm_internal_view *iv, xkb_keysym_t sym, const char *utf8,
		uint32_t mods, bool first) {
	struct imgr *im = imgr_of(iv);
	struct twm_server *server = im->server;
	bool filtering = im->filter[0] != '\0';
	bool plain = !(mods & (TWM_MOD_CTRL | TWM_MOD_ALT | TWM_MOD_LOGO));
	size_t len = strlen(im->filter);

	if (filtering) {
		switch (sym) {
		case XKB_KEY_Escape:
			clear_filter(im); /* IMG-8: Esc clears */
			internal_view_redraw(iv);
			return true;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			activate(im, im->sel, (mods & server->config->modifier) != 0);
			return true;
		case XKB_KEY_Up:
			im->sel = im->sel ? im->sel - 1 : 0;
			internal_view_redraw(iv);
			return true;
		case XKB_KEY_Down:
			if (im->sel + 1 < im->nresults) {
				im->sel++;
			}
			internal_view_redraw(iv);
			return true;
		case XKB_KEY_Tab:
			complete(im);
			compute_results(im);
			internal_view_redraw(iv);
			return true;
		case XKB_KEY_BackSpace:
			while (len > 0 && ((unsigned char)im->filter[len - 1] & 0xC0) == 0x80) {
				len--; /* a whole UTF-8 character */
			}
			if (len > 0) {
				len--;
			}
			im->filter[len] = '\0';
			im->sel = 0;
			if (len) {
				compute_results(im);
			}
			internal_view_redraw(iv);
			return true;
		default:
			break;
		}
	} else if (!first && sym == XKB_KEY_Escape) {
		/* A second Esc leaves: focus goes back to the last window. */
		struct twm_view *best = NULL, *v;
		wl_list_for_each(v, &server->views, link) {
			if (v != &iv->view && view_visible(v) && v->type != TWM_VIEW_INTERNAL &&
					(!best || v->focus_seq > best->focus_seq)) {
				best = v;
			}
		}
		if (best) {
			view_focus(best);
		}
		return true;
	}
	/* Typing starts or extends the filter (IMG-6). */
	if (plain && utf8[0] && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f &&
			len + strlen(utf8) < sizeof(im->filter) - 1) {
		if (!filtering && utf8[0] == ' ') {
			return false;
		}
		strcat(im->filter, utf8);
		im->sel = 0;
		compute_results(im);
		internal_view_redraw(iv);
		return true;
	}
	return filtering; /* while filtering, other keys stay here */
}

static void on_blur(struct twm_internal_view *iv) {
	struct imgr *im = imgr_of(iv);
	if (im->filter[0]) {
		clear_filter(im);
		internal_view_redraw(iv);
	}
}

/* IMG-1: `IconManager { at <corner> }`, 24 px from the edges. */
static void place(struct twm_internal_view *iv, struct wlr_box out, int *x, int *y) {
	struct imgr *im = imgr_of(iv);
	struct wlr_box f = view_frame_box(&iv->view);
	enum twm_corner c = im->server->config->iconmgr_corner;
	bool right = c == TWM_TOP_RIGHT || c == TWM_BOTTOM_RIGHT;
	bool bottom = c == TWM_BOTTOM_LEFT || c == TWM_BOTTOM_RIGHT;
	*x = right ? out.x + out.width - 24 - f.width : out.x + 24;
	*y = bottom ? out.y + out.height - 24 - f.height : out.y + 24;
}

void imgr_init(struct twm_server *server) {
	struct imgr *im = calloc(1, sizeof(*im));
	if (!im) {
		return;
	}
	im->server = server;
	im->iv = internal_view_create(server, "Icon Manager", "twm-iconmgr", render, im);
	im->iv->button = on_button;
	im->iv->key = on_key;
	im->iv->wants_keys_first = wants_keys_first;
	im->iv->blur = on_blur;
	im->iv->place = place;
	server->imgr = im->iv;
}

void imgr_finish(struct twm_server *server) {
	if (!server->imgr) {
		return;
	}
	struct imgr *im = imgr_of(server->imgr);
	internal_view_destroy(server->imgr);
	server->imgr = NULL;
	free_execs(im);
	free(im);
}

void imgr_refresh(struct twm_server *server) {
	struct twm_internal_view *iv = server->imgr;
	if (!iv) {
		return;
	}
	if (!iv->view.shown) {
		if (wl_list_empty(&server->outputs)) {
			return;
		}
		internal_view_show(iv); /* IMG-1: there from the start */
		return;
	}
	struct imgr *im = imgr_of(iv);
	if (im->filter[0]) {
		compute_results(im);
	}
	internal_view_redraw(iv);
}
