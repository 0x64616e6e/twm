#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_output.h>
#include "draw.h"
#include "pixbuf.h"
#include "server.h"
#include "settings.h"
#include "text.h"

/* ROOT-2 / ROOT-3 metrics (B0). */
#define WORDMARK_SIZE 168
#define WORDMARK_TRACKING -2.0f
#define WORDMARK_LEFT 0.51f
#define WORDMARK_BOTTOM 32
#define STATUS_SIZE 11
#define STATUS_TRACKING 1.5f
#define STATUS_INDENT 10
#define STATUS_TOP_FROM_BOTTOM 34

/* ROOT-1: each output's root is `ground` with the root weave. */
void root_update(struct twm_output *output) {
	struct twm_server *server = output->server;
	const struct twm_theme *t = &server->theme;
	float scale = output->wlr_output->scale;
	int dw = (int)lroundf(output->box.width * scale);
	int dh = (int)lroundf(output->box.height * scale);

	struct twm_pixbuf *buf = pixbuf_create(dw, dh);
	if (!buf) {
		return;
	}
	struct twm_rgba ground = rgba_hex(t->ground, 1);
	draw_fill(buf->image, rgba_argb32(ground), 0, 0, dw, dh);
	if (t->root_weave) {
		struct twm_rgba dot = rgba_over(rgba_hex(t->ink, 0.07f), ground);
		draw_weave(buf->image, scale, 6, output->box.x, output->box.y,
			rgba_argb32(dot));
	}

	if (!output->root) {
		output->root = wlr_scene_buffer_create(server->layers[TWM_LAYER_ROOT], NULL);
		output->root->node.data = &server->root_hit;
	}
	wlr_scene_buffer_set_buffer(output->root, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(output->root, output->box.width, output->box.height);
	pixman_region32_t opaque;
	pixman_region32_init_rect(&opaque, 0, 0, output->box.width, output->box.height);
	wlr_scene_buffer_set_opaque_region(output->root, &opaque);
	pixman_region32_fini(&opaque);
	wlr_scene_node_set_position(&output->root->node, output->box.x, output->box.y);
}

static struct twm_output *primary_output(struct twm_server *server) {
	if (wl_list_empty(&server->outputs)) {
		return NULL;
	}
	struct twm_output *output = wl_container_of(server->outputs.next, output, link);
	return output;
}

/* Draw one line of text into a scene buffer; returns false if nothing drew. */
static bool set_text(struct twm_server *server, struct wlr_scene_buffer **node,
		struct twm_font *font, const char *text, uint32_t argb, float tracking,
		float scale, int lx, int baseline_ly) {
	if (!*node) {
		*node = wlr_scene_buffer_create(server->layers[TWM_LAYER_ROOT_TEXT], NULL);
		(*node)->node.data = &server->root_hit;
	}
	if (!font || !font->fcft) { /* no such face: skip it (§2.2) */
		wlr_scene_node_set_enabled(&(*node)->node, false);
		return false;
	}
	int w = text_width(font, text, tracking);
	int pad = (int)ceilf(2 * scale); /* room for italic overhang */
	int dw = w + 2 * pad, dh = font->ascent + font->descent;
	struct twm_pixbuf *buf = pixbuf_create(dw > 0 ? dw : 1, dh > 0 ? dh : 1);
	if (!buf) {
		return false;
	}
	text_draw(buf->image, font, text, pad, font->ascent, argb, tracking, 0);
	wlr_scene_buffer_set_buffer(*node, &buf->base);
	pixbuf_drop(buf);
	int lw = (int)ceilf(dw / scale), lh = (int)ceilf(dh / scale);
	wlr_scene_buffer_set_dest_size(*node, lw, lh);
	wlr_scene_node_set_position(&(*node)->node, lx - (int)ceilf(pad / scale),
		baseline_ly - (int)lroundf(font->ascent / scale));
	wlr_scene_node_set_enabled(&(*node)->node, true);
	return true;
}

static void status_text(struct twm_server *server, char *buf, size_t len) {
	char host[64] = "";
	gethostname(host, sizeof(host) - 1);
	char *dot = strchr(host, '.');
	if (dot) {
		*dot = '\0';
	}
	char display[64];
	snprintf(display, sizeof(display), "%s", server->socket ? server->socket : "");
	for (char *p = display; *p; p++) {
		if (*p == '-') {
			*p = ' ';
		}
	}
	int outputs = wl_list_length(&server->outputs);
	const char *policy = server->config &&
		server->config->focus_policy == TWM_FOCUS_CLICK ? "CLICK" : "SLOPPY";
	snprintf(buf, len, "%s \xc2\xb7 %s \xc2\xb7 %d\xc3\x97 OUTPUT%s \xc2\xb7 %s FOCUS",
		host, display, outputs, outputs == 1 ? "" : "S", policy);
	for (char *p = buf; *p; p++) {
		*p = (char)toupper((unsigned char)*p);
	}
}

/* ROOT-2 wordmark and ROOT-3 status line, on the primary output. */
void root_update_text(struct twm_server *server) {
	const struct twm_theme *t = &server->theme;
	struct twm_output *output = primary_output(server);
	if (!output) {
		if (server->wordmark) {
			wlr_scene_node_set_enabled(&server->wordmark->node, false);
		}
		if (server->status_line) {
			wlr_scene_node_set_enabled(&server->status_line->node, false);
		}
		return;
	}
	float scale = output->wlr_output->scale;
	struct wlr_box b = output->box;
	int left = b.x + (int)lroundf(b.width * WORDMARK_LEFT);

	if (t->root_wordmark) {
		/* Instrument Serif Italic, else any serif italic (§2.2). */
		char family[128];
		snprintf(family, sizeof(family), "%s,serif:style=Italic", "Instrument Serif");
		struct twm_font *wf = font_get(family, WORDMARK_SIZE, TWM_WEIGHT_REGULAR, scale);
		/* Line box of 168 px with its bottom at H - 32 (line-height 1). */
		int box_top = b.y + b.height - WORDMARK_BOTTOM - WORDMARK_SIZE;
		int baseline = box_top;
		if (wf) {
			int asc = (int)lroundf(wf->ascent / scale);
			int desc = (int)lroundf(wf->descent / scale);
			baseline = box_top + (WORDMARK_SIZE - (asc + desc)) / 2 + asc;
		}
		set_text(server, &server->wordmark, wf, "Tom's WM",
			rgba_argb32(rgba_hex(t->ink, 0.07f)), WORDMARK_TRACKING * scale,
			scale, left, baseline);
	} else if (server->wordmark) {
		wlr_scene_node_set_enabled(&server->wordmark->node, false);
	}

	char status[256];
	status_text(server, status, sizeof(status));
	struct twm_font *sf = font_get(t->ui_font, STATUS_SIZE, TWM_WEIGHT_REGULAR, scale);
	int top = b.y + b.height - STATUS_TOP_FROM_BOTTOM;
	int baseline = top + (sf ? (int)lroundf(sf->ascent / scale) : 0);
	set_text(server, &server->status_line, sf, status,
		rgba_argb32(rgba_hex(t->dim, 1)), STATUS_TRACKING * scale, scale,
		left + STATUS_INDENT, baseline);
}

void root_finish(struct twm_server *server) {
	server->wordmark = NULL;
	server->status_line = NULL;
}
