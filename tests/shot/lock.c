/*
 * Screenshot test, after M7: the lock (GRT-1 … GRT-9). Windows are hidden,
 * keys reach the lock alone, synthetic keys reach nothing, a wrong
 * password dashes the frame, the right one unlocks, urgency waits, idle
 * locks, and no screenshot is taken while locked. --test stands a fixed
 * password in for PAM (GRT-9).
 */
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "client.h"

#define ACCENT 0xFFB000
#define ALERT 0xFF8A65
#define BLUE 0x3060C0
#define RED 0xC03030
#define W 1920
#define H 1080

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

/* evdev codes for a password */
static int code_of(char ch) {
	static const char *const row = "qwertyuiop";
	static const char *const row2 = "asdfghjkl";
	static const char *const row3 = "zxcvbnm";
	const char *p;
	if ((p = strchr(row, ch))) {
		return 16 + (int)(p - row);
	}
	if ((p = strchr(row2, ch))) {
		return 30 + (int)(p - row2);
	}
	if ((p = strchr(row3, ch))) {
		return 44 + (int)(p - row3);
	}
	if (ch >= '1' && ch <= '9') {
		return 2 + ch - '1';
	}
	return 57; /* space */
}

static void type(struct tc *tc, const char *s) {
	for (; *s; s++) {
		tc_key(tc, (uint32_t)code_of(*s), true);
		tc_key(tc, (uint32_t)code_of(*s), false);
		tc_wait(tc, 15);
	}
	tc_key(tc, TC_KEY_ENTER, true);
	tc_key(tc, TC_KEY_ENTER, false);
	tc_wait(tc, 250);
}

/* The lock's frame: accent pixels in the middle of the screen. */
static int frame_px(const struct tc_image *img) {
	return count_box(img, 600, 300, 1320, 800, ACCENT, 30);
}

static bool locked(struct tc *tc, struct tc_image *img, const char *out, const char *name) {
	shot(tc, img, out, name);
	return frame_px(img) > 1500;
}

static int twm(const char *args) {
	char cmd[4200];
	snprintf(cmd, sizeof(cmd), "\"%s\" %s >/dev/null", getenv("TWM_BIN"), args);
	return WEXITSTATUS(system(cmd));
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "lock.png";
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	const char *pw = getenv("TWM_TEST_LOCK_PASSWORD");
	tc_expect("GRT-9", pw && *pw, "--test has a stand-in password for PAM");

	/* two windows: b (red) will ask for attention while locked */
	tc_pointer_move(tc, 1000, 300);
	struct tc_window *b = tc_window_create(tc, &(struct tc_window_opts){
		.title = "build", .app_id = "build", .width = 400, .height = 250, .color = RED});
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 100);
	tc_pointer_move(tc, 200, 200);
	struct tc_window *a = tc_window_create(tc, &(struct tc_window_opts){
		.title = "notes", .app_id = "notes", .width = 500, .height = 300, .color = BLUE});
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 150);

	/* --- GRT-7: M-l locks; every window is hidden, not dimmed --- */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, 38 /* KEY_L */);
	tc_wait(tc, 200);
	bool is_locked = locked(tc, &img, out, "locked");
	tc_expect("GRT-7", is_locked, "M-l: the trusted frame, centred (%d accent px)", frame_px(&img));
	tc_expect("GRT-7", count_box(&img, 0, 0, W, H, BLUE, 8) == 0 && count_box(&img, 0, 0, W, H, RED, 8) == 0,
		"no window shows: hidden, not dimmed");
	tc_expect("GRT-2", count_box(&img, 700, 200, 1220, 420, ACCENT, 30) > 100,
		"the LOCKED badge above the frame");

	/* --- GRT-7, TRU-4: synthetic keys reach nothing --- */
	pid_t child = fork();
	if (child == 0) {
		struct tc *t = tc_connect();
		type(t, pw);
		tc_disconnect(t);
		_exit(0);
	}
	waitpid(child, NULL, 0);
	tc_wait(tc, 200);
	tc_expect("GRT-7", locked(tc, &img, out, "synthetic"), "another client typing the password: still locked");

	/* --- SHOT-3: no shots while locked --- */
	tc_chord(tc, 1, 99 /* Print */);
	tc_wait(tc, 300);
	char pics[4096];
	snprintf(pics, sizeof(pics), "%s/Pictures/Screenshots", getenv("TWM_TEST_HOME"));
	DIR *d = opendir(pics);
	tc_expect("SHOT-3", d == NULL, "Print while locked: no screenshot");
	if (d) {
		closedir(d);
	}

	/* --- GRT-5: a wrong password dashes the frame and says so --- */
	type(tc, "nope");
	tc_wait(tc, 200);
	shot(tc, &img, out, "failed");
	tc_expect("GRT-5", count_box(&img, 600, 300, 1320, 800, ALERT, 30) > 30,
		"the failure in alert under the field");
	/* the frame's left edge, dashed: accent and not, along it */
	int on = 0, off = 0, fx = -1;
	for (int x = 600; x < 960 && fx < 0; x++) {
		if (near(tc_pixel(&img, x, 540), ACCENT, 30)) {
			fx = x;
		}
	}
	for (int y = 420; y < 700 && fx >= 0; y++) {
		near(tc_pixel(&img, fx, y), ACCENT, 30) ? on++ : off++;
	}
	tc_expect("GRT-5", fx >= 0 && on > 20 && off > 20, "the frame goes dashed (edge at %d: %d on, %d off)",
		fx, on, off);
	tc_expect("GRT-5", count_box(&img, 0, 0, W, H, BLUE, 8) == 0, "and nothing else moves");

	/* --- GRT-7: urgency waits for the unlock, and the root line says so --- */
	tc_window_request_activation(b);
	tc_wait(tc, 1200); /* the lock redraws once a second */
	shot(tc, &img, out, "held");
	tc_expect("GRT-7", count_box(&img, 700, H - 100, 1220, H - 40, ACCENT, 30) > 10,
		"M-u … urgent held until unlock, on the bottom line");

	/* --- GRT-6: the right password unlocks --- */
	type(tc, pw);
	tc_wait(tc, 200);
	shot(tc, &img, out, "unlocked");
	tc_expect("GRT-6", frame_px(&img) < 1500 || count_box(&img, 0, 0, W, H, BLUE, 8) > 100000,
		"the right password: the lock goes");
	tc_expect("GRT-6", count_box(&img, 0, 0, W, H, BLUE, 8) > 100000,
		"and the windows come back");
	tc_expect("GRT-7", tc_window_activated(a), "focus back where it was");

	/* --- GRT-7: twm --lock, and idle --- */
	tc_expect("GRT-7", twm("--lock") == 0, "twm --lock answers locked");
	tc_wait(tc, 200);
	tc_expect("GRT-7", locked(tc, &img, out, "ipc"), "and locks");
	type(tc, pw);
	tc_wait(tc, 200);
	char rc[4096];
	snprintf(rc, sizeof(rc), "%s/.twmrc", getenv("TWM_TEST_HOME"));
	FILE *f = fopen(rc, "w");
	fputs("IdleLock 2\n", f);
	fclose(f);
	tc_expect("GRT-7", twm("--reload") == 0, "IdleLock 2 loads");
	tc_wait(tc, 2800); /* no input meanwhile */
	tc_expect("GRT-7", locked(tc, &img, out, "idle"), "two idle seconds lock it");
	type(tc, pw);
	tc_wait(tc, 200);
	shot(tc, &img, out, "end");
	tc_expect("GRT-7", count_box(&img, 0, 0, W, H, BLUE, 8) > 100000, "unlocked again");
	f = fopen(rc, "w");
	fputs("IdleLock 0\n", f);
	fclose(f);
	twm("--reload");

	tc_window_destroy(a);
	tc_window_destroy(b);
	tc_disconnect(tc);
	return tc_report();
}
