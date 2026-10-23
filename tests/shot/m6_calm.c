/*
 * M6 screenshot test: polite urgency (URG-1 … URG-5), capture markers
 * (CAP-1 … CAP-4) and the trusted frame (TRU-1 … TRU-6), through real
 * protocols. Helpers run as child processes, so they are ordinary clients:
 * not the test program `--test` trusts.
 */
#include <errno.h>
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
#define SURFACE 0x121310
#define GRAPHITE 0x23251F
#define ACCENT 0xFFB000
#define ALERT 0xE5484D
#define TH 26
#define KEY_U 22

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

/* --- child processes: ordinary, untrusted clients --- */

/* A synthetic click from another client (TRU-4: must go nowhere). */
static void foreign_click(struct tc *tc, int x, int y) {
	pid_t pid = fork();
	if (pid == 0) {
		struct tc *t = tc_connect();
		tc_pointer_move(t, x, y);
		tc_pointer_click(t, TC_BTN_LEFT);
		tc_key(t, TC_KEY_ENTER, true);
		tc_key(t, TC_KEY_ENTER, false);
		tc_wait(t, 100);
		tc_disconnect(t);
		_exit(0);
	}
	for (int i = 0; i < 100; i++) {
		tc_wait(tc, 20);
		if (waitpid(pid, NULL, WNOHANG) == pid) {
			return;
		}
	}
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
}

/*
 * A capturing client. Commands over a pipe: 'w' make a window, 'c' start
 * capturing, 's' stop, 'q' quit. It answers each with one byte.
 */
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
					.title = "obs \xe2\x80\x94 scene 1", .app_id = "obs",
					.width = 498, .height = 272, .color = SURFACE});
			} else if (c == 'c') {
				capturing = true;
			} else if (c == 's') {
				capturing = false;
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

/* `twm --choose who` with `lines` on stdin. */
struct chooser {
	pid_t pid;
	int out;
};

static struct chooser choose_start(const char *who, const char *lines) {
	int in[2], out[2];
	struct chooser c = {-1, -1};
	if (pipe(in) < 0 || pipe(out) < 0) {
		return c;
	}
	c.pid = fork();
	if (c.pid == 0) {
		dup2(in[0], 0);
		dup2(out[1], 1);
		close(in[0]);
		close(in[1]);
		close(out[0]);
		close(out[1]);
		execl(getenv("TWM_BIN"), "twm", "--choose", who, (char *)NULL);
		_exit(127);
	}
	close(in[0]);
	close(out[1]);
	if (write(in[1], lines, strlen(lines)) < 0) {
		perror("write");
	}
	close(in[1]);
	c.out = out[0];
	return c;
}

/* Wait for the chooser; its stdout into buf; its exit status (-1: hung). */
static int choose_wait(struct tc *tc, struct chooser *c, char *buf, size_t len) {
	int status = -1;
	for (int i = 0; i < 150; i++) {
		tc_wait(tc, 20);
		int st;
		if (waitpid(c->pid, &st, WNOHANG) == c->pid) {
			status = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
			break;
		}
	}
	if (status < 0) {
		kill(c->pid, SIGKILL);
		waitpid(c->pid, NULL, 0);
	}
	ssize_t n = read(c->out, buf, len - 1);
	buf[n > 0 ? n : 0] = '\0';
	close(c->out);
	return status;
}

/* The prompt's frame: the accent box around the screen's centre. */
static bool find_prompt(const struct tc_image *img, int *x0, int *y0, int *x1, int *y1) {
	int cy = img->height / 2;
	*x0 = *x1 = -1;
	for (int x = 0; x < img->width; x++) {
		if (near(tc_pixel(img, x, cy), ACCENT, 2)) {
			if (*x0 < 0) {
				*x0 = x;
			}
			*x1 = x;
		}
	}
	if (*x0 < 0) {
		return false;
	}
	int cx = *x0 + 3;
	*y0 = *y1 = -1;
	for (int y = 0; y < img->height; y++) {
		if (near(tc_pixel(img, *x0, y), ACCENT, 2)) {
			if (*y0 < 0) {
				*y0 = y;
			}
			*y1 = y;
		}
	}
	(void)cx;
	return *y0 >= 0;
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "m6-calm.png";
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	char path[4096], buf[1024];

	char rc[4096];
	snprintf(rc, sizeof(rc), "%s/.twmrc", getenv("TWM_TEST_HOME"));
	FILE *f = fopen(rc, "w");
	fputs("Snap off\nQuiet { 2 }\n", f);
	fclose(f);
	tc_wait(tc, 400);

	struct tc_window *a = place(tc, 100, 150, (struct tc_window_opts){
		.title = "foot \xe2\x80\x94 build", .app_id = "foot", .width = 818, .height = 438,
		.color = SURFACE});
	tc_expect("setup", tc_window_activated(a), "build placed and focused");

	/* ===== TRU: the screen-share chooser ===== */
	struct chooser ch = choose_start("firefox \xc2\xb7 meet.example.org",
		"Monitor: HEADLESS-1\nWindow: foot \xe2\x80\x94 build\n");
	tc_wait(tc, 300);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-trusted.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	int px0, py0, px1, py1;
	bool found = find_prompt(&img, &px0, &py0, &px1, &py1);
	tc_expect("TRU-1", found, "a prompt in an accent frame (%d,%d)-(%d,%d)", px0, py0, px1, py1);
	tc_expect("TRU-1", !tc_window_activated(a), "it takes the focus");
	tc_expect_pixel("TRU-4", &img, 500, 300, tc_blend(GROUND, 0.35, SURFACE), 1); /* dimmed */
	tc_expect("TRU-1", count_box(&img, px0 + 1, py0 + TH + 1, px1, py0 + TH + 11, SURFACE, 3) == 0,
		"the weave band, not content, inside the accent border");
	int tab_end = px0;
	while (tab_end < px1 && (tc_pixel(&img, tab_end, py0 + 1) >> 16 & 0xff) > 0x80) {
		tab_end++;
	}
	tc_expect_pixel("TRU-2", &img, px0 + 7 + 6, py0 + 7 + 9, 0x0E0F0C, 2); /* the lock */
	tc_expect("TRU-2", count_box(&img, tab_end - 20, py0 + 4, tab_end, py0 + TH - 4,
		tc_blend(0x0E0F0C, 0.55, ACCENT), 2) > 5, "hatch to the tab's end: no resize box");

	/* TRU-4: another client's synthetic click and Return go nowhere. */
	foreign_click(tc, px1 - 40, py1 - 30);
	tc_wait(tc, 100);
	tc_expect("TRU-4", waitpid(ch.pid, NULL, WNOHANG) == 0,
		"synthetic input from another client does not answer it");

	/* Down, Return (the trusted test keyboard): the second line. */
	tc_key(tc, TC_KEY_DOWN, true);
	tc_key(tc, TC_KEY_DOWN, false);
	tc_wait(tc, 40);
	shot(tc, &img);
	tc_key(tc, TC_KEY_ENTER, true);
	tc_key(tc, TC_KEY_ENTER, false);
	int st = choose_wait(tc, &ch, buf, sizeof(buf));
	tc_expect("TRU-5", st == 0 && strcmp(buf, "Window: foot \xe2\x80\x94 build\n") == 0,
		"Down, Return: the chooser prints the second line (%d, \"%s\")", st, buf);
	tc_wait(tc, 40);
	tc_expect("TRU-4", tc_window_activated(a), "focus back on build");
	shot(tc, &img);
	tc_expect_pixel("TRU-4", &img, 500, 300, SURFACE, 0); /* the dim is gone */

	/* Esc denies: nothing printed, status 1. */
	ch = choose_start("", "Monitor: HEADLESS-1\n");
	tc_wait(tc, 200);
	tc_key(tc, TC_KEY_ESC, true);
	tc_key(tc, TC_KEY_ESC, false);
	st = choose_wait(tc, &ch, buf, sizeof(buf));
	tc_expect("TRU-5", st == 1 && buf[0] == '\0', "Esc denies (%d)", st);

	tc_image_free(&img);
	tc_window_destroy(a);
	tc_disconnect(tc);
	return tc_report();
}
