/*
 * M2 screenshot test: the config end to end. twm starts with no ~/.twmrc
 * (built-in defaults); the test writes one and checks that the file watcher
 * applies it, that a broken one is refused with the old one kept, that
 * `twm --reload` and `twm --check` answer, that the root line reports each
 * step, and that a window comes back where it was.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define GRAPHITE 0x23251F
#define SURFACE 0x121310
#define AMBER 0xFFB000
#define MINT 0x7FD1AE
#define CORAL 0xFF8A65

/* ROOT-5: the root line sits above the status line, from 51 % + 10 px. */
#define RL_X0 999
#define RL_X1 1800
#define RL_Y0 984
#define RL_Y1 1037

static char rc_path[4096];

static int near(uint32_t a, uint32_t b, int tol) {
	for (int s = 0; s <= 16; s += 8) {
		int d = (int)((a >> s) & 0xff) - (int)((b >> s) & 0xff);
		if (d < -tol || d > tol) {
			return 0;
		}
	}
	return 1;
}

static int count_color(const struct tc_image *img, int x0, int y0, int x1, int y1,
		uint32_t c, int tol) {
	int n = 0;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			n += near(tc_pixel(img, x, y), c, tol);
		}
	}
	return n;
}

/* Text on the root line: anything clearly brighter than the wordmark. */
static int count_bright(const struct tc_image *img) {
	int n = 0;
	for (int y = RL_Y0; y < RL_Y1; y++) {
		for (int x = RL_X0; x < RL_X1; x++) {
			uint32_t p = tc_pixel(img, x, y);
			int lum = (int)(((p >> 16) & 0xff) + ((p >> 8) & 0xff) + (p & 0xff)) / 3;
			n += lum > 0x60;
		}
	}
	return n;
}

static void write_rc(const char *text) {
	char tmp[4200];
	snprintf(tmp, sizeof(tmp), "%s.new", rc_path);
	FILE *f = fopen(tmp, "w");
	fputs(text, f);
	fclose(f);
	rename(tmp, rc_path); /* the way most editors save */
}

/* Run a twm command line; capture its output; return its exit status. */
static int run(const char *args, char *out, size_t len) {
	char cmd[8192];
	snprintf(cmd, sizeof(cmd), "'%s' %s 2>&1", getenv("TWM_BIN"), args);
	FILE *p = popen(cmd, "r");
	size_t n = fread(out, 1, len - 1, p);
	out[n] = '\0';
	int status = pclose(p);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static struct tc_window *place(struct tc *tc, int x, int y, struct tc_window_opts opts) {
	tc_pointer_move(tc, x, y);
	struct tc_window *win = tc_window_create(tc, &opts);
	tc_wait(tc, 20);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 30);
	return win;
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "m2-config.png";
	snprintf(rc_path, sizeof(rc_path), "%s/.twmrc", getenv("TWM_TEST_HOME"));
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	char buf[8192];

	/* CFG-1: no file, built-in defaults, and a root line saying so. */
	tc_screenshot(tc, &img);
	tc_expect("CFG-1", count_bright(&img) > 100, "root line: no ~/.twmrc, defaults");
	tc_image_free(&img);

	struct tc_window_opts geo = {
		.title = "geo", .app_id = "geo", .width = 400, .height = 300, .color = SURFACE,
	};
	struct tc_window *win = place(tc, 200, 150, geo);
	tc_pointer_move(tc, 1700, 300);
	tc_screenshot(tc, &img);
	tc_expect_pixel("§5", &img, 202, 152, AMBER, 2);
	tc_image_free(&img);

	/* CFG-5: the watcher applies a new file; accent, title height, border. */
	write_rc("# written by shot-m2_config\n"
		"Accent       #7FD1AE\n"
		"TitleHeight  30\n"
		"BorderWidth  2\n");
	tc_wait(tc, 400);
	tc_screenshot(tc, &img);
	tc_expect_pixel("CFG-5", &img, 202, 152, MINT, 2);
	tc_expect_pixel("CFG-5", &img, 202, 150 + 29, MINT, 2);       /* 30 px tab */
	tc_expect_pixel("CFG-5", &img, 200, 200, MINT, 2);            /* 2 px border */
	tc_expect_pixel("CFG-5", &img, 201, 200, MINT, 2);
	tc_expect_pixel("CFG-5", &img, 202, 200, SURFACE, 0);
	tc_expect_pixel("CFG-5", &img, 300, 150 + 30 + 2, SURFACE, 0); /* client below */
	tc_expect("CFG-6", count_bright(&img) > 150, "root line: reloaded");
	tc_image_free(&img);

	/* CFG-6 (B7): a broken file is refused; the old config stays. */
	write_rc("Accent       #8AB4FF\n"
		"SqueezTitle  { \"*\" left }\n");
	tc_wait(tc, 400);
	tc_screenshot(tc, &img);
	tc_expect_pixel("CFG-7", &img, 202, 152, MINT, 2);
	tc_expect_pixel("CFG-7", &img, 202, 150 + 29, MINT, 2);
	tc_expect("CFG-6", count_color(&img, RL_X0, RL_Y0, RL_X1, RL_Y1, MINT, 40) > 40,
		"the error is on the root line, in accent (%d px)",
		count_color(&img, RL_X0, RL_Y0, RL_X1, RL_Y1, MINT, 40));
	tc_image_write_png(&img, out);
	tc_image_free(&img);

	/* CFG-12: twm --reload reports the same error and keeps the config. */
	int status = run("--reload", buf, sizeof(buf));
	tc_expect("CFG-12", status == 1 && strstr(buf, ":2: unknown key SqueezTitle") &&
		strstr(buf, "previous config kept"), "twm --reload refuses it (exit %d)", status);

	/* CFG-4: twm --check on the same file. */
	status = run("--check \"$HOME/.twmrc\"", buf, sizeof(buf));
	tc_expect("CFG-4", status == 1 && strstr(buf, ".twmrc:2: unknown key SqueezTitle"),
		"twm --check names file and line (exit %d)", status);

	/* CFG-12: fixed file, reloaded by command. */
	write_rc("Accent #FF8A65\nOpaqueMove on\nSnap off\n");
	status = run("--reload", buf, sizeof(buf));
	tc_expect("CFG-12", status == 0 && strstr(buf, "config ok \xc2\xb7 0 warnings \xc2\xb7 reloaded in"),
		"twm --reload: %.*s", (int)strcspn(buf, "\n"), buf);
	status = run("--check \"$HOME/.twmrc\"", buf, sizeof(buf));
	tc_expect("CFG-4", status == 0 && strncmp(buf, "config ok \xc2\xb7 0 warnings", 22) == 0,
		"twm --check: %.*s", (int)strcspn(buf, "\n"), buf);
	tc_wait(tc, 300);
	tc_screenshot(tc, &img);
	tc_expect_pixel("CFG-12", &img, 202, 152, CORAL, 2);
	tc_expect_pixel("CFG-5", &img, 202, 150 + 26, CORAL, 2);  /* back to defaults: */
	tc_expect_pixel("CFG-5", &img, 202, 150 + 27, SURFACE, 0); /* 26 px tab, 1 px border */
	tc_image_free(&img);

	/* OUT-11: OpaqueMove on moves the window itself while dragging. */
	tc_pointer_move(tc, 300, 163);
	tc_pointer_button(tc, TC_BTN_MIDDLE, true);
	for (int i = 1; i <= 5; i++) {
		tc_pointer_move(tc, 300 + 10 * i, 163 + 8 * i);
	}
	tc_screenshot(tc, &img);
	tc_expect_pixel("OUT-11", &img, 252, 192, CORAL, 2);     /* tab moved +50,+40 */
	tc_expect_pixel("OUT-11", &img, 202, 152, GROUND, 8);   /* old place empty */
	tc_image_free(&img);
	tc_pointer_button(tc, TC_BTN_MIDDLE, false);
	tc_wait(tc, 50);

	/* CFG-11, OUT-16: the window comes back where it was, unplaced. */
	tc_window_destroy(win);
	tc_wait(tc, 1300); /* written at most once a second */
	char path[4200];
	snprintf(path, sizeof(path), "%s/twm/geometry", getenv("XDG_STATE_HOME"));
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
	buf[n] = '\0';
	if (f) {
		fclose(f);
	}
	tc_expect("CFG-11", strstr(buf, "geo\tHEADLESS-1\t250 190 400 300\n") != NULL,
		"geometry file: %.*s", (int)strcspn(buf, "\n"), buf);

	tc_pointer_move(tc, 1700, 900);
	struct tc_image before = {0};
	tc_screenshot(tc, &before);
	win = tc_window_create(tc, &geo);
	tc_wait(tc, 100);
	tc_screenshot(tc, &img);
	tc_expect_pixel("OUT-16", &img, 252, 192, GRAPHITE, 2);
	tc_expect("FOC-9", !tc_window_activated(win), "a restored window doesn't take focus");
	int changed = 0;
	for (int y = RL_Y0; y < RL_Y1; y++) {
		for (int x = RL_X0; x < RL_X1; x++) {
			changed += tc_pixel(&img, x, y) != tc_pixel(&before, x, y);
		}
	}
	tc_expect("OUT-16", changed > 200, "a new root line for the restore (%d px changed)", changed);
	tc_image_free(&before);
	tc_image_free(&img);

	/* FOC-7: FocusPolicy click; resting doesn't focus, a click does. */
	write_rc("Accent #FF8A65\nFocusPolicy click\nSnap off\n");
	tc_wait(tc, 400);
	tc_pointer_move(tc, 400, 400);
	tc_wait(tc, 200);
	tc_expect("FOC-7", !tc_window_activated(win), "click policy: hovering doesn't focus");
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 50);
	tc_expect("FOC-7", tc_window_activated(win), "click policy: a click focuses");

	tc_window_destroy(win);
	tc_disconnect(tc);
	return tc_report();
}
