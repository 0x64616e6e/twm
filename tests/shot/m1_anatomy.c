/*
 * M1 screenshot test: frame anatomy against B0/B1, sloppy focus with intent,
 * urgency, click-to-place and the outline. Windows are placed where B0 puts
 * them, by clicking (OUT-12). Every check names the requirement it covers.
 *
 * Text widths depend on the installed fonts, so positions that follow the
 * title (hatch, resize box, tab edge) are found by scanning the image, then
 * checked against the layout rules.
 */
#include <stdio.h>
#include <stdlib.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define SURFACE 0x121310
#define GRAPHITE 0x23251F
#define HAIRLINE 0x2F312A
#define INK 0xE8E6DF
#define MUTED 0x9A9C93
#define ACCENT 0xFFB000
#define ON_ACCENT 0x0E0F0C

#define TH 26 /* TitleHeight */

static uint32_t hatch_ink, sep_focused;

static int near(uint32_t a, uint32_t b, int tol) {
	for (int s = 0; s <= 16; s += 8) {
		int d = (int)((a >> s) & 0xff) - (int)((b >> s) & 0xff);
		if (d < -tol || d > tol) {
			return 0;
		}
	}
	return 1;
}

/* The tab's right edge: first pixel on row y, scanning right from x0, that
 * is none of the tab's own colors. */
static int tab_right(const struct tc_image *img, int x0, int y,
		const uint32_t *colors, int ncolors) {
	for (int x = x0; x < img->width; x++) {
		uint32_t p = tc_pixel(img, x, y);
		int ok = 0;
		for (int i = 0; i < ncolors; i++) {
			ok |= near(p, colors[i], 2);
		}
		if (!ok) {
			return x;
		}
	}
	return img->width;
}

/* Columns on row y in [x0, x1) whose color is `c`; returns the first run of
 * at least `min_len`, or -1. */
static int find_run(const struct tc_image *img, int x0, int x1, int y, uint32_t c,
		int min_len, int *len) {
	int start = -1;
	for (int x = x0; x <= x1; x++) {
		int hit = x < x1 && near(tc_pixel(img, x, y), c, 2);
		if (hit && start < 0) {
			start = x;
		} else if (!hit && start >= 0) {
			if (x - start >= min_len) {
				*len = x - start;
				return start;
			}
			start = -1;
		}
	}
	return -1;
}

static int count_not(const struct tc_image *img, int x0, int y0, int x1, int y1, uint32_t bg) {
	int n = 0;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			n += !near(tc_pixel(img, x, y), bg, 8);
		}
	}
	return n;
}

static struct tc_window *place(struct tc *tc, int x, int y, struct tc_window_opts opts) {
	tc_pointer_move(tc, x, y);
	struct tc_window *win = tc_window_create(tc, &opts);
	tc_wait(tc, 20);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 20);
	return win;
}

static void rest(struct tc *tc, int x, int y, int ms) {
	tc_pointer_move(tc, x, y);
	tc_wait(tc, ms);
}

/* Weave-free ground pixel check: rows/cols where y%6 and x%6 avoid 2,3. */
static uint32_t root_at(int x, int y) {
	int dot = (x % 6 == 2 || x % 6 == 3) && (y % 6 == 2 || y % 6 == 3);
	return dot ? tc_blend(INK, 0.07, GROUND) : GROUND;
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "m1-anatomy.png";
	hatch_ink = tc_blend(ON_ACCENT, 0.55, ACCENT);
	sep_focused = tc_blend(ON_ACCENT, 0.30, ACCENT);
	struct tc *tc = tc_connect();

	/* B0 windows. Client size = frame - 2 px border, - 26 px tab. */
	struct tc_window *xclock = place(tc, 1000, 500, (struct tc_window_opts){
		.title = "xclock", .app_id = "xclock", .width = 230, .height = 230,
		.color = SURFACE, .fixed_size = true, .request_ssd = true});
	struct tc_window *zpool = place(tc, 990, 104, (struct tc_window_opts){
		.title = "zpool status -v tank \xc2\xb7 every 2s", .app_id = "foot",
		.width = 598, .height = 320, .color = SURFACE, .request_ssd = true});
	struct tc_window *irc = place(tc, 1300, 640, (struct tc_window_opts){
		.title = "irc \xe2\x80\x94 3 mentions", .app_id = "irc",
		.width = 300, .height = 150, .color = SURFACE});
	struct tc_window *xterm = place(tc, 80, 64, (struct tc_window_opts){
		.title = "xterm \xe2\x80\x94 ~/.twmrc", .app_id = "xterm",
		.width = 858, .height = 532, .color = SURFACE, .request_ssd = true});

	/* FOC-9: the placing click gives focus. */
	tc_expect("FOC-9", tc_window_activated(xterm) && !tc_window_activated(zpool),
		"the window placed last has focus");

	/* FOC-2 (B7): crossing zpool in ~30 ms leaves focus on xterm. */
	rest(tc, 400, 300, 120);
	tc_pointer_move(tc, 1200, 300);
	tc_wait(tc, 25);
	rest(tc, 400, 300, 150);
	tc_expect("FOC-2", tc_window_activated(xterm) && !tc_window_activated(zpool),
		"a fast crossing doesn't move focus");

	/* FOC-2: resting 80 ms on zpool moves focus there. */
	rest(tc, 1200, 300, 150);
	tc_expect("FOC-2", tc_window_activated(zpool) && !tc_window_activated(xterm),
		"resting on a window focuses it");
	struct tc_image zf = {0};
	tc_screenshot(tc, &zf);

	/* FOC-1: the root doesn't take focus. */
	rest(tc, 400, 300, 150);
	rest(tc, 700, 800, 150);
	tc_expect("FOC-1", tc_window_activated(xterm),
		"pointer on the root: focus stays on xterm");

	/* URG-2: a token-less activation request marks irc urgent; focus stays. */
	tc_window_request_activation(irc);
	tc_wait(tc, 50);
	tc_expect("URG-2", tc_window_activated(xterm) && !tc_window_activated(irc),
		"an activation request doesn't steal focus");

	/* OUT-12: a fifth window waits for placement; its outline follows the
	 * pointer (B0: "notes" at 560x300+380+690). */
	tc_pointer_move(tc, 380, 690);
	struct tc_window *notes = tc_window_create(tc, &(struct tc_window_opts){
		.title = "notes", .app_id = "notes", .width = 558, .height = 272,
		.color = SURFACE});
	tc_wait(tc, 60);

	struct tc_image img = {0};
	if (!tc_expect("D-7", tc_screenshot(tc, &img), "screencopy")) {
		return tc_report();
	}
	tc_expect("D-7", tc_image_write_png(&img, out), "wrote %s", out);

	/* --- xterm, focused: FRM-2, FRM-3, FRM-7, FRM-10 --- */
	int fx = 80, fy = 64;
	tc_expect_pixel("FRM-7", &img, fx + 2, fy + 2, ACCENT, 2);
	tc_expect_pixel("FRM-3", &img, fx + 7, fy + 7, ON_ACCENT, 2);     /* iconify outline */
	tc_expect_pixel("FRM-3", &img, fx + 9, fy + 9, ACCENT, 2);        /* inside it */
	tc_expect_pixel("FRM-3", &img, fx + 12, fy + 12, ON_ACCENT, 2);   /* filled centre */
	tc_expect_pixel("FRM-3", &img, fx + 25, fy + 2, ACCENT, 2);
	tc_expect_pixel("FRM-7", &img, fx + 26, fy + 2, sep_focused, 2);  /* separator */
	tc_expect_pixel("FRM-3", &img, fx + 27, fy + 2, ACCENT, 2);
	/* Row 1 holds only background and separators (glyphs and text start
	 * lower). ON_ACCENT is too close to GROUND to tell apart, so it's left
	 * out of the scan. */
	uint32_t xterm_colors[] = {ACCENT, sep_focused};
	int xr = tab_right(&img, fx, fy + 1, xterm_colors, 2);
	int hlen = 0;
	int hx = find_run(&img, fx + 41, xr, fy + 6, hatch_ink, 40, &hlen);
	tc_expect("FRM-10", hx > 0, "focused tab has a hatch (found at x=%d)", hx);
	tc_expect("FRM-3", hlen == 72, "hatch is 72 px wide (%d)", hlen);
	for (int k = 0; k < 5 && hx > 0; k++) {
		tc_expect_pixel("FRM-10", &img, hx + 30, fy + 6 + 3 * k, hatch_ink, 2);
		tc_expect_pixel("FRM-10", &img, hx + 30, fy + 7 + 3 * k, ACCENT, 2);
	}
	tc_expect_pixel("FRM-10", &img, hx + 30, fy + 5, ACCENT, 2);
	tc_expect_pixel("FRM-10", &img, hx + 30, fy + 20, ACCENT, 2);
	tc_expect("FRM-3", hx > 0 && xr - 27 == hx + hlen + 8,
		"8 px gap, separator, 26 px resize box after the hatch (tab ends at %d)", xr);
	tc_expect_pixel("FRM-7", &img, xr - 27, fy + 2, sep_focused, 2);
	tc_expect_pixel("FRM-3", &img, xr - 19, fy + 7, ON_ACCENT, 2);    /* resize: outer */
	tc_expect_pixel("FRM-3", &img, xr - 12, fy + 10, ON_ACCENT, 2);   /* 8 px square */
	tc_expect_pixel("FRM-3", &img, xr - 16, fy + 9, ON_ACCENT, 2);    /* 4 px square */
	tc_expect_pixel("FRM-3", &img, xr - 9, fy + 15, ACCENT, 2);       /* outside 8 px */
	tc_expect("FRM-3", count_not(&img, fx + 41, fy + 4, hx - 14, fy + 22, ACCENT) > 20,
		"title text is drawn");
	tc_expect("FRM-4", xr < fx + 860, "tab is squeezed (%d of 860 px)", xr - fx);
	tc_expect_pixel("FRM-2", &img, xr - 1, fy, ACCENT, 2);
	tc_expect_pixel("FRM-4", &img, xr + 2, fy + 12, root_at(xr + 2, fy + 12), 2);
	tc_expect_pixel("FRM-2", &img, xr + 4, fy + 25, root_at(xr + 4, fy + 25), 2);
	tc_expect_pixel("FRM-7", &img, xr + 4, fy + 26, ACCENT, 2);       /* border top */
	tc_expect_pixel("FRM-7", &img, fx, fy + 300, ACCENT, 2);
	tc_expect_pixel("FRM-2", &img, fx + 1, fy + 300, SURFACE, 0);
	tc_expect_pixel("FRM-2", &img, fx + 858, fy + 300, SURFACE, 0);
	tc_expect_pixel("FRM-7", &img, fx + 859, fy + 300, ACCENT, 2);
	tc_expect_pixel("FRM-7", &img, fx + 300, fy + 26 + 533, ACCENT, 2);
	tc_expect_pixel("FRM-2", &img, fx + 300, fy + 26 + 534, root_at(fx + 300, fy + 560), 2);

	/* --- zpool, unfocused: FRM-8, FRM-11 --- */
	fx = 990;
	fy = 104;
	tc_expect_pixel("FRM-8", &img, fx + 2, fy + 2, GRAPHITE, 2);
	tc_expect_pixel("FRM-8", &img, fx + 7, fy + 7, MUTED, 2);
	tc_expect_pixel("FRM-8", &img, fx + 26, fy + 2, HAIRLINE, 2);
	uint32_t zpool_colors[] = {GRAPHITE, HAIRLINE, MUTED};
	int zr = tab_right(&img, fx, fy + 1, zpool_colors, 3);
	int dummy;
	tc_expect("FRM-8", find_run(&img, fx + 41, zr, fy + 6, hatch_ink, 8, &dummy) < 0,
		"no hatch without focus");
	tc_expect_pixel("FRM-8", &img, zr - 27, fy + 2, HAIRLINE, 2);
	tc_expect_pixel("FRM-8", &img, zr - 19, fy + 7, MUTED, 2);
	tc_expect_pixel("FRM-8", &img, fx, fy + 150, HAIRLINE, 2);
	tc_expect_pixel("FRM-8", &img, fx + 599, fy + 150, HAIRLINE, 2);
	uint32_t zf_colors[] = {ACCENT, sep_focused};
	int zfr = tab_right(&zf, fx, fy + 1, zf_colors, 2);
	tc_expect("FRM-11", zfr - zr == 80,
		"focused tab is 80 px wider (%d vs %d), left edge fixed", zfr - fx, zr - fx);
	tc_expect_pixel("FRM-11", &zf, fx + 2, fy + 2, ACCENT, 2);
	tc_image_free(&zf);

	/* --- xclock, fixed size: FRM-6 --- */
	fx = 1000;
	fy = 500;
	int cr = tab_right(&img, fx, fy + 1, zpool_colors, 3);
	tc_expect_pixel("FRM-6", &img, cr - 19, fy + 7, GRAPHITE, 2);
	tc_expect_pixel("FRM-6", &img, cr - 27, fy + 2, GRAPHITE, 2);
	tc_expect("FRM-6", count_not(&img, cr - 14, fy + 2, cr, fy + 24, GRAPHITE) == 0,
		"tab ends in title padding, no resize box (tab %d px)", cr - fx);

	/* --- irc, urgent: FRM-9 --- */
	fx = 1300;
	fy = 640;
	tc_expect_pixel("FRM-9", &img, fx + 3, fy + 3, GRAPHITE, 2);
	tc_expect_pixel("FRM-9", &img, fx + 10, fy, ACCENT, 2);           /* inset outline */
	tc_expect_pixel("FRM-9", &img, fx, fy + 13, ACCENT, 2);
	tc_expect_pixel("FRM-9", &img, fx + 7, fy + 7, ACCENT, 2);        /* glyph in accent */
	tc_expect_pixel("FRM-9", &img, fx + 26, fy + 13, ACCENT, 2);      /* separator */
	int dashes = 0, gaps = 0;
	for (int x = 0; x < 48; x++) {
		int on = near(tc_pixel(&img, fx + x, fy + 26), ACCENT, 2);
		int want = x % 6 < 3;
		dashes += on && want;
		gaps += !on && !want;
	}
	tc_expect("FRM-9", dashes == 24 && gaps == 24,
		"border is dashed 3 on / 3 off (%d/24 dashes, %d/24 gaps)", dashes, gaps);
	tc_expect_pixel("FRM-9", &img, fx + 302 - 1, fy + 26 + 1, ACCENT, 2); /* right edge, p=302 */

	/* --- the placement outline: OUT-2, OUT-3, OUT-4, OUT-12 --- */
	int ox = 380, oy = 690, ow = 560, oh = 300;
	uint32_t edge = tc_blend(INK, 0.85, GROUND);
	uint32_t third = tc_blend(INK, 0.35, GROUND);
	tc_expect_pixel("OUT-2", &img, ox + 102, oy, edge, 3);
	tc_expect_pixel("OUT-2", &img, ox, oy + 150, edge, 3);
	tc_expect_pixel("OUT-2", &img, ox + ow - 1, oy + 150, edge, 3);
	tc_expect_pixel("OUT-2", &img, ox + 102, oy + oh - 1, edge, 3);
	tc_expect_pixel("OUT-2", &img, ox + 104, oy + TH, edge, 3);       /* title line */
	tc_expect_pixel("OUT-2", &img, ox + ow / 3, oy + 60, third, 3);   /* thirds */
	tc_expect_pixel("OUT-2", &img, ox + ow * 2 / 3, oy + 60, third, 3);
	/* x = 414: off the weave's dot columns. */
	tc_expect_pixel("OUT-2", &img, ox + 34, oy + oh / 3, third, 3);
	tc_expect_pixel("OUT-2", &img, ox + 34, oy + oh * 2 / 3, third, 3);
	tc_expect("OUT-2", count_not(&img, ox + 10, oy + 4, ox + 60, oy + 22, GROUND) > 20,
		"window name in the title band");
	/* Readout: ground box with a hairline border, centred, accent text. */
	int cx = ox + ow / 2, cy = oy + oh / 2;
	int left = cx;
	while (left > ox && !near(tc_pixel(&img, left, cy), HAIRLINE, 2)) {
		left--;
	}
	int right = cx;
	while (right < ox + ow && !near(tc_pixel(&img, right, cy), HAIRLINE, 2)) {
		right++;
	}
	tc_expect("OUT-3", left > ox && right < ox + ow && abs((left + right) / 2 - cx) <= 1,
		"readout box is centred (%d..%d)", left, right);
	int accent_px = 0;
	for (int y = cy - 30; y < cy + 30; y++) {
		for (int x = left; x < right; x++) {
			accent_px += near(tc_pixel(&img, x, y), ACCENT, 40);
		}
	}
	tc_expect("OUT-4", accent_px > 50, "geometry in accent (%d px)", accent_px);
	tc_expect_pixel("OUT-3", &img, left + 3, cy - 14, GROUND, 0);
	tc_expect("OUT-12", !tc_window_activated(notes) && tc_window_activated(xterm),
		"the window being placed isn't shown or focused yet");

	/* --- the root: ROOT-1, ROOT-2, ROOT-3 --- */
	tc_expect("ROOT-3", count_not(&img, 1000, 1046, 1400, 1062, GROUND) > 100,
		"status line under the wordmark");
	tc_expect("ROOT-2", count_not(&img, 980, 890, 1500, 1040, GROUND) > 500,
		"wordmark on the root");
	tc_image_free(&img);

	/* Place notes; it takes focus (FOC-9). */
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 50);
	tc_expect("FOC-9", tc_window_activated(notes), "placed window takes focus");

	/* --- OUT-1, OUT-6, OUT-8: B2 move on zpool's title, mid-drag and after --- */
	fx = 990;
	fy = 104;
	tc_pointer_move(tc, fx + 100, fy + 13);
	tc_pointer_button(tc, TC_BTN_MIDDLE, true);
	for (int i = 1; i <= 4; i++) {
		tc_pointer_move(tc, fx + 100 + 10 * i, fy + 13 + 10 * i);
	}
	tc_screenshot(tc, &img);
	tc_expect_pixel("OUT-1", &img, fx + 2, fy + 2, GRAPHITE, 2);      /* window still put */
	tc_expect_pixel("OUT-1", &img, fx + 40 + 300, fy + 40, edge, 40); /* outline moved */
	tc_image_free(&img);
	tc_pointer_button(tc, TC_BTN_MIDDLE, false);
	tc_wait(tc, 50);
	tc_screenshot(tc, &img);
	tc_expect_pixel("OUT-6", &img, fx + 40 + 2, fy + 40 + 2, GRAPHITE, 2);
	tc_expect_pixel("OUT-6", &img, fx + 40 + 300, fy + 40 + 26, HAIRLINE, 2);
	tc_image_free(&img);
	fx += 40;
	fy += 40;

	/* --- OUT-9: drag zpool's resize box; the client gets one configure --- */
	tc_screenshot(tc, &img);
	zr = tab_right(&img, fx, fy + 1, zpool_colors, 3);
	tc_image_free(&img);
	tc_pointer_move(tc, zr - 13, fy + 13);
	tc_pointer_button(tc, TC_BTN_LEFT, true);
	for (int i = 1; i <= 5; i++) {
		tc_pointer_move(tc, zr - 13 + 10 * i, fy + 13 + 4 * i);
	}
	int w0, h0;
	tc_window_size(zpool, &w0, &h0);
	tc_expect("OUT-1", w0 == 598 && h0 == 320, "no resize until release (%dx%d)", w0, h0);
	tc_pointer_button(tc, TC_BTN_LEFT, false);
	tc_wait(tc, 80);
	int w1, h1;
	tc_window_size(zpool, &w1, &h1);
	tc_expect("OUT-9", w1 == 648 && h1 == 340, "resized to 648x340 on release (%dx%d)", w1, h1);
	tc_screenshot(tc, &img);
	/* B1 on the resize box focused zpool, so its border is accent. */
	tc_expect_pixel("OUT-9", &img, fx + 649, fy + 200, ACCENT, 2);
	tc_expect_pixel("OUT-9", &img, fx + 648, fy + 200, SURFACE, 0);
	tc_image_free(&img);

	tc_window_destroy(notes);
	tc_window_destroy(xterm);
	tc_window_destroy(irc);
	tc_window_destroy(zpool);
	tc_window_destroy(xclock);
	tc_disconnect(tc);
	return tc_report();
}
