/*
 * Screenshot test, after M7: running installed apps from the Icon Manager
 * (IMG-13) and the close box (FRM-19): B1 asks a window to close, B3 kills
 * its client (twm's f.destroy). The killed client is a child process, an
 * ordinary client; the test's own connection must survive the test.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define SURFACE 0x121310
#define ACCENT 0xFFB000
#define ON_ACCENT 0x0E0F0C
#define TH 26
#define KEY_D 32
#define KEY_H 35
#define KEY_E 18
#define KEY_L 38
#define KEY_O 24

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

static void key(struct tc *tc, uint32_t k) {
	tc_key(tc, k, true);
	tc_key(tc, k, false);
	tc_wait(tc, 30);
}

/* The right end of a focused tab whose left end is at x0, row y. */
static int tab_end(const struct tc_image *img, int x0, int y) {
	int x = x0;
	while (x < img->width && (tc_pixel(img, x, y) >> 16 & 0xff) > 0x80) {
		x++;
	}
	return x;
}

static void place_at(struct tc *tc, int x, int y) {
	tc_pointer_move(tc, x, y);
	tc_wait(tc, 40);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "apps-close.png";
	const char *home = getenv("TWM_TEST_HOME");
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	char path[4096];

	/* An installed app (IMG-13), in the user's own applications. */
	char ran[4096];
	snprintf(ran, sizeof(ran), "%s/hello-ran", home);
	snprintf(path, sizeof(path), "%s/.local/share/applications", home);
	char cmd[4200];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
	if (system(cmd) != 0) {
		return 2;
	}
	snprintf(path, sizeof(path), "%s/.local/share/applications/hello.desktop", home);
	FILE *f = fopen(path, "w");
	fprintf(f, "[Desktop Entry]\nType=Application\nName=Hello Tool\nGenericName=Test App\n"
		"Exec=sh -c \"touch '%s'\"\n", ran);
	fclose(f);
	tc_wait(tc, 300);

	/* --- IMG-13: M-d, type its name, Return: it runs, not in a terminal --- */
	tc_pointer_move(tc, 900, 700);
	tc_chord(tc, 2, TC_KEY_LEFTMETA, KEY_D);
	tc_wait(tc, 60);
	key(tc, KEY_H);
	key(tc, KEY_E);
	key(tc, KEY_L);
	key(tc, KEY_L);
	key(tc, KEY_O);
	tc_wait(tc, 80);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-run.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect("IMG-13", count_box(&img, 1661, 100, 1895, 300, ACCENT, 40) > 40,
		"M-d: the Icon Manager filters, \"hello\" lit in its result");
	key(tc, TC_KEY_ENTER);
	int waited = 0;
	while (access(ran, F_OK) != 0 && waited < 3000) {
		tc_wait(tc, 50);
		waited += 50;
	}
	tc_expect("IMG-13", access(ran, F_OK) == 0, "Return ran Hello Tool's Exec (%d ms)", waited);

	/* --- FRM-19: the close box, before the resize box --- */
	tc_pointer_move(tc, 200, 200);
	struct tc_window *w = tc_window_create(tc, &(struct tc_window_opts){
		.title = "notes", .app_id = "notes", .width = 500, .height = 300, .color = SURFACE});
	place_at(tc, 200, 200);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-closebox.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	int end = tab_end(&img, 200, 201);
	int close_x = end - TH - 1 - TH; /* the close box's left edge */
	tc_expect("FRM-19", count_box(&img, close_x + 6, 200 + 6, close_x + 20, 200 + 20, ON_ACCENT, 30) > 10,
		"a cross in the box before the resize box (tab ends at %d)", end);
	tc_expect_pixel("FRM-19", &img, close_x - 1, 200 + 13, tc_blend(ON_ACCENT, 0.30, ACCENT), 3);
	/* B1: the window is asked to close (it decides: this one stays). */
	tc_pointer_move(tc, close_x + 13, 200 + 13);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 80);
	tc_expect("FRM-19", tc_window_closes(w) == 1, "B1 on it: asked to close (%d)", tc_window_closes(w));
	/* M-w does the same from the keyboard (P-8). */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, 17 /* KEY_W */);
	tc_wait(tc, 80);
	tc_expect("FRM-19", tc_window_closes(w) == 2, "M-w: asked again (%d)", tc_window_closes(w));

	/* B3: another client's window, killed (its connection cut). */
	pid_t child = fork();
	if (child == 0) {
		struct tc *t = tc_connect();
		tc_window_create(t, &(struct tc_window_opts){
			.title = "stuck", .app_id = "stuck", .width = 400, .height = 200, .color = SURFACE});
		for (int i = 0; i < 400 && tc_connected(t); i++) {
			tc_wait(t, 50);
		}
		_exit(tc_connected(t) ? 1 : 0);
	}
	tc_wait(tc, 300);
	place_at(tc, 900, 600);
	shot(tc, &img);
	int vend = tab_end(&img, 900, 601);
	int vclose = vend - TH - 1 - TH;
	tc_pointer_move(tc, vclose + 13, 600 + 13);
	tc_pointer_click(tc, TC_BTN_RIGHT);
	int status = -1;
	for (int i = 0; i < 60; i++) {
		tc_wait(tc, 50);
		if (waitpid(child, &status, WNOHANG) == child) {
			break;
		}
	}
	tc_expect("FRM-19", WIFEXITED(status) && WEXITSTATUS(status) == 0,
		"B3 on its close box: the client was cut off");
	tc_wait(tc, 60);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-killed.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect("FRM-19", !near(tc_pixel(&img, 1100, 700), SURFACE, 0),
		"its window is gone");
	tc_expect("FRM-19", tc_connected(tc) && tc_window_closes(w) == 2, "and ours is untouched");
	if (waitpid(child, NULL, WNOHANG) == 0) {
		kill(child, SIGKILL);
		waitpid(child, NULL, 0);
	}

	/* CloseBox off: B0's tab again, 27 px narrower. */
	snprintf(path, sizeof(path), "%s/.twmrc", home);
	f = fopen(path, "w");
	fputs("CloseBox off\n", f);
	fclose(f);
	tc_wait(tc, 600);
	shot(tc, &img);
	int end_off = tab_end(&img, 200, 201);
	tc_expect("FRM-19", end_off == end - TH - 1, "CloseBox off: the tab is %d px narrower",
		end - end_off);

	tc_image_free(&img);
	tc_window_destroy(w);
	tc_disconnect(tc);
	return tc_report();
}
