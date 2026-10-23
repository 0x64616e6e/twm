/*
 * M3 screenshot test: outlines that think. Snapping with guides (SNP-*),
 * M held = free, B3 fit (OUT-13), the keyboard paths (OUT-6, OUT-7, OUT-8),
 * undo and redo per window (HIS-1, HIS-2) and the history view (HIS-3).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define SURFACE 0x121310
#define GRAPHITE 0x23251F
#define HAIRLINE 0x2F312A
#define GUIDE_FAINT 0x2A2C26
#define MUTED 0x9A9C93
#define ACCENT 0xFFB000
#define TH 26

static int near(uint32_t a, uint32_t b, int tol) {
	for (int s = 0; s <= 16; s += 8) {
		int d = (int)((a >> s) & 0xff) - (int)((b >> s) & 0xff);
		if (d < -tol || d > tol) {
			return 0;
		}
	}
	return 1;
}

static int count_col(const struct tc_image *img, int x, int y0, int y1, uint32_t c) {
	int n = 0;
	for (int y = y0; y < y1; y++) {
		n += near(tc_pixel(img, x, y), c, 2);
	}
	return n;
}

static int count_row(const struct tc_image *img, int y, int x0, int x1, uint32_t c) {
	int n = 0;
	for (int x = x0; x < x1; x++) {
		n += near(tc_pixel(img, x, y), c, 2);
	}
	return n;
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

static struct tc_window *place(struct tc *tc, int x, int y, struct tc_window_opts opts) {
	tc_pointer_move(tc, x, y);
	struct tc_window *win = tc_window_create(tc, &opts);
	tc_wait(tc, 20);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 30);
	return win;
}

/* Is xterm's (focused, accent) frame exactly at (x, y)? Its tab's corner
 * pixel is accent and the pixels just outside it are not. */
static void expect_at(struct tc *tc, const char *req, int x, int y, const char *what) {
	struct tc_image img = {0};
	tc_screenshot(tc, &img);
	int ok = near(tc_pixel(&img, x, y), ACCENT, 2) &&
		!near(tc_pixel(&img, x - 1, y), ACCENT, 2) &&
		!near(tc_pixel(&img, x, y - 1), ACCENT, 2) &&
		near(tc_pixel(&img, x + 2, y + TH + 1), SURFACE, 0);
	tc_expect(req, ok, "%s: xterm at (%d,%d)", what, x, y);
	tc_image_free(&img);
}

static void drag(struct tc *tc, uint32_t button, int x0, int y0, int x1, int y1, bool release) {
	tc_pointer_move(tc, x0, y0);
	tc_pointer_button(tc, button, true);
	for (int i = 1; i <= 6; i++) {
		tc_pointer_move(tc, x0 + (x1 - x0) * i / 6, y0 + (y1 - y0) * i / 6);
	}
	if (release) {
		tc_pointer_button(tc, button, false);
		tc_wait(tc, 40);
	}
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "m3-snap.png";
	struct tc *tc = tc_connect();
	struct tc_image img = {0};

	struct tc_window *zpool = place(tc, 100, 100, (struct tc_window_opts){
		.title = "zpool status", .app_id = "zpool", .width = 598, .height = 372,
		.color = SURFACE});
	struct tc_window *xterm = place(tc, 900, 600, (struct tc_window_opts){
		.title = "xterm \xe2\x80\x94 ~/.twmrc", .app_id = "xterm", .width = 498, .height = 272,
		.color = SURFACE});
	tc_pointer_move(tc, 1800, 200); /* rest on the root: focus stays on xterm */
	tc_wait(tc, 120);

	/* OUT-13: B3 while placing fits the largest empty rectangle around the
	 * pointer: left of it zpool (ends at 700), below it xterm (from 600),
	 * right of it the Icon Manager (from 1660, M4). */
	tc_pointer_move(tc, 800, 300);
	struct tc_window *fit = tc_window_create(tc, &(struct tc_window_opts){
		.title = "fit", .app_id = "fit", .width = 300, .height = 200, .color = SURFACE});
	tc_wait(tc, 20);
	tc_pointer_click(tc, TC_BTN_RIGHT);
	tc_wait(tc, 120);
	int fw, fh;
	tc_window_size(fit, &fw, &fh);
	tc_expect("OUT-13", fw == 960 - 2 && fh == 600 - TH - 2,
		"B3 fits 960x600+700+0 (client %dx%d)", fw, fh);
	tc_screenshot(tc, &img);
	tc_expect_pixel("OUT-13", &img, 702, 2, ACCENT, 2);
	tc_expect_pixel("OUT-13", &img, 1659, 300, ACCENT, 2);
	tc_expect_pixel("OUT-13", &img, 1000, 599, ACCENT, 2);
	tc_image_free(&img);
	tc_window_destroy(fit);
	tc_pointer_move(tc, 1000, 700); /* rest on xterm to focus it again */
	tc_wait(tc, 150);
	tc_pointer_move(tc, 1800, 200);
	tc_wait(tc, 50);
	tc_expect("FOC-2", tc_window_activated(xterm), "xterm focused");

	/* OUT-7, OUT-8: M-S-m, arrows (Shift = 10 px), Return commits. */
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_M);
	for (int i = 0; i < 3; i++) {
		tc_chord(tc, 1, TC_KEY_RIGHT);
	}
	tc_chord(tc, 2, TC_KEY_LEFTSHIFT, TC_KEY_DOWN);
	tc_chord(tc, 1, TC_KEY_ENTER);
	tc_wait(tc, 40);
	expect_at(tc, "OUT-7", 903, 610, "keyboard move +3,+10");
	/* OUT-6: Esc cancels and leaves the window put. */
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_M);
	for (int i = 0; i < 5; i++) {
		tc_chord(tc, 1, TC_KEY_LEFT);
	}
	tc_chord(tc, 1, TC_KEY_ESC);
	tc_wait(tc, 40);
	expect_at(tc, "OUT-6", 903, 610, "Esc cancelled");

	/* SNP-1…6 (B3): drag xterm so its left edge lands 4 px right of zpool's
	 * right edge (700) and its top 3 px above the ⅓ line (360). */
	drag(tc, TC_BTN_MIDDLE, 950, 623, 950 - 199, 623 - 253, false);
	tc_screenshot(tc, &img);
	tc_image_write_png(&img, out);
	uint32_t edge = tc_blend(0xE8E6DF, 0.85, GROUND);
	/* The outline's right and bottom edges, where no guide runs beneath. */
	tc_expect("SNP-1", near(tc_pixel(&img, 1199, 500), edge, 40) &&
		near(tc_pixel(&img, 800, 659), edge, 40) &&
		!near(tc_pixel(&img, 1200, 500), edge, 40),
		"outline snapped to 500x300+700+360");
	/* Dashed guides span the output; the outline covers 300 px and 500 px
	 * of them. */
	int col = count_col(&img, 700, 0, 350, ACCENT);
	tc_expect("SNP-3", col > 140 && col < 210, "vertical guide at x=700, dashed (%d px)", col);
	int row = count_row(&img, 360, 0, 690, ACCENT);
	tc_expect("SNP-3", row > 280 && row < 420, "horizontal guide at y=360, dashed (%d px)", row);
	tc_expect("SNP-3", count_box(&img, 706, 8, 840, 26, ACCENT, 60) > 30,
		"label at the top of the vertical guide");
	tc_expect("SNP-3", count_box(&img, 10, 336, 140, 356, ACCENT, 60) > 30,
		"label above the horizontal guide");
	int faint = count_col(&img, 1280, 0, 1080, GUIDE_FAINT);
	tc_expect("SNP-4", faint > 150, "faint thirds guide at x=1280 (%d px)", faint);
	tc_expect("SNP-6", count_box(&img, 1600, 1040, 1910, 1072, 0x85887C, 50) > 50,
		"output name, mode and scale in the corner");
	tc_expect("SNP-5", count_box(&img, 820, 490, 1080, 530, MUTED, 40) > 30,
		"readout explains the snap");
	tc_image_free(&img);
	tc_pointer_button(tc, TC_BTN_MIDDLE, false);
	tc_wait(tc, 40);
	expect_at(tc, "SNP-1", 700, 360, "snapped move committed");

	/* SNP-7: M pressed during the drag moves freely. */
	drag(tc, TC_BTN_MIDDLE, 750, 373, 755, 413, false);
	tc_key(tc, TC_KEY_LEFTMETA, true);
	tc_pointer_move(tc, 755, 414);
	tc_pointer_move(tc, 755, 413);
	tc_screenshot(tc, &img);
	/* Above xterm (whose focused border is at x=700 too) only a guide could
	 * be accent. */
	col = count_col(&img, 700, 0, 350, ACCENT);
	tc_expect("SNP-7", col < 5, "no guide while M is held (%d px)", col);
	tc_image_free(&img);
	tc_pointer_button(tc, TC_BTN_MIDDLE, false);
	tc_key(tc, TC_KEY_LEFTMETA, false);
	tc_wait(tc, 40);
	expect_at(tc, "SNP-7", 705, 400, "free move landed unsnapped");

	/* HIS-2: undo per window, back through each move; place stops it. */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_Z);
	tc_wait(tc, 40);
	expect_at(tc, "HIS-2", 700, 360, "undo free move");
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_Z);
	tc_wait(tc, 40);
	expect_at(tc, "HIS-2", 903, 610, "undo snapped move");
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_Z);
	tc_wait(tc, 40);
	expect_at(tc, "HIS-2", 900, 600, "undo keyboard move");
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_Z);
	tc_wait(tc, 40);
	expect_at(tc, "HIS-2", 900, 600, "placing isn't undone");
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_Z);
	tc_wait(tc, 40);
	expect_at(tc, "HIS-2", 903, 610, "redo");

	/* HIS-3: M-S-h shows the history, left of the Icon Manager's column;
	 * title-only tab. */
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_H);
	tc_wait(tc, 60);
	int hx = 1920 - 24 - 236 - 24 - 342, hy = 24;
	tc_screenshot(tc, &img);
	tc_expect_pixel("FRM-18", &img, hx + 2, hy + 2, GRAPHITE, 2);
	tc_expect_pixel("FRM-18", &img, hx + 7, hy + 7, GRAPHITE, 2); /* no iconify box */
	tc_expect_pixel("HIS-3", &img, hx + 1, hy + TH + 1 + 5, SURFACE, 2);
	tc_expect("HIS-3", count_box(&img, hx + 1, hy + TH + 1, hx + 341, hy + TH + 1 + 33 * 4,
		0xE8E6DF, 60) > 100, "rows: operation, window, age");
	tc_expect("HIS-3", count_box(&img, hx + 1, hy + TH, hx + 341, hy + 500, ACCENT, 60) > 20,
		"footer keys in accent");
	tc_image_free(&img);

	/* HIS-3: an operation in progress shows on top as pending. */
	drag(tc, TC_BTN_MIDDLE, 150, 113, 180, 133, false); /* on zpool's title text */
	tc_screenshot(tc, &img);
	tc_expect_pixel("HIS-3", &img, hx + 2, hy + TH + 1 + 16, ACCENT, 2); /* 3 px bar */
	char hpath[4096];
	snprintf(hpath, sizeof(hpath), "%.*s-history.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, hpath);
	tc_expect_pixel("HIS-3", &img, hx + 40, hy + TH + 1 + 3,
		tc_blend(ACCENT, 0.08, SURFACE), 2);                            /* wash */
	tc_image_free(&img);
	tc_chord(tc, 1, TC_KEY_ESC); /* OUT-6 during a pointer drag */
	tc_pointer_button(tc, TC_BTN_MIDDLE, false);
	tc_wait(tc, 40);
	tc_screenshot(tc, &img);
	/* zpool didn't move: its tab corner is still at (100, 100). */
	tc_expect("OUT-6", !near(tc_pixel(&img, 100, 100), GROUND, 8) &&
		near(tc_pixel(&img, 99, 100), GROUND, 30) && near(tc_pixel(&img, 100, 99), GROUND, 30),
		"Esc during a drag: zpool still at (100,100)");
	tc_expect_pixel("HIS-3", &img, hx + 2, hy + TH + 1 + 16, SURFACE, 2); /* not pending */
	tc_image_free(&img);

	tc_window_destroy(xterm);
	tc_window_destroy(zpool);
	tc_disconnect(tc);
	return tc_report();
}
