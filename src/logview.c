#include <math.h>
#include <stdlib.h>
#include "draw.h"
#include "internal.h"
#include "logview.h"
#include "pixbuf.h"
#include "rootline.h"
#include "text.h"

#define LV_W 640
#define LV_PAD 12
#define LV_LINE 18
#define LV_TEXT 12
#define LV_LINES 20

#define S(v) ((int)lroundf((float)(v) * scale))

static void render(struct twm_internal_view *iv, float scale, struct twm_pixbuf **out,
		int *lw, int *lh) {
	struct twm_server *server = iv->view.server;
	const struct twm_theme *t = &server->theme;
	size_t n = 0;
	while (n < LV_LINES && rootline_get(server, n)) {
		n++;
	}
	size_t rows = n ? n : 1;
	*lw = LV_W;
	*lh = 2 * LV_PAD + (int)rows * LV_LINE;
	struct twm_pixbuf *buf = pixbuf_create(S(*lw), S(*lh));
	if (!buf) {
		return;
	}
	draw_fill(buf->image, rgba_argb32(rgba_hex(t->surface, 1)), 0, 0, S(*lw), S(*lh));
	struct twm_font *font = font_get(t->ui_font, LV_TEXT, TWM_WEIGHT_REGULAR, scale);
	if (!n) {
		text_draw(buf->image, font, "no messages", S(LV_PAD),
			text_baseline_centered(font, S(LV_PAD), S(LV_LINE)),
			rgba_argb32(rgba_hex(t->dim, 1)), 0, 0);
	}
	/* Oldest at the top, newest at the bottom, like the root line. */
	for (size_t i = 0; i < n; i++) {
		const struct rootline_msg *m = rootline_get(server, n - 1 - i);
		char line[256];
		rootline_format(m, line, sizeof(line));
		uint32_t c = m->kind == ROOTLINE_ERROR ? t->accent : t->ink_2;
		int y = LV_PAD + (int)i * LV_LINE;
		text_draw(buf->image, font, line, S(LV_PAD), text_baseline_centered(font, S(y), S(LV_LINE)),
			rgba_argb32(rgba_hex(c, 1)), 0, S(LV_W - 2 * LV_PAD));
	}
	*out = buf;
}

#undef S

static void place(struct twm_internal_view *iv, struct wlr_box out, int *x, int *y) {
	*x = out.x + 24;
	*y = out.y + 24;
}

void logview_init(struct twm_server *server) {
	server->logview = internal_view_create(server, "log", "twm-log", render, NULL);
	server->logview->place = place;
}

void logview_finish(struct twm_server *server) {
	internal_view_destroy(server->logview);
	server->logview = NULL;
}

void logview_toggle(struct twm_server *server) {
	if (server->logview) {
		internal_view_toggle(server->logview);
	}
}

void logview_refresh(struct twm_server *server) {
	if (server->logview && server->logview->view.shown) {
		internal_view_redraw(server->logview);
	}
}
