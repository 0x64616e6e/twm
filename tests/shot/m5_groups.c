/*
 * M5 screenshot test: tab groups (GRP-1 … GRP-8) and hint mode
 * (HNT-1 … HNT-5), through real pointer and keyboard input.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define SURFACE 0x121310
#define GRAPHITE 0x23251F
#define DIM 0x85887C
#define ACCENT 0xFFB000
#define ON_ACCENT 0x0E0F0C
#define TH 26
#define KEY_S 31
#define KEY_Q 16

static int near(uint32_t a, uint32_t b, int tol) {
	for (int s = 0; s <= 16; s += 8) {
		int d = (int)((a >> s) & 0xff) - (int)((b >> s) & 0xff);
		if (d < -tol || d > tol) {
			return 0;
		}
	}
	return 1;
}

static int count_box(const struct tc_image *img, int x0, int y0, int x1, int y1,
		uint32_t c, int tol) {
	int n = 0;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			n += near(tc_pixel(img, x, y), c, tol);
		}
	}
	return n;
}

/* Right edge of a tab row: the first root-colored pixel on its top row. */
static int row_end(const struct tc_image *img, int x0, int y) {
	for (int x = x0; x < img->width; x++) {
		if (near(tc_pixel(img, x, y), GROUND, 10)) {
			return x;
		}
	}
	return img->width;
}

/* The longest run of hatch-ink on a row (the active tab's hatch). */
static int hatch_run(const struct tc_image *img, int x0, int x1, int y) {
	uint32_t hatch = tc_blend(ON_ACCENT, 0.55, ACCENT);
	int best = 0, run = 0;
	for (int x = x0; x < x1; x++) {
		run = near(tc_pixel(img, x, y), hatch, 2) ? run + 1 : 0;
		best = run > best ? run : best;
	}
	return best;
}

static struct tc_window *place(struct tc *tc, int x, int y, struct tc_window_opts opts) {
	tc_pointer_move(tc, x, y);
	struct tc_window *win = tc_window_create(tc, &opts);
	tc_wait(tc, 20);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 40);
	return win;
}

static void shot(struct tc *tc, struct tc_image *img) {
	tc_image_free(img);
	tc_screenshot(tc, img);
}

static void drag(struct tc *tc, uint32_t button, int x0, int y0, int x1, int y1, bool release) {
	tc_pointer_move(tc, x0, y0);
	tc_pointer_button(tc, button, true);
	for (int i = 1; i <= 6; i++) {
		tc_pointer_move(tc, x0 + (x1 - x0) * i / 6, y0 + (y1 - y0) * i / 6);
	}
	if (release) {
		tc_pointer_button(tc, button, false);
		tc_wait(tc, 50);
	}
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "m5-groups.png";
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	char path[4096];

	/* Exact positions matter here, so no snapping. */
	char rc[4096];
	snprintf(rc, sizeof(rc), "%s/.twmrc", getenv("TWM_TEST_HOME"));
	FILE *f = fopen(rc, "w");
	fputs("Snap off\n", f);
	fclose(f);
	tc_wait(tc, 400);

	struct tc_window *a = place(tc, 100, 150, (struct tc_window_opts){
		.title = "foot \xe2\x80\x94 build", .app_id = "foot", .width = 818, .height = 438,
		.color = SURFACE});
	struct tc_window *b = place(tc, 1000, 650, (struct tc_window_opts){
		.title = "foot \xe2\x80\x94 logs", .app_id = "foot", .width = 498, .height = 272,
		.color = SURFACE});
	struct tc_window *c = place(tc, 1100, 200, (struct tc_window_opts){
		.title = "man twm(1)", .app_id = "man", .width = 398, .height = 200, .color = SURFACE});

	/* --- GRP-1, GRP-2: drag man(1) onto foot's top edge --- */
	drag(tc, TC_BTN_MIDDLE, 1180, 213, 500, 160, false);
	shot(tc, &img);
	tc_image_write_png(&img, out);
	tc_expect("GRP-2", count_box(&img, 100, 150, 900, 152, ACCENT, 2) > 20,
		"a dashed slot opens in the target's row");
	tc_expect("GRP-2", count_box(&img, 380, 145, 520, 149, ACCENT, 2) > 20,
		"a floating tab with an accent edge under the pointer");
	tc_expect("GRP-2", count_box(&img, 540, 194, 720, 210, ACCENT, 60) > 30,
		"DROP \xe2\x86\x92 GROUP OF 2");
	tc_expect_pixel("GRP-2", &img, 1102, 202, ACCENT, 2); /* man(1) still put (focused) */
	tc_pointer_button(tc, TC_BTN_MIDDLE, false);
	tc_wait(tc, 60);
	int w, h;
	tc_window_size(c, &w, &h);
	tc_expect("GRP-3", w == 818 && h == 438, "the joining window takes the group's size (%dx%d)", w, h);
	tc_expect("GRP-1", tc_window_activated(c), "the dropped tab is active and focused");
	shot(tc, &img);
	tc_expect_pixel("GRP-1", &img, 1102, 202, GROUND, 8);   /* gone from its old place */
	tc_expect_pixel("GRP-3", &img, 101, 151, GRAPHITE, 2);  /* iconify box like tab 1 */
	int hr = hatch_run(&img, 100, 900, 150 + 6);
	tc_expect("GRP-3", hr == 38, "the active tab carries a 38 px hatch (%d)", hr);
	int two_tabs = row_end(&img, 100, 150);

	/* GRP-2: and a third, dropped at the end of the row. */
	drag(tc, TC_BTN_MIDDLE, 1100, 663, two_tabs + 60, 160, false);
	shot(tc, &img);
	tc_expect("GRP-2", count_box(&img, two_tabs + 100, 194, two_tabs + 280, 210, ACCENT, 60) > 30,
		"DROP \xe2\x86\x92 GROUP OF 3");
	tc_pointer_button(tc, TC_BTN_MIDDLE, false);
	tc_wait(tc, 60);
	tc_window_size(b, &w, &h);
	tc_expect("GRP-3", w == 818 && h == 438 && tc_window_activated(b), "logs joined, active");
	shot(tc, &img);
	int three_tabs = row_end(&img, 100, 150);
	tc_expect("GRP-3", three_tabs > two_tabs, "three tabs side by side (%d > %d)", three_tabs, two_tabs);
	snprintf(path, sizeof(path), "%.*s-row.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);

	/* --- GRP-4: B1 on the first tab activates it; M-Tab cycles --- */
	tc_pointer_move(tc, 100 + 27 + 20, 163);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 40);
	tc_expect("GRP-4", tc_window_activated(a) && !tc_window_activated(b),
		"B1 on build's tab: build in front");
	tc_pointer_move(tc, 1500, 120); /* the root */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_TAB);
	tc_wait(tc, 40);
	tc_expect("GRP-4", tc_window_activated(c), "M-Tab: the next tab (man) in front");

	/* --- GRP-9: B3 on the row squeezes the whole group, as on one window --- */
	tc_pointer_move(tc, 100 + 27 + 20, 163);
	tc_pointer_click(tc, TC_BTN_RIGHT);
	tc_wait(tc, 40);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-squeezed.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect("GRP-9", !near(tc_pixel(&img, 500, 400), SURFACE, 0) &&
		row_end(&img, 100, 150) == three_tabs, "squeezed: the body gone, every tab still in the row");
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_TAB);           /* another tab to the front */
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect("GRP-9", !near(tc_pixel(&img, 500, 400), SURFACE, 0), "still squeezed with another tab in front");
	tc_pointer_click(tc, TC_BTN_RIGHT);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect_pixel("GRP-9", &img, 500, 400, SURFACE, 0);   /* B3 again: the body back */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_TAB);           /* round to man in front again */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_TAB);
	tc_wait(tc, 40);

	/* --- GRP-6: M-B3 folds the row into the active tab; again fans out --- */
	tc_key(tc, TC_KEY_LEFTMETA, true);
	tc_pointer_click(tc, TC_BTN_RIGHT);
	tc_key(tc, TC_KEY_LEFTMETA, false);
	tc_wait(tc, 40);
	shot(tc, &img);
	int folded = row_end(&img, 100, 150);
	tc_expect("GRP-6", folded < three_tabs - 100, "folded: one tab (%d < %d)", folded, three_tabs);
	tc_expect_pixel("GRP-6", &img, 500, 400, SURFACE, 0);   /* the body stays */
	tc_key(tc, TC_KEY_LEFTMETA, true);
	tc_pointer_click(tc, TC_BTN_RIGHT);
	tc_key(tc, TC_KEY_LEFTMETA, false);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect("GRP-6", row_end(&img, 100, 150) == three_tabs, "fanned out again");

	/* --- GRP-7: M-B1 moves the whole group --- */
	tc_key(tc, TC_KEY_LEFTMETA, true);
	drag(tc, TC_BTN_LEFT, 500, 400, 550, 440, true);
	tc_key(tc, TC_KEY_LEFTMETA, false);
	tc_pointer_move(tc, 150 + 27 + 20, 203);
	tc_pointer_click(tc, TC_BTN_LEFT);                       /* build in front */
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect("GRP-7", !near(tc_pixel(&img, 150, 190), GROUND, 8) &&
		near(tc_pixel(&img, 120, 160), GROUND, 30) && tc_window_activated(a),
		"another tab shows the group at its new place (150,190)");

	/* IMG-3: one Icon Manager row for the group, "foot ×3" / tabs. */
	tc_expect("GRP-7", count_box(&img, 1661 + 170, 106, 1661 + 222, 136, DIM, 40) > 15,
		"the Icon Manager row says tabs");

	/* --- GRP-5: B2 drags build's tab off; released, it stands alone --- */
	drag(tc, TC_BTN_MIDDLE, 150 + 27 + 20, 203, 600, 750, true);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect("GRP-5", !near(tc_pixel(&img, 600, 750), GROUND, 8) &&
		near(tc_pixel(&img, 599, 750), GROUND, 30), "build placed at its outline (600,750)");
	tc_expect("GRP-5", tc_window_activated(a), "and focused");
	/* GRP-8: undo puts it back in the group. */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_Z);
	tc_wait(tc, 60);
	shot(tc, &img);
	tc_expect("GRP-8", near(tc_pixel(&img, 600, 750), GROUND, 30) &&
		row_end(&img, 150, 190) == three_tabs + 50, "undo: build back in the group");

	/* --- HNT-1 … HNT-5: hint mode --- */
	struct tc_window *d = place(tc, 1300, 650, (struct tc_window_opts){
		.title = "notes", .app_id = "notes", .width = 300, .height = 200, .color = SURFACE});
	tc_pointer_move(tc, 1350, 760); /* inside notes: it's nearest, so 'a' */
	tc_wait(tc, 150);
	tc_chord(tc, 2, TC_KEY_LEFTMETA, 39 /* KEY_SEMICOLON */);
	tc_wait(tc, 40);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-hints.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect_pixel("HNT-1", &img, 1302, 652, ACCENT, 2);   /* notes: letter box */
	tc_expect("HNT-1", count_box(&img, 1305, 653, 1321, 673, ON_ACCENT, 30) > 10,
		"its letter on accent");
	tc_expect_pixel("HNT-1", &img, 152, 192, ACCENT, 2);    /* the group: letter box */
	tc_expect_pixel("HNT-1", &img, 152 + 40, 192, GRAPHITE, 2); /* the rest of its tab unchanged */
	/* HNT-5: keys don't reach the client meanwhile. 's' goes to the group. */
	tc_chord(tc, 1, KEY_S);
	tc_wait(tc, 150);
	tc_expect("HNT-3", !tc_window_activated(d) &&
		(tc_window_activated(a) || tc_window_activated(b) || tc_window_activated(c)),
		"'s': the group's window raised and focused");
	shot(tc, &img);
	tc_expect_pixel("HNT-3", &img, 1302, 652, GRAPHITE, 2);  /* letters gone */
	/* HNT-4: Esc, and a key that isn't a hint, leave with nothing changed. */
	tc_pointer_move(tc, 1350, 760);
	tc_wait(tc, 150);
	tc_chord(tc, 2, TC_KEY_LEFTMETA, 39);
	tc_chord(tc, 1, TC_KEY_ESC);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect("HNT-4", near(tc_pixel(&img, 1302, 652), ACCENT, 2) && tc_window_activated(d),
		"Esc: no letters, focus unchanged");
	tc_expect_pixel("HNT-4", &img, 152, 192, GRAPHITE, 2);   /* the group: no letter */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, 39);
	tc_chord(tc, 1, KEY_Q);
	tc_wait(tc, 80);
	tc_expect("HNT-4", tc_window_activated(d), "a non-hint key: focus unchanged");
	shot(tc, &img);
	tc_expect_pixel("HNT-4", &img, 152, 192, GRAPHITE, 2);   /* no letters left */

	/* --- OUT-17: an app that sizes itself (foot, when its font changes) --- */
	/* notes was resized by twm first (M-B3), so twm has a size on record. */
	int w0, h0;
	tc_window_size(d, &w0, &h0);
	tc_key(tc, TC_KEY_LEFTMETA, true);
	drag(tc, TC_BTN_RIGHT, 1560, 830, 1610, 870, true);
	tc_key(tc, TC_KEY_LEFTMETA, false);
	tc_wait(tc, 60);
	int w1, h1;
	tc_window_size(d, &w1, &h1);
	tc_window_resize_self(d, w1 + 70, h1 + 50);
	tc_wait(tc, 80);
	/* Focus elsewhere and back: each change sends it a configure. */
	tc_pointer_move(tc, 150 + 27 + 20, 203);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
	tc_pointer_move(tc, 1350, 760);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 80);
	int w2, h2;
	tc_window_size(d, &w2, &h2);
	shot(tc, &img);
	tc_expect("OUT-17", (w1 != w0 || h1 != h0) && w2 == w1 + 70 && h2 == h1 + 50 &&
		tc_window_activated(d), "alone, it keeps the size it chose (%dx%d -> %dx%d)",
		w1, h1, w2, h2);
	tc_expect("OUT-17", !near(tc_pixel(&img, 1300 + w2 + 1, 650 + TH + h2 / 2), GROUND, 8),
		"and its frame follows");

	/* In a group, only a resize changes the size: a tab can't grow it alone. */
	tc_pointer_move(tc, 150 + 27 + 20, 203);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
	struct tc_window *g = tc_window_activated(a) ? a : tc_window_activated(b) ? b : c;
	tc_window_resize_self(g, 900, 500);
	tc_wait(tc, 120);
	int gw, gh, ow, oh;
	tc_window_size(g, &gw, &gh);
	tc_window_size(g == a ? b : a, &ow, &oh);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-zoom.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect("OUT-17", gw == 818 && gh == 438 && ow == 818 && oh == 438,
		"a grouped tab growing alone is held to the group's size (%dx%d, other %dx%d)",
		gw, gh, ow, oh);
	tc_expect("OUT-17", near(tc_pixel(&img, 150 + 818 + 20, 190 + TH + 200), GROUND, 30),
		"the group's frame doesn't grow");
	/* GRP-7: resizing the group still resizes every tab. */
	tc_key(tc, TC_KEY_LEFTMETA, true);
	drag(tc, TC_BTN_RIGHT, 900, 580, 960, 620, true);
	tc_key(tc, TC_KEY_LEFTMETA, false);
	tc_wait(tc, 80);
	tc_window_size(g, &gw, &gh);
	tc_window_size(g == a ? b : a, &ow, &oh);
	tc_expect("GRP-7", gw > 818 && gw == ow && gh == oh,
		"M-B3 on the group: every tab takes the new size (%dx%d, other %dx%d)", gw, gh, ow, oh);

	/* --- FRM-2, P-2: a client that draws its own shadow (GTK, Chromium) --- */
	/* Its window geometry (300x150) fills the frame exactly; the 24 px
	 * magenta shadow around it is never drawn. */
	struct tc_window *sh = place(tc, 300, 820, (struct tc_window_opts){
		.title = "shadowed", .app_id = "shadowed", .width = 300, .height = 150,
		.color = SURFACE, .shadow = 24});
	tc_wait(tc, 60);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-shadow.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect_pixel("FRM-2", &img, 300, 820 + TH + 75, ACCENT, 2);          /* left border */
	tc_expect_pixel("FRM-2", &img, 301, 820 + TH + 1, SURFACE, 0);          /* content's corner */
	tc_expect_pixel("FRM-2", &img, 300 + 300, 820 + TH + 150, SURFACE, 0);  /* and the other */
	tc_expect_pixel("FRM-2", &img, 300 + 301, 820 + TH + 75, ACCENT, 2);    /* right border */
	tc_expect("P-2", count_box(&img, 250, 770, 650, 1060, 0xFF00FF, 30) == 0,
		"no client shadow drawn (%d magenta px)", count_box(&img, 250, 770, 650, 1060, 0xFF00FF, 30));
	tc_window_destroy(sh);

	/* --- INP-3 … INP-5: a touchscreen (twm's own, --test) --- */
	struct tc_window *tw = place(tc, 1250, 220, (struct tc_window_opts){
		.title = "touch", .app_id = "touch", .width = 300, .height = 200, .color = SURFACE});
	char reply[64];
	tc_twm_request("touch up 99", reply, sizeof(reply)); /* the device appears */
	tc_wait(tc, 60);
	tc_roundtrip(tc);
	tc_expect("INP-4", strcmp(reply, "touch ok\n") == 0, "a touchscreen on the seat (%s)", reply);
	/* INP-4: on an app that takes touch, touch goes to it, surface-local. */
	tc_twm_request("touch down 1 1350 320", NULL, 0);
	tc_twm_request("touch move 1 1360 330", NULL, 0);
	tc_twm_request("touch up 1", NULL, 0);
	tc_wait(tc, 60);
	struct tc_touch t;
	tc_touch_state(tc, &t);
	int cx = 1250 + 1, cy = 220 + TH + 1; /* the client's corner on screen */
	tc_expect("INP-4", t.downs == 1 && t.ups == 1 && t.motions == 1 && tc_touched_window(tc, tw),
		"down, motion, up reached the window (%d %d %d)", t.downs, t.motions, t.ups);
	tc_expect("INP-4", (int)lround(t.x) == 1360 - cx && (int)lround(t.y) == 330 - cy,
		"at the finger, surface-local (%.0f,%.0f, want %d,%d)", t.x, t.y, 1360 - cx, 330 - cy);
	tc_expect("FOC-5", tc_window_activated(tw), "the touch focused it");
	/* INP-5: elsewhere a finger is the pointer: dragging the title moves it. */
	tc_twm_request("touch down 2 1297 233", NULL, 0);
	for (int i = 1; i <= 5; i++) {
		char m[64];
		snprintf(m, sizeof(m), "touch move 2 %d %d", 1297 + 20 * i, 233 + 10 * i);
		tc_twm_request(m, NULL, 0);
		tc_wait(tc, 10);
	}
	tc_twm_request("touch up 2", NULL, 0);
	tc_wait(tc, 80);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-touch.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_touch_state(tc, &t);
	tc_expect("INP-5", near(tc_pixel(&img, 1250 + 100, 220 + TH + 50 + 100), ACCENT, 2) &&
		!near(tc_pixel(&img, 1250, 220 + TH + 100), ACCENT, 2),
		"a finger on the title dragged the window by (100,50)");
	tc_expect("INP-5", t.downs == 1, "and the app got no touch for it (%d downs)", t.downs);
	tc_window_destroy(tw);
	tc_image_free(&img);

	tc_window_destroy(d);
	tc_window_destroy(c);
	tc_window_destroy(b);
	tc_window_destroy(a);
	tc_disconnect(tc);
	return tc_report();
}
