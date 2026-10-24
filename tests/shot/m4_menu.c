/*
 * M4 screenshot test, part 2: the root menu (MNU-1 … MNU-7), window
 * functions picked from it (MNU-5), and a layer-shell bar whose exclusive
 * zone the placement respects (GEN-2).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define SURFACE 0x121310
#define GRAPHITE 0x23251F
#define HAIRLINE 0x2F312A
#define HAIRLINE_STRONG 0x3A3D34
#define DIM 0x85887C
#define MUTED 0x9A9C93
#define ACCENT 0xFFB000
#define ON_ACCENT 0x0E0F0C
#define TH 26

/* Menu "main" at (MX, MY): header 32, list padding 4, items 30, rules 9.
 * Rows: xterm 0, browser 30, files 60, run… 90, rule 120, move 129, resize 159,
 * squeeze 189, iconify 219, raise / lower 249, close 279, kill 309, rule 339,
 * desktops 348, settings 378, reload 408, exit 438. */
#define MX 500
#define MY 120
#define ITEM_Y(row_offset) (MY + 32 + 4 + (row_offset))
/* Item tops: xterm 0, browser 30, files 60, rule 90, move 99, resize 129,
 * squeeze 159, iconify 189, raise/lower 219, rule 249, desktops 258,
 * reload 288, exit 318. */

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

static void shot(struct tc *tc, struct tc_image *img) {
	tc_image_free(img);
	tc_screenshot(tc, img);
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "m4-menu.png";
	struct tc *tc = tc_connect();
	struct tc_image img = {0};

	tc_pointer_move(tc, 1000, 600);
	struct tc_window *win = tc_window_create(tc, &(struct tc_window_opts){
		.title = "target", .app_id = "target", .width = 400, .height = 250, .color = SURFACE});
	tc_wait(tc, 20);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 40);

	/* MNU-1: a click on the bare root opens "main" and keeps it open. */
	tc_pointer_move(tc, MX, MY);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 40);
	/* MNU-2: the item under the pointer is in accent (B0: squeeze). */
	tc_pointer_move(tc, MX + 100, ITEM_Y(189) + 15);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_image_write_png(&img, out);
	tc_expect_pixel("MNU-2", &img, MX + 3, MY + 3, GRAPHITE, 2);            /* header */
	tc_expect("MNU-2", count_box(&img, MX + 10, MY + 8, MX + 100, MY + 26, MUTED, 40) > 20,
		"TOM'S WM in the header");
	tc_expect("MNU-2", count_box(&img, MX + 180, MY + 8, MX + 250, MY + 24,
		HAIRLINE_STRONG, 2) > 100, "header hatch");
	tc_expect_pixel("MNU-2", &img, MX, MY + 200, HAIRLINE_STRONG, 2);       /* border */
	tc_expect_pixel("MNU-2", &img, MX + 5, ITEM_Y(10), SURFACE, 2);
	tc_expect("MNU-2", count_box(&img, MX + 14, ITEM_Y(0), MX + 120, ITEM_Y(30), 0xE8E6DF, 60) > 20,
		"first item label");
	tc_expect("MNU-3", count_box(&img, MX + 200, ITEM_Y(0), MX + 246, ITEM_Y(30), DIM, 40) > 15,
		"shortcut column (M-\xe2\x86\xb5)");
	tc_expect_pixel("MNU-2", &img, MX + 20, ITEM_Y(120) + 4, HAIRLINE, 2);   /* separator */
	tc_expect_pixel("MNU-2", &img, MX + 5, ITEM_Y(189) + 3, ACCENT, 2);     /* selected */
	tc_expect("MNU-2", count_box(&img, MX + 14, ITEM_Y(189), MX + 120, ITEM_Y(219),
		ON_ACCENT, 30) > 20, "selected label on accent");
	tc_expect("MNU-3", count_box(&img, MX + 220, ITEM_Y(189), MX + 246, ITEM_Y(219),
		ON_ACCENT, 30) > 10, "squeeze reads B3");

	/* MNU-6: keys drive it; Esc closes. */
	tc_chord(tc, 1, TC_KEY_DOWN);
	tc_wait(tc, 30);
	shot(tc, &img);
	tc_expect_pixel("MNU-6", &img, MX + 5, ITEM_Y(219) + 3, ACCENT, 2);     /* iconify */
	tc_chord(tc, 1, TC_KEY_ESC);
	tc_wait(tc, 30);
	shot(tc, &img);
	tc_expect_pixel("MNU-1", &img, MX + 3, MY + 3, GROUND, 8);

	/* MNU-1, MNU-5: press, drag to "iconify", release; then click the
	 * window: it iconifies. */
	tc_pointer_move(tc, MX, MY);
	tc_pointer_button(tc, TC_BTN_LEFT, true);
	for (int i = 1; i <= 4; i++) {
		tc_pointer_move(tc, MX + 25 * i, MY + (ITEM_Y(219) + 15 - MY) * i / 4);
	}
	tc_pointer_button(tc, TC_BTN_LEFT, false);
	tc_wait(tc, 30);
	shot(tc, &img);
	tc_expect_pixel("MNU-1", &img, MX + 3, MY + 3, GROUND, 8);               /* closed */
	tc_pointer_move(tc, 1150, 700);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
	shot(tc, &img);
	tc_expect_pixel("MNU-5", &img, 1150, 700, GROUND, 8);                    /* iconified */
	tc_expect_pixel("MNU-5", &img, 72 + 8, 940 + 20, HAIRLINE, 2);           /* its icon */
	tc_pointer_move(tc, 72 + 36, 940 + 28);
	tc_pointer_click(tc, TC_BTN_LEFT);                                       /* back */
	tc_wait(tc, 60);

	/* MNU-5: "move" from the menu, a click on the window, the outline
	 * follows the pointer, a click puts it down. */
	tc_pointer_move(tc, MX, MY);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_pointer_move(tc, MX + 100, ITEM_Y(129) + 15);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_pointer_move(tc, 1100, 613);
	tc_pointer_click(tc, TC_BTN_LEFT);                                       /* select */
	for (int i = 1; i <= 4; i++) {
		tc_pointer_move(tc, 1100 - 25 * i, 613 + 20 * i);
	}
	tc_pointer_click(tc, TC_BTN_LEFT);                                       /* put down */
	tc_wait(tc, 60);
	shot(tc, &img);
	tc_expect("MNU-5", !near(tc_pixel(&img, 900, 680), GROUND, 8) &&
		near(tc_pixel(&img, 1350, 610), GROUND, 8),
		"moved by (-100,+80) to (900,680)");

	/* MNU-4, MNU-7: the "desktops" submenu opens beside its item; "3"
	 * switches. Opened with M-m this time (MNU-6). */
	tc_pointer_move(tc, MX, MY);
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_M);
	tc_pointer_move(tc, MX + 100, ITEM_Y(348) + 15);
	tc_wait(tc, 30);
	shot(tc, &img);
	int sx = MX + 260 - 1, sy = ITEM_Y(348) - 32 - 4;
	tc_expect_pixel("MNU-7", &img, sx + 3, sy + 3, GRAPHITE, 2);             /* submenu */
	tc_expect("MNU-7", count_box(&img, sx + 10, sy + 8, sx + 110, sy + 26, MUTED, 40) > 20,
		"DESKTOPS header");
	tc_pointer_move(tc, sx + 100, sy + 32 + 4 + 60 + 15);                    /* "3" */
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_expect_pixel("MNU-7", &img, 900 + 5, 680 + 5, GROUND, 8);             /* on desktop 3 */
	tc_expect_pixel("MNU-7", &img, 1661 + 10 + 2 * 55 + 2, 24 + TH + 1 + 12, ACCENT, 2);

	/* GEN-2: a 30 px bar with an exclusive zone; B3 fit stops under it. */
	struct tc_layer *bar = tc_layer_bar(tc, 30, 30, 0x445566);
	tc_wait(tc, 60);
	tc_pointer_move(tc, 800, 500);
	struct tc_window *fit = tc_window_create(tc, &(struct tc_window_opts){
		.title = "fit", .app_id = "fit", .width = 300, .height = 200, .color = SURFACE});
	tc_wait(tc, 20);
	tc_pointer_click(tc, TC_BTN_RIGHT);
	tc_wait(tc, 100);
	shot(tc, &img);
	char path[4096];
	snprintf(path, sizeof(path), "%.*s-layer.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect_pixel("GEN-2", &img, 960, 10, 0x445566, 0);                    /* the bar */
	int fw, fh;
	tc_window_size(fit, &fw, &fh);
	tc_expect("GEN-2", fw == 1660 - 2 && fh == 1050 - TH - 2,
		"fit stays below the bar: 1660x1050+0+30 (client %dx%d)", fw, fh);
	tc_expect_pixel("GEN-2", &img, 2, 32, ACCENT, 2);                        /* tab at y=30 */
	tc_layer_destroy(bar);
	tc_image_free(&img);

	tc_window_destroy(fit);
	tc_window_destroy(win);
	tc_disconnect(tc);
	return tc_report();
}
