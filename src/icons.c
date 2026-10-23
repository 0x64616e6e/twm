#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "desktop.h"
#include "draw.h"
#include "icons.h"
#include "pixbuf.h"
#include "settings.h"
#include "text.h"
#include "view.h"

/* ROOT-9 metrics (B0). */
#define SLOT_W 72
#define SLOT_GAP 20
#define BOX 56
#define GLYPH_SIZE 20
#define LABEL_SIZE 11
#define LABEL_GAP 8
#define LABEL_PAD 4
#define ORIGIN_X 72
#define ORIGIN_FROM_BOTTOM 140

void icon_label(const char *title, const char *app_id, bool urgent, char *buf, size_t len) {
	/* The first word of the title (B0 "htop", "mutt"), else the app id. */
	char name[32] = "";
	const char *src = title && *title ? title : app_id ? app_id : "";
	size_t i = 0;
	while (src[i] && src[i] != ' ' && i + 1 < sizeof(name) && i < 16) {
		name[i] = src[i];
		i++;
	}
	name[i] = '\0';
	if (urgent) {
		snprintf(buf, len, "%s \xc2\xb7 urgent", name);
		return;
	}
	/* A count like "(4)" at the start or end of the title (decided, ROOT-9). */
	int count = -1;
	const char *t = title ? title : "";
	size_t tl = strlen(t);
	if (t[0] == '(') {
		char *end;
		long n = strtol(t + 1, &end, 10);
		if (end != t + 1 && *end == ')') {
			count = (int)n;
		}
	} else if (tl > 2 && t[tl - 1] == ')') {
		const char *open = strrchr(t, '(');
		char *end;
		if (open) {
			long n = strtol(open + 1, &end, 10);
			if (end != open + 1 && end == t + tl - 1) {
				count = (int)n;
			}
		}
	}
	if (count >= 0) {
		snprintf(buf, len, "%s \xc2\xb7 %d", name, count);
	} else {
		snprintf(buf, len, "%s", name);
	}
}

static void render(struct twm_icon *icon) {
	struct twm_view *view = icon->view;
	struct twm_server *server = view->server;
	const struct twm_theme *t = &server->theme;
	float scale = output_max_scale(server);
	bool urgent = view->urgent;
	uint32_t fg = urgent ? t->accent : t->ink_2;
	char label[64];
	icon_label(view->impl->get_title(view), view->impl->get_app_id(view), urgent,
		label, sizeof(label));

	struct twm_font *lf = font_get(t->ui_font, LABEL_SIZE, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *gf = font_get(t->ui_font, GLYPH_SIZE, TWM_WEIGHT_SEMIBOLD, scale);
	if (!lf || !gf) {
		return;
	}
	int label_w = (int)ceilf(text_width(lf, label, 0) / scale) + 2 * LABEL_PAD;
	int w = label_w > SLOT_W ? label_w : SLOT_W;
	int label_h = (int)ceilf(lf->height / scale);
	int h = BOX + LABEL_GAP + label_h;
#define S(v) ((int)lroundf((float)(v) * scale))
	struct twm_pixbuf *buf = pixbuf_create(S(w), S(h));
	if (!buf) {
		return;
	}
	int bx = (w - BOX) / 2;
	draw_fill(buf->image, rgba_argb32(rgba_hex(t->surface, 1)), S(bx), 0, S(BOX), S(BOX));
	draw_box(buf->image, rgba_argb32(rgba_hex(urgent ? t->accent : t->hairline, 1)),
		S(bx), 0, S(BOX), S(BOX), draw_stroke(scale));
	/* Glyph: the app id's first letter (no app icons yet). */
	const char *id = view->impl->get_app_id(view);
	char glyph[2] = {(char)toupper((unsigned char)(id && *id ? id[0] : label[0])), '\0'};
	int gw = text_width(gf, glyph, 0);
	text_draw(buf->image, gf, glyph, S(bx) + (S(BOX) - gw) / 2,
		text_baseline_centered(gf, 0, S(BOX)), rgba_argb32(rgba_hex(fg, 1)), 0, 0);
	/* Label on ground, centred under the box. */
	int lx = (w - label_w) / 2;
	draw_fill(buf->image, rgba_argb32(rgba_hex(t->ground, 1)), S(lx), S(BOX + LABEL_GAP),
		S(label_w), S(label_h));
	text_draw(buf->image, lf, label, S(lx + LABEL_PAD), S(BOX + LABEL_GAP) + lf->ascent,
		rgba_argb32(rgba_hex(fg, 1)), 0, 0);
#undef S
	wlr_scene_buffer_set_buffer(icon->buffer, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(icon->buffer, w, h);
	wlr_scene_node_set_position(&icon->buffer->node, -(w - SLOT_W) / 2, 0);
	icon->width = w;
}

void icon_create(struct twm_view *view) {
	struct twm_server *server = view->server;
	if (view->icon || !server->config->icons_on_root) {
		return;
	}
	struct twm_icon *icon = calloc(1, sizeof(*icon));
	if (!icon) {
		return;
	}
	icon->view = view;
	icon->order = ++server->icon_counter;
	icon->hit = (struct twm_hit){.kind = TWM_HIT_ICON, .view = view};
	icon->tree = wlr_scene_tree_create(server->layers[TWM_LAYER_ROOT_TEXT]);
	icon->tree->node.data = &icon->hit;
	icon->buffer = wlr_scene_buffer_create(icon->tree, NULL);
	view->icon = icon;
	icons_layout(server);
}

void icon_destroy(struct twm_view *view) {
	struct twm_icon *icon = view->icon;
	if (!icon) {
		return;
	}
	wlr_scene_node_destroy(&icon->tree->node);
	free(icon);
	view->icon = NULL;
	icons_layout(view->server);
}

void icon_move(struct twm_view *view, int x, int y) {
	struct twm_icon *icon = view->icon;
	if (!icon) {
		return;
	}
	icon->x = x;
	icon->y = y;
	icon->user_placed = true;
	wlr_scene_node_set_position(&icon->tree->node, x, y);
}

static int by_order(const void *a, const void *b) {
	const struct twm_icon *x = *(struct twm_icon *const *)a, *y = *(struct twm_icon *const *)b;
	return x->order < y->order ? -1 : x->order > y->order;
}

void icons_layout(struct twm_server *server) {
	struct twm_output *out = wl_list_empty(&server->outputs) ? NULL :
		wl_container_of(server->outputs.next, out, link);
	size_t n = 0;
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		n += v->icon != NULL;
	}
	if (!n || !out) {
		return;
	}
	struct twm_icon **list = calloc(n, sizeof(*list));
	size_t k = 0;
	wl_list_for_each(v, &server->views, link) {
		if (v->icon) {
			list[k++] = v->icon;
		}
	}
	qsort(list, k, sizeof(*list), by_order);
	int slot = 0;
	for (size_t i = 0; i < k; i++) {
		struct twm_icon *icon = list[i];
		struct twm_view *view = icon->view;
		bool here = view->sticky || view->desktop == server->desktop;
		wlr_scene_node_set_enabled(&icon->tree->node, here);
		if (!here) {
			continue;
		}
		render(icon);
		if (!icon->user_placed) {
			icon->x = out->box.x + ORIGIN_X + slot * (SLOT_W + SLOT_GAP);
			icon->y = out->box.y + out->box.height - ORIGIN_FROM_BOTTOM;
			slot++;
		}
		wlr_scene_node_set_position(&icon->tree->node, icon->x, icon->y);
	}
	free(list);
}
