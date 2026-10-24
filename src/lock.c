#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "auth.h"
#include "desktop.h"
#include "draw.h"
#include "grab.h"
#include "hints.h"
#include "info.h"
#include "internal.h"
#include "lock.h"
#include "menu.h"
#include "pixbuf.h"
#include "prompt.h"
#include "rootline.h"
#include "settings.h"
#include "sysinfo.h"
#include "text.h"
#include "ui.h"
#include "view.h"

/*
 * GRT-2 metrics, B11 at 1:1: the frame 560 wide (558 inside the 1 px
 * accent border the frame draws), an 11 px weave band, the panel padded
 * 30/34. The badge sits 26 px above the frame.
 */
#define LK_W 558
#define LK_BAND 11
#define LK_PAD_Y 30
#define LK_PAD_X 34
#define LK_NAME 44
#define LK_FIELD_H 46
#define LK_BADGE_H 30
#define LK_BADGE_GAP 26
#define LK_INSET 80
#define TRIES 5
#define COOLDOWN_MS 60000

struct twm_lock {
	struct twm_server *server;
	bool active;
	char why[48];
	time_t at;
	char user[64], host[128];

	struct twm_internal_view *iv;
	struct wlr_scene_rect *veil;
	struct wlr_scene_buffer *badge, *clock, *card, *line;
	struct wl_event_source *tick;
	uint64_t restore_focus;

	/* the PAM conversation */
	struct auth *auth;
	struct auth *spent; /* finished; freed outside its callback */
	struct wl_event_source *reap;
	char question[128]; /* "PASSWORD" */
	bool echo, asking, busy;
	bool submit; /* Return came before PAM asked (a fingerprint first) */
	char input[512];
	size_t ilen;
	char msg[200]; /* a failure, in alert */
	char info[200]; /* PAM's info lines ("fingerprint ready") */
	int tries;
	uint64_t cooldown_until;
	bool caps;

	/* idle */
	struct wl_event_source *idle_lock, *idle_screen;
	bool dark;
	int inhibitors;
	struct wlr_idle_notifier_v1 *notifier;
	struct wlr_idle_inhibit_manager_v1 *inhibit;
	struct wl_listener new_inhibitor;
};

struct inhibitor {
	struct twm_lock *lock;
	struct wl_listener destroy;
};

bool lock_active(struct twm_server *server) {
	return server->lock && server->lock->active;
}

static uint32_t C(uint32_t rgb) {
	return rgba_argb32(rgba_hex(rgb, 1));
}

#define S(v) ((int)lroundf((float)(v) * scale))

static int held_urgent(struct twm_server *server) {
	int n = 0;
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		n += v->urgent_held;
	}
	return n;
}

/* --- the frame's content --- */

static int content_height(void) {
	/* band, panel: padding, name row, gap, label, gap, field, gap, hint,
	 * rule, the session line, padding; band */
	return 2 * LK_BAND + 2 + 2 * LK_PAD_Y + LK_NAME + 22 + 14 + 10 + LK_FIELD_H + 10 + 16 +
		16 + 1 + 14 + 14;
}

static void render(struct twm_internal_view *iv, float scale, struct twm_pixbuf **out,
		int *lw, int *lh) {
	struct twm_lock *lk = iv->data;
	struct twm_server *server = lk->server;
	const struct twm_theme *t = &server->theme;
	*lw = LK_W;
	*lh = content_height();
	struct twm_pixbuf *buf = pixbuf_create(S(*lw), S(*lh));
	if (!buf) {
		return;
	}
	pixman_image_t *img = buf->image;
	int stroke = draw_stroke(scale);
	/* TRU-1: the band (trusted weave), the inner border, the panel */
	draw_fill(img, C(t->ground), 0, 0, S(*lw), S(*lh));
	struct twm_rgba dot = rgba_over(rgba_hex(t->ink, 0.30f), rgba_hex(t->ground, 1));
	draw_weave(img, scale, 4, 0, 0, rgba_argb32(dot));
	int ix = LK_BAND, iy = LK_BAND, iw = *lw - 2 * LK_BAND, ih = *lh - 2 * LK_BAND;
	draw_fill(img, C(t->surface), S(ix), S(iy), S(iw), S(ih));
	draw_box(img, C(t->graphite), S(ix), S(iy), S(iw), S(ih), stroke);

	int x = ix + 1 + LK_PAD_X, w = iw - 2 - 2 * LK_PAD_X;
	int y = iy + 1 + LK_PAD_Y;
	/* the user, in the wordmark's serif; @ host on the right */
	char family[128];
	snprintf(family, sizeof(family), "%s,serif:style=Italic", "Instrument Serif");
	struct twm_font *name = font_get(family, LK_NAME, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *small = font_get(t->ui_font, 12, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *field = font_get(t->ui_font, 18, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *mono = font_get(t->ui_font, 14, TWM_WEIGHT_REGULAR, scale);
	int base = S(y) + name->ascent;
	text_draw(img, name, lk->user, S(x), base, C(t->ink), 0, S(w - 140));
	char at[160];
	snprintf(at, sizeof(at), "@ %s", lk->host);
	int aw = text_width(small, at, 0);
	text_draw(img, small, at, S(x + w) - aw, base, C(t->dim), 0, 0);
	y += LK_NAME + 22;

	/* PASSWORD: PAM's own question, as a label */
	text_draw(img, small, lk->question[0] ? lk->question : "PASSWORD", S(x), S(y) + small->ascent,
		C(t->muted), 1.0f * scale, 0);
	y += 14 + 10;

	/* the field: % on the left, what was typed, the caret block on the right */
	bool live = lk->asking && !lk->busy;
	draw_fill(img, C(t->ground), S(x), S(y), S(w), S(LK_FIELD_H));
	draw_box(img, C(live ? t->accent : t->hairline_strong), S(x), S(y), S(w), S(LK_FIELD_H), stroke);
	int fb = text_baseline_centered(mono, S(y), S(LK_FIELD_H));
	text_draw(img, mono, "%", S(x + 12), fb, C(t->dim), 0, 0);
	char shown[600] = "";
	if (lk->echo) {
		snprintf(shown, sizeof(shown), "%.*s", (int)lk->ilen, lk->input);
	} else {
		/* one dot a character, not a byte */
		size_t o = 0;
		for (size_t i = 0; i < lk->ilen && o + 4 < sizeof(shown); i++) {
			if (((unsigned char)lk->input[i] & 0xC0) != 0x80) {
				memcpy(shown + o, "\xe2\x80\xa2", 3); /* • */
				o += 3;
			}
		}
		shown[o] = '\0';
	}
	int tx = x + 12 + 24;
	text_draw(img, field, shown, S(tx), text_baseline_centered(field, S(y), S(LK_FIELD_H)),
		C(t->ink), lk->echo ? 0 : 4.0f * scale, S(w - 36 - 30));
	if (live) {
		draw_fill(img, C(t->accent), S(x + w - 14 - 10), S(y + (LK_FIELD_H - 20) / 2), S(10), S(20));
	}
	y += LK_FIELD_H + 10;

	/* under the field: the failure (GRT-5), PAM's word, or the hint */
	char hint[256];
	uint32_t hc = t->dim;
	uint64_t now = server_now_ms();
	if (lk->cooldown_until > now) {
		snprintf(hint, sizeof(hint), "pam: %d failed tries \xc2\xb7 wait %d s", TRIES,
			(int)((lk->cooldown_until - now + 999) / 1000));
		hc = t->alert;
	} else if (lk->busy) {
		snprintf(hint, sizeof(hint), "checking\xe2\x80\xa6");
	} else if (lk->msg[0]) {
		snprintf(hint, sizeof(hint), "%s", lk->msg);
		hc = t->alert;
	} else if (lk->info[0]) {
		snprintf(hint, sizeof(hint), "%s", lk->info);
	} else {
		snprintf(hint, sizeof(hint), "caps %s \xc2\xb7 \xe2\x8f\x8e unlock", lk->caps ? "on" : "off");
		if (lk->caps) {
			hc = t->accent;
		}
	}
	text_draw(img, small, hint, S(x), S(y) + small->ascent, C(hc), 0, S(w));
	y += 16 + 16;

	/* GRT-4: the session is kept; no session list while locked */
	draw_fill(img, C(t->graphite), S(x), S(y), S(w), stroke);
	y += 1 + 14;
	struct info_counts ic;
	info_count(server, &ic);
	char kept[96];
	snprintf(kept, sizeof(kept), "session kept \xc2\xb7 %d frame%s held", ic.open, ic.open == 1 ? "" : "s");
	struct twm_font *tiny = font_get(t->ui_font, 11, TWM_WEIGHT_REGULAR, scale);
	text_draw(img, tiny, kept, S(x), S(y) + tiny->ascent, C(t->dim), 0, S(w));
	*out = buf;
}

/* --- around the frame: badge, clock, card, root line --- */

static struct twm_output *lock_output(struct twm_server *server) {
	struct twm_output *o = output_at(server, server->cursor->x, server->cursor->y);
	if (!o && !wl_list_empty(&server->outputs)) {
		o = wl_container_of(server->outputs.next, o, link);
	}
	return o;
}

/* Put `buf` (lw × lh logical) into *node at (x, y) on the lock's layer. */
static void place_buffer(struct twm_lock *lk, struct wlr_scene_buffer **node,
		struct twm_pixbuf *buf, int lw, int lh, int x, int y) {
	if (!*node) {
		*node = wlr_scene_buffer_create(lk->server->layers[TWM_LAYER_LOCK], NULL);
	}
	if (!buf) {
		return;
	}
	wlr_scene_buffer_set_buffer(*node, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(*node, lw, lh);
	wlr_scene_node_set_position(&(*node)->node, x, y);
	wlr_scene_node_set_enabled(&(*node)->node, true);
}

static void draw_badge(struct twm_lock *lk, struct twm_output *o, int frame_y) {
	struct twm_server *server = lk->server;
	const struct twm_theme *t = &server->theme;
	float scale = o->wlr_output->scale;
	struct twm_font *f = font_get(t->ui_font, 12, TWM_WEIGHT_SEMIBOLD, scale);
	char text[64];
	long idle = (long)(time(NULL) - lk->at);
	if (strcmp(lk->why, "on idle") == 0) {
		int mins = (server->config->idle_lock + (int)idle) / 60;
		snprintf(text, sizeof(text), "LOCKED \xc2\xb7 IDLE %d MIN", mins);
	} else {
		snprintf(text, sizeof(text), "LOCKED");
	}
	int tw = (int)ceilf((float)text_width(f, text, 2.0f * scale) / scale);
	int lw = 16 + 13 + 10 + tw + 16, lh = LK_BADGE_H;
	struct twm_pixbuf *buf = pixbuf_create(S(lw), S(lh));
	if (buf) {
		draw_fill(buf->image, C(t->graphite), 0, 0, S(lw), S(lh));
		draw_glyph(buf->image, TWM_GLYPH_LOCK, S(16), S((lh - 12) / 2), scale, C(t->accent));
		text_draw(buf->image, f, text, S(16 + 13 + 10), text_baseline_centered(f, 0, S(lh)),
			C(t->accent), 2.0f * scale, 0);
	}
	place_buffer(lk, &lk->badge, buf, lw, lh, o->box.x + (o->box.width - lw) / 2,
		frame_y - LK_BADGE_GAP - lh);
}

static void draw_clock(struct twm_lock *lk, struct twm_output *o) {
	const struct twm_theme *t = &lk->server->theme;
	float scale = o->wlr_output->scale;
	struct twm_font *big = font_get(t->ui_font, 40, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *small = font_get(t->ui_font, 12, TWM_WEIGHT_REGULAR, scale);
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char hm[16], date[48];
	strftime(hm, sizeof(hm), "%H:%M", &tm);
	strftime(date, sizeof(date), "%a %d %b %Y", &tm);
	int w1 = text_width(big, hm, 1.0f * scale), w2 = text_width(small, date, 0);
	int lw = (int)ceilf((float)(w1 > w2 ? w1 : w2) / scale) + 2, lh = 40 + 6 + 16;
	struct twm_pixbuf *buf = pixbuf_create(S(lw), S(lh));
	if (buf) {
		text_draw(buf->image, big, hm, S(lw) - w1, big->ascent, C(t->ink), 1.0f * scale, 0);
		text_draw(buf->image, small, date, S(lw) - w2, S(46) + small->ascent, C(t->muted), 0, 0);
	}
	place_buffer(lk, &lk->clock, buf, lw, lh, o->box.x + o->box.width - LK_INSET - lw,
		o->box.y + 64);
}

static void draw_card(struct twm_lock *lk, struct twm_output *o) {
	struct twm_server *server = lk->server;
	const struct twm_theme *t = &server->theme;
	float scale = o->wlr_output->scale;
	struct twm_font *label = font_get(t->ui_font, 11, TWM_WEIGHT_REGULAR, scale);
	struct twm_font *line = font_get(t->ui_font, 13, TWM_WEIGHT_REGULAR, scale);
	char host[128], os[200], up[32], l2[260], l3[96];
	snprintf(host, sizeof(host), "%s", lk->host);
	for (char *p = host; *p; p++) {
		*p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
	}
	sys_os(os, sizeof(os));
	sys_fmt_uptime_short(sys_uptime(), up, sizeof(up));
	snprintf(l2, sizeof(l2), "%s \xc2\xb7 up %s", os, up);
	struct info_counts ic;
	info_count(server, &ic);
	snprintf(l3, sizeof(l3), "%d frame%s held \xc2\xb7 desktop %d/%d", ic.open, ic.open == 1 ? "" : "s",
		server->desktop, desktop_count(server));
	int w = text_width(line, l2, 0);
	int lw = (int)ceilf((float)w / scale) + 2, lh = 16 + 5 + 20 + 5 + 20;
	struct twm_pixbuf *buf = pixbuf_create(S(lw), S(lh));
	if (buf) {
		text_draw(buf->image, label, host, 0, label->ascent, C(t->dim), 2.0f * scale, 0);
		text_draw(buf->image, line, l2, 0, S(21) + line->ascent, C(t->ink_2), 0, 0);
		text_draw(buf->image, line, l3, 0, S(46) + line->ascent, C(t->dim), 0, 0);
	}
	place_buffer(lk, &lk->card, buf, lw, lh, o->box.x + LK_INSET, o->box.y + 72);
}

static void draw_line(struct twm_lock *lk, struct twm_output *o) {
	struct twm_server *server = lk->server;
	const struct twm_theme *t = &server->theme;
	float scale = o->wlr_output->scale;
	struct twm_font *f = font_get(t->ui_font, 13, TWM_WEIGHT_REGULAR, scale);
	struct tm tm;
	localtime_r(&lk->at, &tm);
	char hm[16], a[96], b[96] = "";
	strftime(hm, sizeof(hm), "%H:%M", &tm);
	snprintf(a, sizeof(a), " session locked %s", lk->why);
	int held = held_urgent(server);
	if (held) {
		snprintf(b, sizeof(b), " %d urgent held until unlock", held);
	}
	int wa = text_width(f, hm, 0) + text_width(f, a, 0);
	int wb = held ? text_width(f, "M-u", 0) + text_width(f, b, 0) : 0;
	int gap = held ? S(26) : 0;
	int lw = (int)ceilf((float)(wa + gap + wb) / scale) + 2, lh = 20;
	struct twm_pixbuf *buf = pixbuf_create(S(lw), S(lh));
	if (buf) {
		int base = text_baseline_centered(f, 0, S(lh)), x = 0;
		x += text_draw(buf->image, f, hm, x, base, C(t->dim), 0, 0);
		x += text_draw(buf->image, f, a, x, base, C(t->muted), 0, 0);
		if (held) {
			x += gap;
			x += text_draw(buf->image, f, "M-u", x, base, C(t->accent), 0, 0);
			text_draw(buf->image, f, b, x, base, C(t->muted), 0, 0);
		}
	}
	place_buffer(lk, &lk->line, buf, lw, lh, o->box.x + (o->box.width - lw) / 2,
		o->box.y + o->box.height - 58 - lh);
}

#undef S

static void redraw(struct twm_lock *lk) {
	if (!lk->active || !lk->iv) {
		return;
	}
	struct twm_server *server = lk->server;
	struct twm_output *o = lock_output(server);
	lk->iv->view.alarm = lk->msg[0] || lk->cooldown_until > server_now_ms(); /* GRT-5 */
	internal_view_redraw(lk->iv);
	if (!o) {
		return;
	}
	/* the badge and the frame, centred together */
	struct wlr_box f = view_frame_box(&lk->iv->view);
	int group = LK_BADGE_H + LK_BADGE_GAP + f.height;
	int fy = o->box.y + (o->box.height - group) / 2 + LK_BADGE_H + LK_BADGE_GAP;
	view_move(&lk->iv->view, o->box.x + (o->box.width - f.width) / 2, fy);
	view_update_frame(&lk->iv->view);
	draw_badge(lk, o, fy);
	draw_clock(lk, o);
	draw_card(lk, o);
	draw_line(lk, o);
	/* the veil: every output, one step darker (TRU-4) */
	struct wlr_box all;
	wlr_output_layout_get_box(server->output_layout, NULL, &all);
	wlr_scene_rect_set_size(lk->veil, all.width, all.height);
	wlr_scene_node_set_position(&lk->veil->node, all.x, all.y);
	wlr_scene_node_set_enabled(&lk->veil->node, true);
}

/* --- the layers a lock hides (GRT-7: hidden, not dimmed) --- */

static const enum twm_layer hidden_layers[] = {
	TWM_LAYER_ROOT_TEXT, TWM_LAYER_BOTTOM, TWM_LAYER_WINDOWS, TWM_LAYER_UTILITY,
	TWM_LAYER_TOP, TWM_LAYER_FULLSCREEN, TWM_LAYER_UNMANAGED, TWM_LAYER_LAYER_OVERLAY,
	TWM_LAYER_TRUSTED_DIM, TWM_LAYER_TRUSTED, TWM_LAYER_OVERLAY, TWM_LAYER_MENU,
};

static void show_layers(struct twm_server *server, bool show) {
	for (size_t i = 0; i < sizeof(hidden_layers) / sizeof(hidden_layers[0]); i++) {
		wlr_scene_node_set_enabled(&server->layers[hidden_layers[i]]->node, show);
	}
}

/* --- the conversation --- */

static void start_auth(struct twm_lock *lk);

static void reap_spent(void *data) {
	struct twm_lock *lk = data;
	lk->reap = NULL;
	auth_cancel(lk->spent);
	lk->spent = NULL;
}

/* The conversation is over; it is freed once its callback has returned. */
static void spend(struct twm_lock *lk) {
	if (lk->spent) {
		auth_cancel(lk->spent);
	}
	lk->spent = lk->auth;
	lk->auth = NULL;
	if (!lk->reap) {
		lk->reap = wl_event_loop_add_idle(lk->server->loop, reap_spent, lk);
	}
}

static void on_auth(void *data, enum auth_event ev, const char *text, bool echo) {
	struct twm_lock *lk = data;
	switch (ev) {
	case AUTH_PROMPT: {
		/* "Password: " -> "PASSWORD" */
		char q[128];
		snprintf(q, sizeof(q), "%s", text);
		size_t n = strlen(q);
		while (n && (q[n - 1] == ' ' || q[n - 1] == ':')) {
			q[--n] = '\0';
		}
		for (char *p = q; *p; p++) {
			*p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
		}
		snprintf(lk->question, sizeof(lk->question), "%s", q);
		lk->echo = echo;
		lk->asking = true;
		lk->busy = false;
		if (lk->submit && lk->ilen > 0) {
			/* typed and sent while PAM was still on the fingerprint */
			lk->submit = false;
			lk->input[lk->ilen] = '\0';
			lk->busy = true;
			lk->asking = false;
			auth_respond(lk->auth, lk->input);
			lk->ilen = 0;
		}
		break;
	}
	case AUTH_INFO:
		snprintf(lk->info, sizeof(lk->info), "%s", text);
		break;
	case AUTH_ERROR:
		snprintf(lk->msg, sizeof(lk->msg), "pam: %s", text);
		break;
	case AUTH_OK:
		lk->busy = false;
		spend(lk);
		lock_engage(lk->server, NULL); /* NULL: unlock */
		return;
	case AUTH_FAIL:
		lk->busy = false;
		lk->asking = false;
		lk->tries++;
		if (lk->tries >= TRIES) {
			lk->cooldown_until = server_now_ms() + COOLDOWN_MS;
			lk->tries = 0;
			snprintf(lk->msg, sizeof(lk->msg), "pam: %s", text);
		} else {
			snprintf(lk->msg, sizeof(lk->msg), "pam: %s \xc2\xb7 try %d of %d", text,
				lk->tries + 1, TRIES);
		}
		spend(lk);
		if (lk->cooldown_until <= server_now_ms()) {
			start_auth(lk); /* a new conversation for the next try */
		}
		break;
	}
	redraw(lk);
}

static void start_auth(struct twm_lock *lk) {
	struct twm_server *server = lk->server;
	const char *test_pw = server->opts.test_mode ? getenv("TWM_TEST_LOCK_PASSWORD") : NULL;
	if (!test_pw && !auth_available()) {
		snprintf(lk->msg, sizeof(lk->msg), "twm was built without PAM");
		return;
	}
	lk->asking = false;
	lk->auth = auth_start(server->loop, auth_service(), lk->user, test_pw, on_auth, lk);
	if (!lk->auth) {
		snprintf(lk->msg, sizeof(lk->msg), "pam: couldn't start");
	}
}

/* --- locking and unlocking --- */

static void unlock(struct twm_lock *lk) {
	struct twm_server *server = lk->server;
	lk->active = false;
	server->locked = false;
	if (lk->auth) {
		auth_cancel(lk->auth);
		lk->auth = NULL;
	}
	for (size_t i = 0; i < sizeof(lk->input); i++) {
		((volatile char *)lk->input)[i] = 0;
	}
	lk->ilen = 0;
	internal_view_destroy(lk->iv);
	lk->iv = NULL;
	struct wlr_scene_buffer **nodes[] = {&lk->badge, &lk->clock, &lk->card, &lk->line};
	for (size_t i = 0; i < 4; i++) {
		if (*nodes[i]) {
			wlr_scene_node_destroy(&(*nodes[i])->node);
			*nodes[i] = NULL;
		}
	}
	wlr_scene_node_set_enabled(&lk->veil->node, false);
	show_layers(server, true);
	/* URG-3: what waited during the lock is urgent now (quiet desktops keep
	 * theirs until visited) */
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (v->urgent_held && (v->sticky || !(server->quiet & (1u << (v->desktop - 1))))) {
			v->urgent_held = false;
			v->urgent = true;
			view_update_frame(v);
		}
	}
	/* focus back where it was (or to a prompt that waited) */
	wl_list_for_each(v, &server->views, link) {
		if (v->id == lk->restore_focus) {
			view_focus(v);
			break;
		}
	}
	prompt_resume(server);
	input_update_pointer_focus(server, 0);
	rootline_post(server, ROOTLINE_INFO, "lock", "unlocked");
	ui_changed(server);
	lock_activity(server);
}

void lock_engage(struct twm_server *server, const char *why) {
	struct twm_lock *lk = server->lock;
	if (!lk) {
		return;
	}
	if (!why) {
		if (lk->active) {
			unlock(lk);
		}
		return;
	}
	if (lk->active) {
		return;
	}
	const char *test_pw = server->opts.test_mode ? getenv("TWM_TEST_LOCK_PASSWORD") : NULL;
	if (!test_pw && !auth_available()) {
		rootline_post(server, ROOTLINE_ERROR, "lock", "twm was built without PAM: no lock");
		return;
	}
	lk->active = true;
	server->locked = true;
	snprintf(lk->why, sizeof(lk->why), "%s", why);
	lk->at = time(NULL);
	lk->restore_focus = server->focused ? server->focused->id : 0;
	lk->msg[0] = lk->info[0] = lk->question[0] = '\0';
	lk->tries = 0;
	lk->ilen = 0;
	lk->submit = false;
	/* nothing else holds the input (TRU-4) */
	grab_cancel(server);
	menu_close_all(server);
	if (server->selecting) {
		select_cancel(server);
	}
	hints_cancel(server);
	show_layers(server, false);
	lk->iv = internal_view_create(server, "system \xc2\xb7 locked", "twm-lock", render, lk);
	lk->iv->view.utility = false;
	lk->iv->view.trusted = true;
	lk->iv->view.sticky = true;
	wlr_scene_node_reparent(&lk->iv->view.tree->node, server->layers[TWM_LAYER_LOCK]);
	lk->iv->positioned = true;
	lk->iv->view.mapped = true;
	internal_view_redraw(lk->iv);
	view_show(&lk->iv->view, false);
	view_focus(&lk->iv->view);
	wlr_seat_pointer_clear_focus(server->seat);
	wlr_seat_keyboard_notify_clear_focus(server->seat);
	start_auth(lk);
	redraw(lk);
	wl_event_source_timer_update(lk->tick, 1000);
	rootline_post(server, ROOTLINE_INFO, "lock", "session locked %s", why);
}

void lock_key(struct twm_server *server, xkb_keysym_t sym, const char *utf8, uint32_t mods,
		bool caps) {
	struct twm_lock *lk = server->lock;
	if (!lk || !lk->active) {
		return;
	}
	lk->caps = caps;
	if (lk->busy || lk->cooldown_until > server_now_ms()) {
		redraw(lk);
		return;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (lk->auth && lk->asking) {
			lk->input[lk->ilen] = '\0';
			lk->busy = true;
			lk->asking = false;
			lk->msg[0] = '\0';
			auth_respond(lk->auth, lk->input); /* wiped there */
			lk->ilen = 0;
		} else if (!lk->auth) {
			start_auth(lk);
		} else if (lk->ilen > 0) {
			lk->submit = true; /* sent when PAM asks */
		}
	} else if (sym == XKB_KEY_BackSpace) {
		while (lk->ilen > 0) {
			unsigned char c = (unsigned char)lk->input[--lk->ilen];
			lk->input[lk->ilen] = '\0';
			if ((c & 0xC0) != 0x80) {
				break; /* a whole character */
			}
		}
	} else if (sym == XKB_KEY_Escape || ((mods & TWM_MOD_CTRL) && (sym == XKB_KEY_u || sym == XKB_KEY_U))) {
		for (size_t i = 0; i < lk->ilen; i++) {
			((volatile char *)lk->input)[i] = 0;
		}
		lk->ilen = 0;
	} else if (utf8 && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f && !(mods & TWM_MOD_CTRL)) {
		size_t n = strlen(utf8);
		if (lk->ilen + n < sizeof(lk->input) - 1) {
			memcpy(lk->input + lk->ilen, utf8, n);
			lk->ilen += n;
			lk->msg[0] = '\0'; /* typing again clears the failure */
		}
	}
	redraw(lk);
}

/* --- idle (GRT-7) --- */

static void set_dark(struct twm_lock *lk, bool dark) {
	if (lk->dark == dark) {
		return;
	}
	lk->dark = dark;
	struct twm_output *o;
	wl_list_for_each(o, &lk->server->outputs, link) {
		struct wlr_output_state st;
		wlr_output_state_init(&st);
		wlr_output_state_set_enabled(&st, !dark);
		if (!wlr_output_commit_state(o->wlr_output, &st)) {
			wlr_log(WLR_ERROR, "idle: couldn't turn %s %s", o->wlr_output->name, dark ? "off" : "on");
		}
		wlr_output_state_finish(&st);
	}
}

static void arm(struct twm_lock *lk) {
	const struct twm_config *c = lk->server->config;
	bool held = lk->inhibitors > 0;
	wl_event_source_timer_update(lk->idle_lock,
		!held && c && c->idle_lock > 0 && !lk->active ? c->idle_lock * 1000 : 0);
	wl_event_source_timer_update(lk->idle_screen,
		!held && c && c->idle_screen > 0 ? c->idle_screen * 1000 : 0);
}

void lock_activity(struct twm_server *server) {
	struct twm_lock *lk = server->lock;
	if (!lk) {
		return;
	}
	if (lk->notifier) {
		wlr_idle_notifier_v1_notify_activity(lk->notifier, server->seat);
	}
	set_dark(lk, false);
	arm(lk);
}

static int idle_lock_fired(void *data) {
	struct twm_lock *lk = data;
	lock_engage(lk->server, "on idle");
	return 0;
}

static int idle_screen_fired(void *data) {
	struct twm_lock *lk = data;
	set_dark(lk, true);
	return 0;
}

static int tick(void *data) {
	struct twm_lock *lk = data;
	if (!lk->active) {
		return 0;
	}
	if (lk->cooldown_until && lk->cooldown_until <= server_now_ms()) {
		lk->cooldown_until = 0;
		lk->msg[0] = '\0';
		if (!lk->auth) {
			start_auth(lk);
		}
	}
	redraw(lk); /* the clock, the idle minutes, the countdown */
	wl_event_source_timer_update(lk->tick, 1000);
	return 0;
}

static void inhibitor_destroy(struct wl_listener *listener, void *data) {
	struct inhibitor *in = wl_container_of(listener, in, destroy);
	struct twm_lock *lk = in->lock;
	wl_list_remove(&in->destroy.link);
	free(in);
	if (--lk->inhibitors == 0 && lk->notifier) {
		wlr_idle_notifier_v1_set_inhibited(lk->notifier, false);
	}
	arm(lk);
}

static void handle_new_inhibitor(struct wl_listener *listener, void *data) {
	struct twm_lock *lk = wl_container_of(listener, lk, new_inhibitor);
	struct wlr_idle_inhibitor_v1 *inh = data;
	struct inhibitor *in = calloc(1, sizeof(*in));
	if (!in) {
		return;
	}
	in->lock = lk;
	in->destroy.notify = inhibitor_destroy;
	wl_signal_add(&inh->events.destroy, &in->destroy);
	if (lk->inhibitors++ == 0 && lk->notifier) {
		wlr_idle_notifier_v1_set_inhibited(lk->notifier, true);
	}
	arm(lk);
}

void lock_configure(struct twm_server *server) {
	struct twm_lock *lk = server->lock;
	if (!lk) {
		return;
	}
	arm(lk);
	if (lk->active) {
		redraw(lk);
	}
}

void lock_init(struct twm_server *server) {
	struct twm_lock *lk = calloc(1, sizeof(*lk));
	if (!lk) {
		return;
	}
	lk->server = server;
	server->lock = lk;
	sys_host(lk->host, sizeof(lk->host));
	sys_user(lk->user, sizeof(lk->user));
	float veil[4];
	rgba_premul(rgba_hex(server->theme.ground, 0.42f), veil); /* B11 */
	lk->veil = wlr_scene_rect_create(server->layers[TWM_LAYER_LOCK], 0, 0, veil);
	wlr_scene_node_set_enabled(&lk->veil->node, false);
	lk->tick = wl_event_loop_add_timer(server->loop, tick, lk);
	lk->idle_lock = wl_event_loop_add_timer(server->loop, idle_lock_fired, lk);
	lk->idle_screen = wl_event_loop_add_timer(server->loop, idle_screen_fired, lk);
	lk->notifier = wlr_idle_notifier_v1_create(server->display);
	lk->inhibit = wlr_idle_inhibit_v1_create(server->display);
	if (lk->inhibit) {
		lk->new_inhibitor.notify = handle_new_inhibitor;
		wl_signal_add(&lk->inhibit->events.new_inhibitor, &lk->new_inhibitor);
	} else {
		wl_list_init(&lk->new_inhibitor.link);
	}
	arm(lk);
}

void lock_finish(struct twm_server *server) {
	struct twm_lock *lk = server->lock;
	if (!lk) {
		return;
	}
	if (lk->auth) {
		auth_cancel(lk->auth);
	}
	if (lk->reap) {
		wl_event_source_remove(lk->reap);
	}
	auth_cancel(lk->spent);
	wl_list_remove(&lk->new_inhibitor.link);
	wl_event_source_remove(lk->tick);
	wl_event_source_remove(lk->idle_lock);
	wl_event_source_remove(lk->idle_screen);
	free(lk);
	server->lock = NULL;
}
