/*
 * M4 screenshot test, part 1: the Icon Manager at rest and filtering
 * (IMG-*), desktops (DSK-1, DSK-2), the Panner (DSK-3, DSK-4), iconify with
 * root icons (FRM-13, ROOT-9, ROOT-10), squeeze (FRM-14), undo of those
 * (HIS-1, HIS-2) and the log window (ROOT-8).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define SURFACE 0x121310
#define GRAPHITE 0x23251F
#define HAIRLINE 0x2F312A
#define MINI 0x5A5D53
#define MUTED 0x9A9C93
#define DIM 0x85887C
#define ACCENT 0xFFB000
#define TH 26

/* The Icon Manager's content corner: frame at (1920-24-236, 24). */
#define IMX (1660 + 1)
#define IMY (24 + TH + 1)
/* Its list starts after the desktop row (10+28+10+1) and 6 px padding. */
#define IM_ROW(i) (IMY + 49 + 6 + (i) * 30)
/* The Panner's content corner: frame 236 x (150+28), bottom-right. */
#define PNX (1660 + 1)
#define PNY (1080 - 24 - 178 + TH + 1)

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

static void key(struct tc *tc, uint32_t k) {
	tc_chord(tc, 1, k);
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "m4-imgr.png";
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	uint32_t wash = tc_blend(ACCENT, 0.10, SURFACE);

	struct tc_window *zpool = place(tc, 100, 100, (struct tc_window_opts){
		.title = "zpool status", .app_id = "zpool", .width = 598, .height = 372,
		.color = SURFACE});
	struct tc_window *xterm = place(tc, 800, 150, (struct tc_window_opts){
		.title = "xterm \xe2\x80\x94 ~/.twmrc", .app_id = "xterm", .width = 498, .height = 272,
		.color = SURFACE});
	tc_pointer_move(tc, 1300, 700); /* the root: focus stays on xterm */
	tc_wait(tc, 150);

	/* --- IMG-1…4: the Icon Manager at rest (B0) --- */
	shot(tc, &img);
	tc_image_write_png(&img, out);
	tc_expect_pixel("IMG-1", &img, 1662, 26, GRAPHITE, 2);           /* utility tab */
	tc_expect_pixel("IMG-1", &img, 1660, 300 > 24 + TH ? 24 + TH + 5 : 0, HAIRLINE, 2);
	tc_expect_pixel("IMG-2", &img, IMX + 12, IMY + 12, ACCENT, 2);   /* desktop 1 current */
	tc_expect_pixel("IMG-2", &img, IMX + 65, IMY + 10, HAIRLINE, 2); /* desktop 2 outlined */
	tc_expect_pixel("IMG-2", &img, IMX + 67, IMY + 13, SURFACE, 2);
	/* Sorted by name: xterm (focused), zpool. */
	tc_expect_pixel("IMG-3", &img, IMX + 14, IM_ROW(0) + 13, ACCENT, 2);   /* filled marker */
	tc_expect_pixel("IMG-3", &img, IMX + 200, IM_ROW(0) + 3, wash, 2);     /* washed row */
	tc_expect_pixel("IMG-3", &img, IMX + 12, IM_ROW(1) + 11, MUTED, 2);    /* outlined marker */
	tc_expect_pixel("IMG-3", &img, IMX + 14, IM_ROW(1) + 13, SURFACE, 2);
	tc_expect("IMG-3", count_box(&img, IMX + 30, IM_ROW(1), IMX + 200, IM_ROW(1) + 30,
		0xC9C7BF, 60) > 40, "zpool's row has its title");

	/* --- DSK-3: the Panner (B0), bottom-right --- */
	tc_expect_pixel("DSK-3", &img, PNX + 10, PNY + 40, ACCENT, 2);   /* cell 1 current */
	tc_expect_pixel("DSK-3", &img, PNX + 121, PNY + 10, HAIRLINE, 2); /* cell 2 dashed */
	tc_expect_pixel("DSK-3", &img, PNX + 124, PNY + 10, SURFACE, 2);
	/* Miniatures at (frame) * cell / output: xterm focused, zpool not. */
	double sx = 104.0 / 1920, sy = 62.0 / 1080;
	int mx = 10 + (int)lround(800 * sx), my = 10 + (int)lround(150 * sy);
	tc_expect_pixel("DSK-3", &img, PNX + mx, PNY + my + 6, ACCENT, 2);
	tc_expect_pixel("DSK-3", &img, PNX + mx + 6, PNY + my + 6,
		tc_blend(ACCENT, 0.12, SURFACE), 2);
	int zx = 10 + (int)lround(100 * sx), zy = 10 + (int)lround(100 * sy);
	tc_expect_pixel("DSK-3", &img, PNX + zx, PNY + zy + 8, MINI, 2);

	/* --- IMG-6…8: type at the Icon Manager --- */
	tc_pointer_move(tc, IMX + 150, IM_ROW(1) + 10); /* rest on it: it takes focus */
	tc_wait(tc, 150);
	key(tc, TC_KEY_Z);
	key(tc, TC_KEY_P);
	tc_wait(tc, 40);
	shot(tc, &img);
	char fpath[4096];
	snprintf(fpath, sizeof(fpath), "%.*s-filter.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, fpath);
	tc_expect_pixel("IMG-6", &img, 1662, 26, ACCENT, 2);              /* frame focused */
	/* Filter row 10..43 (input 33 px, ground), list from 60, WINDOWS head
	 * 22 px, first result 82..117 selected. */
	tc_expect_pixel("IMG-6", &img, IMX + 150, IMY + 14, GROUND, 2);   /* input box */
	tc_expect("IMG-6", count_box(&img, IMX + 60, IMY + 10, IMX + 120, IMY + 43, 0xE8E6DF, 60) > 15,
		"the input shows what was typed");
	tc_expect_pixel("IMG-8", &img, IMX + 1, IMY + 82 + 17, ACCENT, 2); /* selection bar */
	tc_expect_pixel("IMG-8", &img, IMX + 150, IMY + 82 + 3, wash, 2);
	tc_expect("IMG-7", count_box(&img, IMX + 30, IMY + 82, IMX + 52, IMY + 117, ACCENT, 50) > 15,
		"the matched \"zp\" is in accent");
	tc_expect("IMG-7", count_box(&img, IMX + 10, IMY + 60, IMX + 90, IMY + 82, DIM, 40) > 10,
		"WINDOWS heading");
	tc_expect("IMG-6", !tc_window_activated(zpool) && !tc_window_activated(xterm),
		"typing went to the Icon Manager, not a client");
	key(tc, TC_KEY_ENTER);
	tc_wait(tc, 60);
	tc_expect("IMG-8", tc_window_activated(zpool), "Return raises and focuses zpool");
	shot(tc, &img);
	tc_expect_pixel("IMG-8", &img, IMX + 12, IMY + 12, ACCENT, 2); /* back at rest */

	/* --- DSK-1, DSK-2: desktops --- */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_2);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect_pixel("DSK-2", &img, 101, 101, GROUND, 8);             /* zpool gone */
	tc_expect_pixel("DSK-1", &img, IMX + 66, IMY + 12, ACCENT, 2);   /* desktop 2 current */
	tc_expect_pixel("DSK-5", &img, PNX + 120, PNY + 40, ACCENT, 2);  /* Panner agrees */
	tc_expect_pixel("IMG-1", &img, 1662, 26, GRAPHITE, 2);           /* still there */
	struct tc_window *notes = place(tc, 300, 300, (struct tc_window_opts){
		.title = "notes", .app_id = "notes", .width = 300, .height = 200, .color = SURFACE});
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_1);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect_pixel("DSK-1", &img, 302, 302, GROUND, 8);             /* notes on 2 */
	tc_expect("DSK-1", tc_window_activated(zpool), "back on 1, focus back on zpool");

	/* f.send: M-S-2 sends the focused window; the Panner shows it on 2. */
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_2);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect_pixel("DSK-1", &img, 101, 101, GROUND, 8);
	int z2 = 120 + (int)lround(100 * sx);
	tc_expect_pixel("DSK-4", &img, PNX + z2, PNY + zy + 8, MINI, 2);
	/* B1 on the Icon Manager's desktop 2 switches. */
	tc_pointer_move(tc, IMX + 10 + 55 + 20, IMY + 24);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect("IMG-2", !near(tc_pixel(&img, 101, 101), GROUND, 8), "zpool on desktop 2, shown");
	tc_expect_pixel("IMG-2", &img, IMX + 66, IMY + 12, ACCENT, 2);
	/* M-Return in the filter brings xterm (on 1) here (IMG-8). */
	tc_pointer_move(tc, IMX + 150, IMY + 30);
	tc_wait(tc, 150);
	key(tc, TC_KEY_P); /* no wait: "xterm — ~/.twmrc" lacks "p"; use its x */
	key(tc, TC_KEY_ESC);
	tc_chord(tc, 1, 45 /* KEY_X */);
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_ENTER);
	tc_wait(tc, 60);
	tc_expect("IMG-8", tc_window_activated(xterm), "M-Return brought xterm and focused it");
	shot(tc, &img);
	tc_expect_pixel("IMG-8", &img, 801, 151, ACCENT, 2);             /* on desktop 2 now */
	tc_expect_pixel("IMG-8", &img, IMX + 66, IMY + 12, ACCENT, 2);   /* still on 2 */

	/* --- FRM-13, ROOT-9: iconify box; the root icon; IM iconified row --- */
	tc_pointer_move(tc, 806, 162);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
	shot(tc, &img);
	snprintf(fpath, sizeof(fpath), "%.*s-icons.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, fpath);
	tc_expect_pixel("FRM-13", &img, 900, 300, GROUND, 8);            /* xterm hidden */
	tc_expect_pixel("ROOT-9", &img, 72 + 8, 940 + 20, HAIRLINE, 2);  /* icon box */
	tc_expect_pixel("ROOT-9", &img, 72 + 8 + 2, 940 + 2, SURFACE, 2);
	tc_expect("ROOT-9", count_box(&img, 72, 940 + 64, 72 + 72, 940 + 82, 0xC9C7BF, 60) > 15,
		"icon label");
	/* The Icon Manager on desktop 2: notes, zpool, a divider (6+1+6 px),
	 * then xterm iconified (bar marker, dim). */
	tc_expect_pixel("IMG-4", &img, IMX + 20, IM_ROW(2) + 6, HAIRLINE, 2);
	tc_expect_pixel("IMG-4", &img, IMX + 14, IM_ROW(2) + 13 + 15, DIM, 2);
	/* ROOT-10: B1 on the icon deiconifies and focuses. */
	tc_pointer_move(tc, 72 + 36, 940 + 28);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
	tc_expect("ROOT-10", tc_window_activated(xterm), "B1 on the icon: xterm back, focused");
	shot(tc, &img);
	tc_expect_pixel("ROOT-10", &img, 801, 151, ACCENT, 2);
	tc_expect_pixel("ROOT-10", &img, 72 + 8, 940 + 20, GROUND, 8);
	/* M-i iconifies; B2 drags the icon. */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_I);
	tc_wait(tc, 40);
	tc_pointer_move(tc, 72 + 36, 940 + 28);
	tc_pointer_button(tc, TC_BTN_MIDDLE, true);
	for (int i = 1; i <= 5; i++) {
		tc_pointer_move(tc, 72 + 36 + 60 * i, 940 + 28 - 60 * i);
	}
	tc_pointer_button(tc, TC_BTN_MIDDLE, false);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect_pixel("ROOT-10", &img, 372 + 8, 640 + 20, HAIRLINE, 2); /* moved +300,-300 */
	tc_expect_pixel("ROOT-10", &img, 72 + 8, 940 + 20, GROUND, 8);
	/* IMG-5: B1 on the iconified row brings it back. */
	tc_pointer_move(tc, IMX + 100, IM_ROW(2) + 13 + 15);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
	tc_expect("IMG-5", tc_window_activated(xterm), "B1 on the iconified row: xterm back");

	/* --- FRM-14: B3 on the title squeezes; HIS-2 undoes it --- */
	tc_pointer_move(tc, 900, 163);
	tc_pointer_click(tc, TC_BTN_RIGHT);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect_pixel("FRM-14", &img, 801, 151, ACCENT, 2);            /* the tab stays */
	tc_expect_pixel("FRM-14", &img, 1000, 300, GROUND, 8);           /* the body is gone */
	tc_expect_pixel("FRM-14", &img, 800, 190, GROUND, 8);            /* and the border */
	tc_pointer_move(tc, 1300, 700);
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_Z);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect_pixel("HIS-2", &img, 1000, 300, SURFACE, 0);           /* unsqueezed by undo */
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_Z);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect_pixel("HIS-2", &img, 1000, 300, GROUND, 8);            /* and squeezed again */
	tc_pointer_move(tc, 900, 163);
	tc_pointer_click(tc, TC_BTN_RIGHT);
	tc_wait(tc, 40);

	/* --- ROOT-8: M-n, the log window --- */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_N);
	tc_wait(tc, 60);
	shot(tc, &img);
	tc_expect_pixel("ROOT-8", &img, 26, 26, GRAPHITE, 2);
	tc_expect("ROOT-8", count_box(&img, 25, 24 + TH + 1, 640, 24 + TH + 60, 0xC9C7BF, 60) > 40,
		"recent root-line messages");
	tc_image_free(&img);

	tc_window_destroy(notes);
	tc_window_destroy(xterm);
	tc_window_destroy(zpool);
	tc_disconnect(tc);
	return tc_report();
}
