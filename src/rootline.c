#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "pixbuf.h"
#include "rootline.h"
#include "settings.h"
#include "text.h"
#include "ui.h"

/* ROOT-5 metrics: 12 px lines, 18 px apart, 10 px above the status line. */
#define LINE_SIZE 12
#define LINE_STEP 18
#define GAP_ABOVE_STATUS 10
#define STATUS_TOP_FROM_BOTTOM 34
#define LEFT_FRACTION 0.51f
#define LEFT_INDENT 10

static uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void rootline_format(const struct rootline_msg *msg, char *buf, size_t len) {
	struct tm tm;
	localtime_r(&msg->when, &tm);
	/* B7: "22:33  .twmrc   line 14: …" — the source padded to 8 columns. */
	snprintf(buf, len, "%02d:%02d  %-8s %s", tm.tm_hour, tm.tm_min, msg->source, msg->text);
}

static const struct rootline_msg *nth_newest(struct twm_rootline *rl, size_t n) {
	if (n >= rl->count) {
		return NULL;
	}
	size_t i = (rl->head + ROOTLINE_KEEP - 1 - n) % ROOTLINE_KEEP;
	return &rl->msgs[i];
}

const struct rootline_msg *rootline_get(struct twm_server *server, size_t i) {
	return server->rootline ? nth_newest(server->rootline, i) : NULL;
}

static int handle_timer(void *data) {
	rootline_update(data);
	return 0;
}

void rootline_update(struct twm_server *server) {
	struct twm_rootline *rl = server->rootline;
	if (!rl) {
		return;
	}
	uint64_t now = now_ms();
	const struct rootline_msg *shown[ROOTLINE_VISIBLE];
	size_t nshown = 0;
	uint64_t next_expiry = 0;
	for (size_t i = 0; i < rl->count && nshown < ROOTLINE_VISIBLE; i++) {
		const struct rootline_msg *m = nth_newest(rl, i);
		if (m->expires_ms && m->expires_ms <= now) {
			continue; /* expired; a sticky one may be older */
		}
		shown[nshown++] = m;
		if (m->expires_ms && (!next_expiry || m->expires_ms < next_expiry)) {
			next_expiry = m->expires_ms;
		}
	}
	wl_event_source_timer_update(rl->timer,
		next_expiry ? (int)(next_expiry - now + 1) : 0);

	struct twm_output *output = wl_list_empty(&server->outputs) ? NULL :
		wl_container_of(server->outputs.next, output, link);
	if (!output || nshown == 0) {
		if (rl->node) {
			wlr_scene_node_set_enabled(&rl->node->node, false);
		}
		return;
	}
	const struct twm_theme *t = &server->theme;
	float scale = output->wlr_output->scale;
	struct twm_font *font = font_get(t->ui_font, LINE_SIZE, TWM_WEIGHT_REGULAR, scale);
	if (!font) {
		return;
	}

	char lines[ROOTLINE_VISIBLE][256];
	int width = 1;
	for (size_t i = 0; i < nshown; i++) {
		rootline_format(shown[i], lines[i], sizeof(lines[i]));
		int w = text_width(font, lines[i], 0);
		width = w > width ? w : width;
	}
	int step = (int)lroundf(LINE_STEP * scale);
	int dh = step * (int)(nshown - 1) + font->ascent + font->descent;
	struct twm_pixbuf *buf = pixbuf_create(width, dh);
	if (!buf) {
		return;
	}
	/* Oldest at the top, newest at the bottom (ROOT-5). */
	for (size_t i = 0; i < nshown; i++) {
		const struct rootline_msg *m = shown[i];
		uint32_t color = m->kind == ROOTLINE_ERROR ? t->accent : i == 0 ? t->ink_2 : t->dim;
		int row = (int)(nshown - 1 - i);
		text_draw(buf->image, font, lines[i], 0, row * step + font->ascent,
			rgba_argb32(rgba_hex(color, 1)), 0, 0);
	}
	if (!rl->node) {
		rl->node = wlr_scene_buffer_create(server->layers[TWM_LAYER_ROOT_TEXT], NULL);
		rl->node->node.data = &server->root_hit;
	}
	wlr_scene_buffer_set_buffer(rl->node, &buf->base);
	pixbuf_drop(buf);
	int lw = (int)ceilf(width / scale), lh = (int)ceilf(dh / scale);
	wlr_scene_buffer_set_dest_size(rl->node, lw, lh);
	struct wlr_box b = output->box;
	int left = b.x + (int)lroundf(b.width * LEFT_FRACTION) + LEFT_INDENT;
	int bottom = b.y + b.height - STATUS_TOP_FROM_BOTTOM - GAP_ABOVE_STATUS;
	wlr_scene_node_set_position(&rl->node->node, left, bottom - lh);
	wlr_scene_node_set_enabled(&rl->node->node, true);
}

void rootline_post(struct twm_server *server, enum rootline_kind kind,
		const char *source, const char *fmt, ...) {
	struct twm_rootline *rl = server->rootline;
	char text[sizeof(rl->msgs[0].text)];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	wlr_log(kind == ROOTLINE_ERROR ? WLR_ERROR : WLR_INFO, "%s: %s", source, text);
	if (!rl) {
		return;
	}
	struct rootline_msg *m = &rl->msgs[rl->head];
	rl->head = (rl->head + 1) % ROOTLINE_KEEP;
	if (rl->count < ROOTLINE_KEEP) {
		rl->count++;
	}
	int timeout = server->config ? server->config->root_line_timeout : 60;
	*m = (struct rootline_msg){
		.kind = kind,
		.when = time(NULL),
		.expires_ms = now_ms() + (uint64_t)timeout * 1000,
	};
	snprintf(m->source, sizeof(m->source), "%s", source);
	snprintf(m->text, sizeof(m->text), "%s", text);
	rootline_update(server);
	ui_changed(server); /* the log window (ROOT-8) */
}

void rootline_stick(struct twm_server *server, uint64_t key, enum rootline_kind kind,
		const char *source, const char *text) {
	rootline_post(server, kind, source, "%s", text);
	struct twm_rootline *rl = server->rootline;
	if (rl && rl->count) {
		struct rootline_msg *m = &rl->msgs[(rl->head + ROOTLINE_KEEP - 1) % ROOTLINE_KEEP];
		m->expires_ms = 0;
		m->key = key;
		rootline_update(server);
	}
}

void rootline_unstick(struct twm_server *server, uint64_t key) {
	struct twm_rootline *rl = server->rootline;
	if (!rl || !key) {
		return;
	}
	for (size_t i = 0; i < ROOTLINE_KEEP; i++) {
		if (rl->msgs[i].key == key) {
			rl->msgs[i].key = 0;
			rl->msgs[i].expires_ms = 1; /* long past */
		}
	}
	rootline_update(server);
}

void rootline_init(struct twm_server *server) {
	struct twm_rootline *rl = calloc(1, sizeof(*rl));
	if (!rl) {
		return;
	}
	rl->timer = wl_event_loop_add_timer(server->loop, handle_timer, server);
	server->rootline = rl;
}

void rootline_finish(struct twm_server *server) {
	struct twm_rootline *rl = server->rootline;
	if (!rl) {
		return;
	}
	wl_event_source_remove(rl->timer);
	free(rl);
	server->rootline = NULL;
}
