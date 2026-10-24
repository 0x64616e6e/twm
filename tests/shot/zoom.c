/*
 * Screenshot test, after M7: zoom and fullscreen (ZOM-*). twm's f.zoom,
 * f.horizoom and f.fullzoom keep the frame; f.fullscreen shows the client
 * alone; app requests do the same; undo takes them back; a fullscreen
 * window steps back when the focus leaves it; and a fullscreen window that
 * captures the screen still says SCREEN (CAP-2).
 */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define ACCENT 0xFFB000
#define ALERT 0xFF8A65
#define BLUE 0x3060C0
#define RED 0xC03030
#define GREEN 0x2E7D32
#define TH 26
#define W 1920
#define H 1080
#define KEY_F 33
#define KEY_Z 44

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

static void shot(struct tc *tc, struct tc_image *img, const char *out, const char *name) {
	tc_image_free(img);
	tc_screenshot(tc, img);
	char path[4096];
	snprintf(path, sizeof(path), "%.*s-%s.png", (int)strlen(out) - 4, out, name);
	tc_image_write_png(img, path);
}

static bool is(const struct tc_image *img, int x, int y, uint32_t c) {
	return near(tc_pixel(img, x, y), c, 6);
}

static void expect_size(const char *req, struct tc_window *w, int ew, int eh, const char *what) {
	int cw, ch;
	tc_window_size(w, &cw, &ch);
	tc_expect(req, cw == ew && ch == eh, "%s: %dx%d (want %dx%d)", what, cw, ch, ew, eh);
}

static void states(struct tc_window *w, bool *max, bool *fs, int *tiled) {
	tc_window_states(w, max, fs, tiled);
}

/* A capturing client that goes fullscreen. 'w' window, 'f' fullscreen,
 * 'c' capture, 'q' quit; each answered with one byte. */
struct helper {
	pid_t pid;
	int cmd, ack;
};

static void helper_main(int cmd, int ack) {
	struct tc *t = tc_connect();
	struct tc_window *win = NULL;
	bool capturing = false;
	for (;;) {
		struct pollfd p = {.fd = cmd, .events = POLLIN};
		if (poll(&p, 1, capturing ? 0 : 20) > 0) {
			char c;
			if (read(cmd, &c, 1) != 1 || c == 'q') {
				break;
			}
			if (c == 'w' && !win) {
				win = tc_window_create(t, &(struct tc_window_opts){
					.title = "player", .app_id = "player",
					.width = 480, .height = 270, .color = GREEN});
			} else if (c == 'f' && win) {
				tc_window_set_fullscreen(win, true);
			} else if (c == 'c') {
				capturing = true;
			}
			tc_roundtrip(t);
			if (write(ack, &c, 1) != 1) {
				break;
			}
		}
		if (capturing) {
			struct tc_image img = {0};
			tc_screenshot(t, &img);
			tc_image_free(&img);
		}
		tc_wait(t, capturing ? 40 : 1);
	}
	if (win) {
		tc_window_destroy(win);
	}
	tc_disconnect(t);
	_exit(0);
}

static struct helper helper_start(void) {
	int a[2], b[2];
	struct helper h = {-1, -1, -1};
	if (pipe(a) < 0 || pipe(b) < 0) {
		return h;
	}
	h.pid = fork();
	if (h.pid == 0) {
		close(a[1]);
		close(b[0]);
		helper_main(a[0], b[1]);
	}
	close(a[0]);
	close(b[1]);
	h.cmd = a[1];
	h.ack = b[0];
	fcntl(h.ack, F_SETFL, O_NONBLOCK);
	return h;
}

static bool helper_do(struct tc *tc, struct helper *h, char c) {
	if (write(h->cmd, &c, 1) != 1) {
		return false;
	}
	for (int i = 0; i < 200; i++) {
		char r;
		if (read(h->ack, &r, 1) == 1) {
			return r == c;
		}
		tc_wait(tc, 10);
	}
	return false;
}

static void helper_quit(struct tc *tc, struct helper *h) {
	char c = 'q';
	if (write(h->cmd, &c, 1) != 1) {
		kill(h->pid, SIGKILL);
	}
	for (int i = 0; i < 100 && waitpid(h->pid, NULL, WNOHANG) != h->pid; i++) {
		tc_wait(tc, 20);
	}
	close(h->cmd);
	close(h->ack);
}

static void place_at(struct tc *tc, int x, int y) {
	tc_pointer_move(tc, x, y);
	tc_wait(tc, 40);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 80);
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "zoom.png";
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	bool max, fs;
	int tiled;

	/* f.zoom and f.horizoom have no default key (ZOM-9): bind them here. */
	char rc[4096], cmd[4200];
	snprintf(rc, sizeof(rc), "%s/.twmrc", getenv("TWM_TEST_HOME"));
	FILE *f = fopen(rc, "w");
	fprintf(f, "\"M-S-Up\"    : f.zoom\n\"M-S-Right\" : f.horizoom\n");
	fclose(f);
	snprintf(cmd, sizeof(cmd), "\"%s\" --reload >/dev/null", getenv("TWM_BIN"));
	tc_expect("ZOM-9", system(cmd) == 0, "bindings for f.zoom and f.horizoom load");
	tc_wait(tc, 200);

	/* A window at 300,200: its normal geometry. */
	tc_pointer_move(tc, 300, 200);
	struct tc_window *a = tc_window_create(tc, &(struct tc_window_opts){
		.title = "notes", .app_id = "notes", .width = 400, .height = 300, .color = BLUE});
	place_at(tc, 300, 200);
	tc_pointer_move(tc, 500, 350);
	tc_wait(tc, 150);

	/* --- ZOM-1: f.fullzoom (M-Up) fills the usable area, framed --- */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_UP);
	tc_wait(tc, 150);
	expect_size("ZOM-1", a, W - 2, H - 2 - TH, "M-Up: the client fills the output inside its frame");
	states(a, &max, &fs, &tiled);
	tc_expect("ZOM-5", max && !fs && tiled == 15, "and is told it's maximized, tiled all round (%d %d %d)",
		max, fs, tiled);
	shot(tc, &img, out, "fullzoom");
	tc_expect("ZOM-1", count_box(&img, 0, 0, 300, TH, ACCENT, 30) > 1000,
		"its tab is at the top-left corner");
	tc_expect("ZOM-1", is(&img, 1000, 600, BLUE) && is(&img, 1, H - 2, BLUE),
		"the client reaches the bottom-left, inside the border");

	/* --- ZOM-1: again, back where it was --- */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_UP);
	tc_wait(tc, 150);
	expect_size("ZOM-1", a, 400, 300, "M-Up again: back to its size");
	states(a, &max, &fs, &tiled);
	tc_expect("ZOM-5", !max && !fs && tiled == 0, "and told so (%d %d %d)", max, fs, tiled);
	shot(tc, &img, out, "normal");
	tc_expect("ZOM-1", near(tc_pixel(&img, 310, 213), ACCENT, 40) && is(&img, 500, 350, BLUE) &&
		!is(&img, 1000, 600, BLUE), "and back at 300,200");

	/* --- ZOM-1, ZOM-2: f.zoom is tall at its x; f.horizoom from there is wide at its y --- */
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_UP);
	tc_wait(tc, 150);
	expect_size("ZOM-1", a, 400, H - 2 - TH, "f.zoom: full height, same width");
	states(a, &max, &fs, &tiled);
	tc_expect("ZOM-5", !max && tiled == (4 | 8), "tiled top and bottom (%d)", tiled);
	shot(tc, &img, out, "zoom");
	tc_expect("ZOM-1", is(&img, 500, 1000, BLUE) && !is(&img, 250, 600, BLUE) &&
		near(tc_pixel(&img, 310, 13), ACCENT, 40), "at x 300, from the top");
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_RIGHT);
	tc_wait(tc, 150);
	expect_size("ZOM-2", a, W - 2, 300, "f.horizoom after f.zoom: wide at its normal height");
	states(a, &max, &fs, &tiled);
	tc_expect("ZOM-5", tiled == (1 | 2), "tiled left and right (%d)", tiled);
	shot(tc, &img, out, "horizoom");
	tc_expect("ZOM-2", is(&img, 1500, 350, BLUE) && !is(&img, 1500, 100, BLUE),
		"at its normal y (200), not the top");

	/* --- ZOM-7: undo takes a zoom back --- */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, KEY_Z);
	tc_wait(tc, 150);
	expect_size("ZOM-7", a, 400, H - 2 - TH, "M-z: back to f.zoom");
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_UP);
	tc_wait(tc, 150);
	expect_size("ZOM-1", a, 400, 300, "f.zoom again: normal");

	/* --- ZOM-3: f.fullscreen (M-S-f): the client alone, over everything --- */
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, KEY_F);
	tc_wait(tc, 150);
	expect_size("ZOM-3", a, W, H, "M-S-f: the whole output");
	states(a, &max, &fs, &tiled);
	tc_expect("ZOM-5", fs, "told it's fullscreen (%d)", fs);
	shot(tc, &img, out, "fullscreen");
	tc_expect("ZOM-3", is(&img, 0, 0, BLUE) && is(&img, W - 1, H - 1, BLUE) && is(&img, 10, 13, BLUE),
		"no tab, no border: blue from corner to corner");
	tc_expect("ZOM-3", count_box(&img, 1660, 0, W, 400, BLUE, 6) > 60000,
		"above every other window, the Icon Manager too");

	/* --- ZOM-4: another window takes the focus: the fullscreen one steps back --- */
	struct tc_window *b = tc_window_create(tc, &(struct tc_window_opts){
		.title = "other", .app_id = "other", .width = 300, .height = 200, .color = RED});
	place_at(tc, 800, 400);
	tc_wait(tc, 100);
	shot(tc, &img, out, "stepped-back");
	tc_expect("ZOM-4", is(&img, 900, 500, RED) && near(tc_pixel(&img, 810, 413), ACCENT, 40),
		"the new window, focused, shows above it");
	tc_expect("ZOM-4", is(&img, 0, 0, BLUE), "it stays fullscreen behind them");
	/* focus back (sloppy focus): it comes forward again */
	tc_pointer_move(tc, 300, 900);
	tc_wait(tc, 60);
	tc_pointer_move(tc, 305, 905);
	tc_wait(tc, 250);
	shot(tc, &img, out, "forward");
	tc_expect("ZOM-4", is(&img, 900, 500, BLUE), "focused again, it's in front (%06x)",
		tc_pixel(&img, 900, 500) & 0xffffff);

	/* --- ZOM-5: an app's own maximize request is f.fullzoom --- */
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, KEY_F);
	tc_wait(tc, 150);
	expect_size("ZOM-3", a, 400, 300, "M-S-f again: back to normal");
	tc_window_set_maximized(b, true);
	tc_wait(tc, 150);
	expect_size("ZOM-5", b, W - 2, H - 2 - TH, "the app asks to maximize: f.fullzoom");
	tc_window_set_maximized(b, false);
	tc_wait(tc, 150);
	expect_size("ZOM-5", b, 300, 200, "and to unmaximize: back");
	tc_window_set_fullscreen(b, true);
	tc_wait(tc, 150);
	expect_size("ZOM-5", b, W, H, "it asks for fullscreen (F11)");
	tc_window_set_fullscreen(b, false);
	tc_wait(tc, 150);
	expect_size("ZOM-5", b, 300, 200, "and leaves it");

	/* --- ZOM-5: a window that opens maximized skips click-to-place --- */
	tc_pointer_move(tc, 600, 600);
	struct tc_window *c = tc_window_create(tc, &(struct tc_window_opts){
		.title = "big", .app_id = "big", .width = 300, .height = 200, .color = GREEN,
		.start_maximized = true});
	tc_wait(tc, 200);
	expect_size("ZOM-5", c, W - 2, H - 2 - TH, "opened maximized, at once");
	shot(tc, &img, out, "open-maximized");
	tc_expect("ZOM-5", is(&img, 1000, 900, GREEN), "on screen without a placing click");
	tc_window_destroy(c);
	tc_window_destroy(b);
	tc_window_destroy(a);
	tc_wait(tc, 150);

	/* --- ZOM-6: fullscreen and capturing: the marker over it --- */
	struct helper h = helper_start();
	tc_pointer_move(tc, 400, 300);
	helper_do(tc, &h, 'w');
	tc_wait(tc, 80);
	tc_pointer_click(tc, TC_BTN_LEFT); /* place it */
	tc_wait(tc, 80);
	helper_do(tc, &h, 'f');
	tc_wait(tc, 150);
	shot(tc, &img, out, "fullscreen-quiet");
	tc_expect("ZOM-6", count_box(&img, 0, 0, W, 2, ALERT, 30) == 0 && is(&img, W - 40, 10, GREEN),
		"fullscreen, not capturing: no marker");
	helper_do(tc, &h, 'c');
	tc_wait(tc, 400);
	shot(tc, &img, out, "fullscreen-capture");
	tc_expect("ZOM-6", count_box(&img, 0, 0, W, 2, ALERT, 30) > 3500,
		"capturing: an alert line along the top edge");
	tc_expect("ZOM-6", count_box(&img, W - 140, 2, W, 2 + TH, ALERT, 30) > 20,
		"and SCREEN at the top-right corner");
	tc_expect("ZOM-6", is(&img, 960, 540, GREEN), "the rest is the window's");
	helper_quit(tc, &h);

	tc_disconnect(tc);
	return tc_report();
}
