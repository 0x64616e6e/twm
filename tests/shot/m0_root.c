/*
 * M0 screenshot test: the root (ROOT-1) on the headless backend (GEN-4), and
 * a client window reaching the screen.
 */
#include <stdio.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define INK 0xE8E6DF

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "m0-root.png";
	struct tc *tc = tc_connect();
	tc_expect("GEN-4", tc_output_width(tc) == 1920 && tc_output_height(tc) == 1080,
		"headless output is %dx%d", tc_output_width(tc), tc_output_height(tc));

	struct tc_image img = {0};
	if (!tc_expect("D-7", tc_screenshot(tc, &img), "screencopy returns a frame")) {
		return tc_report();
	}

	/* ROOT-1: a 2x2 dot at (2,2) in every 6x6 tile, ink at 7% over ground. */
	uint32_t dot = tc_blend(INK, 0.07, GROUND);
	tc_expect_pixel("ROOT-1", &img, 0, 0, GROUND, 0);
	tc_expect_pixel("ROOT-1", &img, 2, 2, dot, 1);
	tc_expect_pixel("ROOT-1", &img, 3, 3, dot, 1);
	tc_expect_pixel("ROOT-1", &img, 4, 4, GROUND, 0);
	int bad = 0;
	for (int y = 0; y < 60; y++) {
		for (int x = 0; x < 60; x++) {
			int in = (x % 6 == 2 || x % 6 == 3) && (y % 6 == 2 || y % 6 == 3);
			uint32_t want = in ? dot : GROUND;
			uint32_t got = tc_pixel(&img, x, y);
			int d = 0;
			for (int s = 0; s <= 16; s += 8) {
				int c = (int)((got >> s) & 0xff) - (int)((want >> s) & 0xff);
				d += c < 0 ? -c : c;
			}
			bad += d > 3;
		}
	}
	tc_expect("ROOT-1", bad == 0, "weave has a 6 px period (%d off pixels in 60x60)", bad);
	tc_expect_pixel("ROOT-1", &img, 1919 - 3, 1079 - 3, (1916 % 6 == 2 || 1916 % 6 == 3) &&
		(1076 % 6 == 2 || 1076 % 6 == 3) ? dot : GROUND, 1);
	tc_image_free(&img);

	/* A client window reaches the screen. Since M1 new windows wait for
	 * click-to-place (OUT-12): park the pointer, click. */
	tc_pointer_move(tc, 810, 440);
	struct tc_window *win = tc_window_create(tc, &(struct tc_window_opts){
		.title = "m0", .app_id = "twm-test", .width = 300, .height = 200,
		.color = 0x3060A0,
	});
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_pointer_move(tc, 1900, 20);
	tc_wait(tc, 100);
	if (tc_screenshot(tc, &img)) {
		tc_expect_pixel("GEN-4", &img, 960, 590, 0x3060A0, 0);
		tc_expect_pixel("ROOT-1", &img, 805, 590, GROUND, 8);
		tc_expect("D-7", tc_image_write_png(&img, out), "wrote %s", out);
		tc_image_free(&img);
	}
	tc_window_destroy(win);
	tc_disconnect(tc);
	return tc_report();
}
