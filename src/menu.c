#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "actions.h"
#include "draw.h"
#include "menu.h"
#include "pixbuf.h"
#include "settings.h"
#include "text.h"
#include "view.h"

/* MNU-2 metrics (B0). */
#define MN_W 260
#define MN_HEADER 32
#define MN_ITEM 30
#define MN_SEP 9
#define MN_PAD 14
#define MN_LIST_PAD 4
#define MN_TEXT 13
#define MN_HEAD_TEXT 12
#define MN_HATCH_W 60
#define MN_HATCH_H 10
#define MN_MAX 64
#define DRAG_THRESHOLD 4

struct twm_menu_ui {
	struct twm_server *server;
	char name[64];
	struct wlr_scene_buffer *node;
	int x, y, w, h;
	int nitems;
	int item_y[MN_MAX], item_h[MN_MAX];
	int selected;
	struct twm_menu_ui *child, *parent;
};

/* Press-drag-release vs. click-and-click (MNU-1). */
static struct {
	bool by_press;   /* opened by a press whose release hasn't come yet */
	bool moved;      /* the pointer travelled since */
	double ox, oy;
} mstate;

static const struct twm_menu *menu_def(struct twm_menu_ui *m) {
	return config_menu(m->server->config, m->name);
}

static bool is_window_func(enum twm_func_kind k) {
	switch (k) {
	case TWM_F_MOVE: case TWM_F_RESIZE: case TWM_F_RAISEMOVE: case TWM_F_SQUEEZE:
	case TWM_F_ICONIFY: case TWM_F_RAISE: case TWM_F_LOWER: case TWM_F_RAISELOWER:
	case TWM_F_FOCUS: case TWM_F_DELETE: case TWM_F_SEND:
		return true;
	default:
		return false;
	}
}

void menu_shortcut(struct twm_server *server, const struct twm_func *func, char *buf, size_t len) {
	buf[0] = '\0';
	if (func->kind == TWM_F_MENU) {
		snprintf(buf, len, "\xe2\x96\xb8"); /* ▸ */
		return;
	}
	const struct twm_button_binding *b =
		config_button_for(server->config, func, TWM_CTX_TITLE);
	if (b && b->mods == 0) {
		snprintf(buf, len, "B%u", b->button); /* B0: move "B2", squeeze "B3" */
		return;
	}
	const struct twm_key_binding *k = config_key_for(server->config, func);
	if (!k) {
		return;
	}
	/* "M-Return" → "M-↵", "M-space" → "M-␣" (B0). */
	char spec[64];
	snprintf(spec, sizeof(spec), "%s", k->spec);
	char *last = strrchr(spec, '-');
	const char *key = last && last[1] ? last + 1 : spec;
	const char *pretty = strcasecmp(key, "Return") == 0 ? "\xe2\x86\xb5" :
		strcasecmp(key, "space") == 0 ? "\xe2\x90\xa3" : key;
	snprintf(buf, len, "%.*s%s", (int)(key - spec), spec, pretty);
}

#define S(v) ((int)lroundf((float)(v) * scale))

static void render(struct twm_menu_ui *m) {
	struct twm_server *server = m->server;
	const struct twm_theme *t = &server->theme;
	const struct twm_menu *def = menu_def(m);
	float scale = output_max_scale(server);
	m->nitems = def ? (int)(def->nitems < MN_MAX ? def->nitems : MN_MAX) : 0;
	int y = MN_HEADER + MN_LIST_PAD;
	for (int i = 0; i < m->nitems; i++) {
		m->item_y[i] = y;
		m->item_h[i] = def->items[i].func.kind == TWM_F_SEPARATOR ? MN_SEP : MN_ITEM;
		y += m->item_h[i];
	}
	m->w = MN_W;
	m->h = y + MN_LIST_PAD;
	struct twm_pixbuf *buf = pixbuf_create(S(m->w), S(m->h));
	if (!buf) {
		return;
	}
	pixman_image_t *img = buf->image;
	int stroke = draw_stroke(scale);
	struct twm_font *font = font_get(t->ui_font, MN_TEXT, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *bold = font_get(t->ui_font, MN_TEXT, TWM_WEIGHT_SEMIBOLD, scale);
	struct twm_font *hf = font_get(t->ui_font, MN_HEAD_TEXT, TWM_WEIGHT_REGULAR, scale);
#define C(rgb) rgba_argb32(rgba_hex((rgb), 1))
	draw_fill(img, C(t->surface), 0, 0, S(m->w), S(m->h));
	/* Header: "TOM'S WM" and a hatch on graphite. */
	draw_fill(img, C(t->graphite), 0, 0, S(m->w), S(MN_HEADER));
	char title[64];
	snprintf(title, sizeof(title), "%s", strcmp(m->name, "main") == 0 ? "TOM'S WM" : m->name);
	for (char *p = title; *p; p++) {
		*p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
	}
	text_draw(img, hf, title, S(MN_PAD), text_baseline_centered(hf, 0, S(MN_HEADER)),
		C(t->muted), 1.0f * scale, 0);
	draw_hatch(img, S(m->w - MN_PAD - MN_HATCH_W), S((MN_HEADER - MN_HATCH_H) / 2),
		S(MN_HATCH_W), S(MN_HATCH_H), scale, C(t->hairline_strong));
	for (int i = 0; i < m->nitems; i++) {
		const struct twm_menu_item *it = &def->items[i];
		int iy = m->item_y[i];
		if (it->func.kind == TWM_F_SEPARATOR) {
			draw_fill(img, C(t->hairline), S(MN_PAD), S(iy + MN_SEP / 2), S(m->w - 2 * MN_PAD),
				stroke);
			continue;
		}
		bool sel = i == m->selected;
		if (sel) {
			draw_fill(img, C(t->accent), 0, S(iy), S(m->w), S(MN_ITEM));
		}
		char sc[32];
		menu_shortcut(server, &it->func, sc, sizeof(sc));
		struct twm_font *f = sel ? bold : font;
		int sw = text_width(f, sc, 0);
		int base = text_baseline_centered(f, S(iy), S(MN_ITEM));
		text_draw(img, f, it->label, S(MN_PAD), base, C(sel ? t->on_accent : t->ink), 0,
			S(m->w - 2 * MN_PAD) - sw - S(10));
		text_draw(img, f, sc, S(m->w - MN_PAD) - sw, base, C(sel ? t->on_accent : t->dim), 0, 0);
	}
	draw_box(img, C(t->hairline_strong), 0, 0, S(m->w), S(m->h), stroke);
#undef C
	if (!m->node) {
		m->node = wlr_scene_buffer_create(server->layers[TWM_LAYER_MENU], NULL);
	}
	wlr_scene_buffer_set_buffer(m->node, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(m->node, m->w, m->h);
	wlr_scene_node_set_position(&m->node->node, m->x, m->y);
}

#undef S

static struct twm_menu_ui *create(struct twm_server *server, const char *name,
		double x, double y, struct twm_menu_ui *parent) {
	if (!config_menu(server->config, name)) {
		return NULL;
	}
	struct twm_menu_ui *m = calloc(1, sizeof(*m));
	if (!m) {
		return NULL;
	}
	m->server = server;
	snprintf(m->name, sizeof(m->name), "%s", name);
	m->selected = -1;
	m->parent = parent;
	m->x = (int)x;
	m->y = (int)y;
	render(m);
	/* MNU-1: kept on the output. */
	struct twm_output *o = output_at(server, x, y);
	if (o) {
		if (m->x + m->w > o->box.x + o->box.width) {
			m->x = parent ? parent->x - m->w : o->box.x + o->box.width - m->w;
		}
		if (m->y + m->h > o->box.y + o->box.height) {
			m->y = o->box.y + o->box.height - m->h;
		}
		if (m->x < o->box.x) {
			m->x = o->box.x;
		}
		if (m->y < o->box.y) {
			m->y = o->box.y;
		}
	}
	wlr_scene_node_set_position(&m->node->node, m->x, m->y);
	return m;
}

static void destroy(struct twm_menu_ui *m) {
	if (!m) {
		return;
	}
	destroy(m->child);
	if (m->node) {
		wlr_scene_node_destroy(&m->node->node);
	}
	free(m);
}

void menu_close_all(struct twm_server *server) {
	if (!server->menu) {
		return;
	}
	destroy(server->menu);
	server->menu = NULL;
	input_update_pointer_focus(server, 0);
}

bool menu_active(struct twm_server *server) {
	return server->menu != NULL;
}

void menu_open(struct twm_server *server, const char *name, double x, double y, bool by_press) {
	menu_close_all(server);
	server->menu = create(server, name, x, y, NULL);
	if (!server->menu) {
		return;
	}
	mstate.by_press = by_press;
	mstate.moved = false;
	mstate.ox = x;
	mstate.oy = y;
	wlr_seat_pointer_clear_focus(server->seat); /* the menu has the pointer */
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
}

static int item_at(struct twm_menu_ui *m, double x, double y) {
	if (x < m->x || x >= m->x + m->w) {
		return -1;
	}
	for (int i = 0; i < m->nitems; i++) {
		if (y >= m->y + m->item_y[i] && y < m->y + m->item_y[i] + m->item_h[i]) {
			const struct twm_menu *def = menu_def(m);
			return def && def->items[i].func.kind != TWM_F_SEPARATOR ? i : -1;
		}
	}
	return -1;
}

static bool inside(struct twm_menu_ui *m, double x, double y) {
	return x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h;
}

static struct twm_menu_ui *deepest(struct twm_server *server) {
	struct twm_menu_ui *m = server->menu;
	while (m && m->child) {
		m = m->child;
	}
	return m;
}

static void select_item(struct twm_menu_ui *m, int i) {
	if (m->selected == i) {
		return;
	}
	m->selected = i;
	render(m);
	const struct twm_menu *def = menu_def(m);
	/* A submenu opens beside its item; anything else closes one. */
	if (m->child && (i < 0 || !def || def->items[i].func.kind != TWM_F_MENU ||
			strcmp(def->items[i].func.arg, m->child->name) != 0)) {
		destroy(m->child);
		m->child = NULL;
	}
	if (i >= 0 && def && def->items[i].func.kind == TWM_F_MENU && !m->child) {
		m->child = create(m->server, def->items[i].func.arg, m->x + m->w - 1,
			m->y + m->item_y[i] - MN_HEADER - MN_LIST_PAD, m);
	}
}

void menu_motion(struct twm_server *server, double x, double y) {
	if (!server->menu) {
		return;
	}
	if (fabs(x - mstate.ox) > DRAG_THRESHOLD || fabs(y - mstate.oy) > DRAG_THRESHOLD) {
		mstate.moved = true;
	}
	/* The deepest menu under the pointer gets the highlight. */
	for (struct twm_menu_ui *m = deepest(server); m; m = m->parent) {
		if (inside(m, x, y)) {
			select_item(m, item_at(m, x, y));
			return;
		}
	}
}

static void run_item(struct twm_server *server, struct twm_menu_ui *m, int i) {
	const struct twm_menu *def = menu_def(m);
	if (!def || i < 0 || i >= m->nitems) {
		return;
	}
	struct twm_func func = def->items[i].func;
	if (func.kind == TWM_F_MENU || func.kind == TWM_F_SEPARATOR) {
		return;
	}
	char *arg = func.arg ? strdup(func.arg) : NULL; /* the config may go away */
	func.arg = arg;
	menu_close_all(server);
	if (is_window_func(func.kind)) {
		select_begin(server, &func); /* MNU-5 */
	} else {
		struct action_ctx ctx = {.context = TWM_CTX_ROOT,
			.lx = server->cursor->x, .ly = server->cursor->y};
		action_run(server, &func, &ctx);
	}
	free(arg);
}

void menu_button(struct twm_server *server, uint32_t button, bool pressed) {
	double x = server->cursor->x, y = server->cursor->y;
	struct twm_menu_ui *hit = NULL;
	for (struct twm_menu_ui *m = deepest(server); m && !hit; m = m->parent) {
		if (inside(m, x, y)) {
			hit = m;
		}
	}
	if (pressed) {
		if (!hit) {
			menu_close_all(server); /* MNU-1: a click outside closes */
		}
		return;
	}
	if (mstate.by_press && !mstate.moved) {
		mstate.by_press = false; /* a click: the menu stays open */
		return;
	}
	mstate.by_press = false;
	if (!hit) {
		menu_close_all(server); /* released outside after dragging */
		return;
	}
	run_item(server, hit, item_at(hit, x, y));
}

static int step(struct twm_menu_ui *m, int from, int dir) {
	const struct twm_menu *def = menu_def(m);
	if (!def || m->nitems == 0) {
		return -1;
	}
	int i = from;
	for (int n = 0; n < m->nitems; n++) {
		i = (i + dir + m->nitems) % m->nitems;
		if (def->items[i].func.kind != TWM_F_SEPARATOR) {
			return i;
		}
	}
	return -1;
}

void menu_key(struct twm_server *server, xkb_keysym_t sym) {
	struct twm_menu_ui *m = deepest(server);
	if (!m) {
		return;
	}
	const struct twm_menu *def = menu_def(m);
	switch (sym) {
	case XKB_KEY_Escape:
		menu_close_all(server);
		return;
	case XKB_KEY_Down:
		select_item(m, step(m, m->selected < 0 ? -1 : m->selected, 1));
		return;
	case XKB_KEY_Up:
		select_item(m, step(m, m->selected < 0 ? 0 : m->selected, -1));
		return;
	case XKB_KEY_Left:
		if (m->parent) {
			m->parent->child = NULL;
			destroy(m);
		}
		return;
	case XKB_KEY_Right:
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if (m->selected < 0 || !def) {
			return;
		}
		if (def->items[m->selected].func.kind == TWM_F_MENU) {
			if (m->child) {
				select_item(m->child, step(m->child, -1, 1));
			}
			return;
		}
		if (sym != XKB_KEY_Right) {
			run_item(server, m, m->selected);
		}
		return;
	default:
		return;
	}
}

void select_begin(struct twm_server *server, const struct twm_func *func) {
	select_cancel(server);
	struct twm_func *copy = calloc(1, sizeof(*copy));
	if (!copy) {
		return;
	}
	*copy = *func;
	copy->arg = func->arg ? strdup(func->arg) : NULL;
	server->select_func = copy;
	server->selecting = true;
	wlr_seat_pointer_clear_focus(server->seat);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "crosshair");
}

void select_cancel(struct twm_server *server) {
	if (!server->selecting) {
		return;
	}
	server->selecting = false;
	if (server->select_func) {
		free(server->select_func->arg);
		free(server->select_func);
		server->select_func = NULL;
	}
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	input_update_pointer_focus(server, 0);
}

bool select_press(struct twm_server *server) {
	if (!server->selecting) {
		return false;
	}
	double sx, sy;
	struct wlr_surface *surface;
	struct twm_hit hit = server_hit_at(server, server->cursor->x, server->cursor->y,
		&surface, &sx, &sy);
	struct twm_func *func = server->select_func;
	server->select_func = NULL;
	server->selecting = false;
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	if (hit.view && func) {
		struct action_ctx ctx = {
			.view = hit.view,
			.context = hit.kind == TWM_HIT_ICON ? TWM_CTX_ICON : TWM_CTX_WINDOW,
			.button = 0, /* the outline then follows the pointer; a click ends it */
			.lx = server->cursor->x, .ly = server->cursor->y,
		};
		action_run(server, func, &ctx);
	}
	if (func) {
		free(func->arg);
		free(func);
	}
	return true;
}
