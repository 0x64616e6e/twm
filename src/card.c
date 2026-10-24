#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_output.h>
#include "actions.h"
#include "card.h"
#include "desktop.h"
#include "draw.h"
#include "info.h"
#include "layers.h"
#include "pixbuf.h"
#include "settings.h"
#include "sysinfo.h"
#include "text.h"

/*
 * The root card (CARD-*), from B9: the host name in the wordmark's serif,
 * then the facts in 13 px lines, drawn into one buffer on the root text
 * layer of the primary output. It redraws only when what it says changes.
 */

#define TICK_MS 1000
#define LOAD_EVERY_MS 15000 /* one sparkline sample: eight make two minutes */
#define POOL_EVERY_MS 30000 /* CARD-3: the pool costs a process */
#define INSET 80
#define LINE_SIZE 13
#define LINE_PITCH 25 /* 21 px lines, 4 px apart */
#define HOST_SIZE 44
#define HOST_GAP 12 /* under the host name */
#define CELLS 10
#define SAMPLES 8

struct twm_card {
	struct twm_server *server;
	struct wlr_scene_buffer *node;
	struct twm_hit hit;
	struct wl_event_source *timer;
	bool forced;

	double load[SAMPLES];
	int nload;
	uint64_t load_ms;

	bool have_pool;
	struct sys_pool pool;
	uint64_t pool_ms;
	int pool_fd;
	struct wl_event_source *pool_src;
	char pool_out[4096];
	size_t pool_len;

	char drawn[1024];
};

static bool wanted(struct twm_card *c) {
	struct twm_server *s = c->server;
	return (c->forced || (s->config && s->config->root_card)) && !wl_list_empty(&s->outputs);
}

/* --- the pool, in the background --- */

static void pool_done(struct twm_card *c) {
	c->pool_out[c->pool_len] = '\0';
	c->have_pool = sys_parse_zpool(c->pool_out, &c->pool);
	wl_event_source_remove(c->pool_src);
	close(c->pool_fd);
	c->pool_src = NULL;
	c->pool_fd = -1;
}

static int pool_readable(int fd, uint32_t mask, void *data) {
	struct twm_card *c = data;
	if (mask & WL_EVENT_READABLE) {
		ssize_t n = read(fd, c->pool_out + c->pool_len, sizeof(c->pool_out) - 1 - c->pool_len);
		if (n > 0) {
			c->pool_len += (size_t)n;
			if (c->pool_len < sizeof(c->pool_out) - 1) {
				return 0;
			}
		} else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			return 0;
		}
	}
	pool_done(c);
	return 0;
}

static void pool_start(struct twm_card *c) {
	if (c->pool_src) {
		return; /* still running */
	}
	int fds[2];
	if (pipe(fds) < 0) {
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return;
	}
	if (pid == 0) {
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		dup2(fds[1], STDOUT_FILENO);
		int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDERR_FILENO);
		}
		close(fds[0]);
		close(fds[1]);
		execl("/bin/sh", "/bin/sh", "-c", SYS_ZPOOL_CMD, (char *)NULL);
		_exit(127);
	}
	/* reaped with the others (server.c's SIGCHLD handler) */
	close(fds[1]);
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	c->pool_fd = fds[0];
	c->pool_len = 0;
	c->pool_src = wl_event_loop_add_fd(c->server->loop, fds[0], WL_EVENT_READABLE, pool_readable, c);
}

/* --- what it says --- */

enum seg_kind { SEG_TEXT, SEG_SPARK, SEG_BAR };

struct seg {
	enum seg_kind kind;
	char text[96];
	uint32_t argb;
	double share; /* SEG_BAR: the used part, 0…1 */
};

#define MAX_LINES 5
#define MAX_SEGS 8

struct card_text {
	char host[128];
	int nlines;
	int nsegs[MAX_LINES];
	struct seg segs[MAX_LINES][MAX_SEGS];
};

static struct seg *seg(struct card_text *t, int line) {
	if (t->nsegs[line] == MAX_SEGS) {
		return &t->segs[line][MAX_SEGS - 1];
	}
	struct seg *s = &t->segs[line][t->nsegs[line]++];
	memset(s, 0, sizeof(*s));
	return s;
}

static void text_seg(struct card_text *t, int line, uint32_t argb, const char *fmt, ...)
	__attribute__((format(printf, 4, 5)));
static void text_seg(struct card_text *t, int line, uint32_t argb, const char *fmt, ...) {
	struct seg *s = seg(t, line);
	s->kind = SEG_TEXT;
	s->argb = argb;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s->text, sizeof(s->text), fmt, ap);
	va_end(ap);
}

static void compose(struct twm_card *c, struct card_text *t) {
	struct twm_server *s = c->server;
	const struct twm_theme *th = &s->theme;
	unsigned f = s->config->card_fields;
	uint32_t body = rgba_argb32(rgba_hex(th->ink_2, 1));
	uint32_t dim = rgba_argb32(rgba_hex(th->dim, 1));
	uint32_t accent = rgba_argb32(rgba_hex(th->accent, 1));
	memset(t, 0, sizeof(*t));
	if (f & TWM_CARD_HOST) {
		sys_host(t->host, sizeof(t->host));
	}
	int l = 0;
	if (f & (TWM_CARD_UPTIME | TWM_CARD_LOAD)) {
		if (f & TWM_CARD_UPTIME) {
			char up[32];
			sys_fmt_uptime_short(sys_uptime(), up, sizeof(up));
			text_seg(t, l, body, "up %s", up);
		}
		if ((f & TWM_CARD_LOAD) && c->nload) {
			text_seg(t, l, body, "%sload ", (f & TWM_CARD_UPTIME) ? "  \xc2\xb7  " : "");
			struct seg *sp = seg(t, l);
			sp->kind = SEG_SPARK;
			sp->argb = accent;
			text_seg(t, l, body, " %.2f", c->load[c->nload - 1]);
		}
		l++;
	}
	int label_w = 3; /* "mem" */
	if ((f & TWM_CARD_POOL) && c->have_pool) {
		int n = (int)strlen(c->pool.name);
		label_w = n > label_w ? (n > 10 ? 10 : n) : label_w;
	}
	if ((f & TWM_CARD_POOL) && c->have_pool) {
		char fr[16];
		sys_fmt_bytes(c->pool.free, fr, sizeof(fr));
		text_seg(t, l, dim, "%-*.*s  ", label_w, label_w, c->pool.name);
		struct seg *b = seg(t, l);
		b->kind = SEG_BAR;
		b->argb = accent;
		b->share = c->pool.size ? (double)c->pool.alloc / (double)c->pool.size : 0;
		text_seg(t, l, body, "  %s free", fr);
		l++;
	}
	struct sys_mem m;
	if ((f & TWM_CARD_MEM) && sys_mem(&m) && m.total) {
		double gib = 1024.0 * 1024 * 1024;
		text_seg(t, l, dim, "%-*s  ", label_w, "mem");
		struct seg *b = seg(t, l);
		b->kind = SEG_BAR;
		b->argb = accent;
		b->share = (double)m.used / (double)m.total;
		text_seg(t, l, body, "  %.1f / %.0fG", (double)m.used / gib, (double)m.total / gib);
		l++;
	}
	if (f & TWM_CARD_FRAMES) {
		struct info_counts ic;
		info_count(s, &ic);
		char g[32] = "";
		if (ic.groups) {
			snprintf(g, sizeof(g), " \xc2\xb7 %d group%s", ic.groups, ic.groups == 1 ? "" : "s");
		}
		text_seg(t, l, dim, "%d frame%s%s \xc2\xb7 desk %d/%d", ic.open, ic.open == 1 ? "" : "s",
			g, s->desktop, desktop_count(s));
		l++;
	}
	t->nlines = l;
}

/* A signature of everything drawn: redraw only when it changes. */
static void signature(struct twm_card *c, const struct card_text *t, float scale,
		struct wlr_box ua, char *out, size_t len) {
	size_t o = (size_t)snprintf(out, len, "%s|%g|%u|%d|%d,%d,%d,%d", t->host, (double)scale,
		c->server->theme_gen, c->server->config->card_corner, ua.x, ua.y, ua.width, ua.height);
	for (int l = 0; l < t->nlines; l++) {
		for (int i = 0; i < t->nsegs[l] && o < len; i++) {
			const struct seg *s = &t->segs[l][i];
			o += (size_t)snprintf(out + o, len - o, "|%s%.2f", s->text, s->share);
		}
	}
	for (int i = 0; i < c->nload && o < len; i++) {
		o += (size_t)snprintf(out + o, len - o, ",%.2f", c->load[i]);
	}
}

static struct twm_output *primary(struct twm_server *s) {
	if (wl_list_empty(&s->outputs)) {
		return NULL;
	}
	struct twm_output *o = wl_container_of(s->outputs.next, o, link);
	return o;
}

static void draw(struct twm_card *c, bool force) {
	struct twm_server *s = c->server;
	if (!wanted(c)) {
		if (c->node) {
			wlr_scene_node_set_enabled(&c->node->node, false);
		}
		c->drawn[0] = '\0';
		return;
	}
	struct twm_output *out = primary(s);
	const struct twm_theme *th = &s->theme;
	float scale = out->wlr_output->scale;
	struct wlr_box ua = output_usable(out);
	struct card_text t;
	compose(c, &t);
	char sig[sizeof(c->drawn)];
	signature(c, &t, scale, ua, sig, sizeof(sig));
	if (!force && c->node && strcmp(sig, c->drawn) == 0) {
		return;
	}
	snprintf(c->drawn, sizeof(c->drawn), "%s", sig);

	char family[128];
	snprintf(family, sizeof(family), "%s,serif:style=Italic", "Instrument Serif");
	struct twm_font *hf = font_get(family, HOST_SIZE, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *lf = font_get(th->ui_font, LINE_SIZE, TWM_WEIGHT_REGULAR, scale);
#define X(v) ((int)lroundf((float)(v) * scale))
	int cw = text_width(lf, "0", 0); /* one cell, device px */
	if (cw <= 0) {
		cw = X(8);
	}
	/* widths, device px */
	int width = t.host[0] ? text_width(hf, t.host, 0) + X(4) : 0;
	for (int l = 0; l < t.nlines; l++) {
		int w = 0;
		for (int i = 0; i < t.nsegs[l]; i++) {
			const struct seg *sg = &t.segs[l][i];
			w += sg->kind == SEG_TEXT ? text_width(lf, sg->text, 0) :
				(sg->kind == SEG_SPARK ? SAMPLES : CELLS) * cw;
		}
		width = w > width ? w : width;
	}
	int host_h = t.host[0] ? HOST_SIZE + HOST_GAP : 0;
	int lh = host_h + t.nlines * LINE_PITCH; /* logical */
	int lw = (int)ceilf((float)width / scale) + 2;
	struct twm_pixbuf *buf = pixbuf_create(X(lw), X(lh));
	if (!buf) {
		return;
	}
	pixman_image_t *img = buf->image;
	if (t.host[0] && hf->fcft) {
		int base = text_baseline_centered(hf, 0, X(HOST_SIZE));
		text_draw(img, hf, t.host, X(1), base, rgba_argb32(rgba_hex(th->ink, 0.55f)), 0, 0);
	}
	uint32_t empty = rgba_argb32(rgba_hex(th->hairline_strong, 1));
	for (int l = 0; l < t.nlines; l++) {
		int top = X(host_h + l * LINE_PITCH);
		int base = text_baseline_centered(lf, top, X(21));
		int x = X(1);
		for (int i = 0; i < t.nsegs[l]; i++) {
			const struct seg *sg = &t.segs[l][i];
			if (sg->kind == SEG_TEXT) {
				x += text_draw(img, lf, sg->text, x, base, sg->argb, 0, 0);
			} else if (sg->kind == SEG_SPARK) {
				/* ▂▃▅▃▂▁▂▃: eight samples, the tallest the line's ascent */
				double max = 1.0; /* a quiet machine stays low */
				for (int k = 0; k < c->nload; k++) {
					max = c->load[k] > max ? c->load[k] : max;
				}
				for (int k = 0; k < SAMPLES; k++) {
					int idx = c->nload - SAMPLES + k;
					if (idx < 0) {
						continue; /* not sampled yet: nothing */
					}
					double v = c->load[idx];
					int h = (int)lround(v / max * (lf->ascent * 0.8));
					h = h < X(1) ? X(1) : h;
					draw_fill(img, sg->argb, x + k * cw, base - h, cw, h);
				}
				x += SAMPLES * cw;
			} else {
				/* ▮▮▮▮▮▮▮▮▮▮: ten cells, the used share in accent */
				int used = (int)lround(sg->share * CELLS);
				int bw = cw * 6 / 10, bh = lf->ascent * 3 / 4;
				for (int k = 0; k < CELLS; k++) {
					draw_fill(img, k < used ? sg->argb : empty, x + k * cw + (cw - bw) / 2,
						base - bh, bw, bh);
				}
				x += CELLS * cw;
			}
		}
	}
#undef X
	if (!c->node) {
		c->node = wlr_scene_buffer_create(s->layers[TWM_LAYER_ROOT_TEXT], NULL);
		c->hit = (struct twm_hit){.kind = TWM_HIT_CARD};
		c->node->node.data = &c->hit;
	}
	wlr_scene_buffer_set_buffer(c->node, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(c->node, lw, lh);
	enum twm_corner corner = s->config->card_corner;
	bool right = corner == TWM_TOP_RIGHT || corner == TWM_BOTTOM_RIGHT;
	bool bottom = corner == TWM_BOTTOM_LEFT || corner == TWM_BOTTOM_RIGHT;
	int x = right ? ua.x + ua.width - INSET - lw : ua.x + INSET;
	int y = bottom ? ua.y + ua.height - INSET - lh : ua.y + INSET;
	wlr_scene_node_set_position(&c->node->node, x, y);
	wlr_scene_node_set_enabled(&c->node->node, true);
}

static void sample(struct twm_card *c, bool now) {
	uint64_t ms = server_now_ms();
	if (now || !c->nload || ms - c->load_ms >= LOAD_EVERY_MS) {
		double v = sys_load();
		if (v >= 0) {
			if (c->nload == SAMPLES) {
				memmove(c->load, c->load + 1, (SAMPLES - 1) * sizeof(c->load[0]));
				c->nload--;
			}
			c->load[c->nload++] = v;
		}
		c->load_ms = ms;
	}
	if ((c->server->config->card_fields & TWM_CARD_POOL) &&
			(!c->pool_ms || ms - c->pool_ms >= POOL_EVERY_MS)) {
		c->pool_ms = ms;
		pool_start(c);
	}
}

static int tick(void *data) {
	struct twm_card *c = data;
	if (!wanted(c)) {
		draw(c, false);
		return 0; /* no timer while it's off */
	}
	sample(c, false);
	draw(c, false);
	wl_event_source_timer_update(c->timer, TICK_MS);
	return 0;
}

void card_configure(struct twm_server *server) {
	struct twm_card *c = server->card;
	if (!c) {
		return;
	}
	if (wanted(c)) {
		sample(c, false);
		draw(c, true);
		wl_event_source_timer_update(c->timer, TICK_MS);
	} else {
		draw(c, false);
	}
}

void card_force(struct twm_server *server, bool on) {
	struct twm_card *c = server->card;
	if (!c) {
		return;
	}
	c->forced = on;
	card_configure(server);
}

void card_open_fetch(struct twm_server *server) {
	/* CARD-3: the full twmfetch, kept open afterwards */
	action_run_in_terminal(server, "sh -c 'twmfetch --no-shot; exec \"${SHELL:-sh}\"'");
}

void card_init(struct twm_server *server) {
	struct twm_card *c = calloc(1, sizeof(*c));
	if (!c) {
		return;
	}
	c->server = server;
	c->pool_fd = -1;
	c->timer = wl_event_loop_add_timer(server->loop, tick, c);
	server->card = c;
	card_configure(server);
}

void card_finish(struct twm_server *server) {
	struct twm_card *c = server->card;
	if (!c) {
		return;
	}
	if (c->pool_src) {
		wl_event_source_remove(c->pool_src);
		close(c->pool_fd);
	}
	wl_event_source_remove(c->timer);
	if (c->node) {
		wlr_scene_node_destroy(&c->node->node);
	}
	free(c);
	server->card = NULL;
}
