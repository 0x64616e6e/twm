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
#include "config.h"
#if HAVE_PORTAL
#include SDBUS_HEADER
#endif

#define GROUND 0x0D0E0B
#define SURFACE 0x121310
#define GRAPHITE 0x23251F
#define ACCENT 0xFFB000
#define ALERT 0xFF8A65
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

#if HAVE_PORTAL
/* --- D-Bus: the Access portal backend and notifications --- */

struct reply {
	bool done;
	uint32_t response;
	char results[256]; /* "id=value;…" */
};

static int on_access_reply(sd_bus_message *m, void *data, sd_bus_error *err) {
	struct reply *r = data;
	r->done = true;
	if (sd_bus_message_is_method_error(m, NULL)) {
		r->response = 99;
		return 0;
	}
	sd_bus_message_read(m, "u", &r->response);
	if (sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) {
		return 0;
	}
	while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
		const char *key;
		sd_bus_message_read(m, "s", &key);
		if (strcmp(key, "choices") == 0 &&
				sd_bus_message_enter_container(m, 'v', "a(ss)") > 0) {
			const char *id, *val;
			sd_bus_message_enter_container(m, 'a', "(ss)");
			while (sd_bus_message_read(m, "(ss)", &id, &val) > 0) {
				size_t n = strlen(r->results);
				snprintf(r->results + n, sizeof(r->results) - n, "%s=%s;", id, val);
			}
			sd_bus_message_exit_container(m);
			sd_bus_message_exit_container(m);
		} else {
			sd_bus_message_skip(m, "v");
		}
		sd_bus_message_exit_container(m);
	}
	return 0;
}

static void bus_wait(struct tc *tc, sd_bus *bus, int ms) {
	for (int t = 0; t < ms; t += 10) {
		while (sd_bus_process(bus, NULL) > 0) {
		}
		tc_wait(tc, 10);
	}
}

/* AccessDialog with one option choice and one boolean choice. */
static sd_bus_slot *access_call(sd_bus *bus, const char *handle, struct reply *r) {
	sd_bus_message *m = NULL;
	sd_bus_slot *slot = NULL;
	sd_bus_message_new_method_call(bus, &m, "org.freedesktop.impl.portal.desktop.twm",
		"/org/freedesktop/portal/desktop", "org.freedesktop.impl.portal.Access",
		"AccessDialog");
	sd_bus_message_append(m, "osssss", handle, "org.mozilla.firefox", "",
		"Allow Firefox to use the camera?", "meet.example.org wants to see you", "");
	sd_bus_message_open_container(m, 'a', "{sv}");
	sd_bus_message_append(m, "{sv}", "grant_label", "s", "Allow");
	sd_bus_message_open_container(m, 'e', "sv");
	sd_bus_message_append(m, "s", "choices");
	sd_bus_message_open_container(m, 'v', "a(ssa(ss)s)");
	sd_bus_message_open_container(m, 'a', "(ssa(ss)s)");
	sd_bus_message_append(m, "(ssa(ss)s)", "device", "DEVICE", 2, "cam", "Built-in camera",
		"usb", "USB camera", "cam");
	sd_bus_message_append(m, "(ssa(ss)s)", "remember", "Remember for this site", 0, "false");
	sd_bus_message_close_container(m);
	sd_bus_message_close_container(m);
	sd_bus_message_close_container(m);
	sd_bus_message_close_container(m);
	sd_bus_call_async(bus, &slot, m, on_access_reply, r, 0);
	sd_bus_message_unref(m);
	return slot;
}

static void key(struct tc *tc, uint32_t k) {
	tc_key(tc, k, true);
	tc_key(tc, k, false);
	tc_wait(tc, 30);
}

static void portal_checks(struct tc *tc, const char *out, struct tc_window *a) {
	sd_bus *bus = NULL;
	if (sd_bus_open_user(&bus) < 0) {
		printf("skip PORTAL   no session bus (dbus-run-session missing)\n");
		return;
	}
	struct tc_image img = {0};
	char path[4096];

	/* Access: a camera prompt; Down picks the USB camera, space ticks
	 * "remember", Return allows. */
	struct reply r = {0};
	sd_bus_slot *slot = access_call(bus, "/org/test/request/1", &r);
	bus_wait(tc, bus, 300);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-access.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	int x0, y0, x1, y1;
	tc_expect("TRU-1", find_prompt(&img, &x0, &y0, &x1, &y1) && !tc_window_activated(a),
		"AccessDialog opens a trusted prompt");
	key(tc, TC_KEY_DOWN);
	key(tc, 57); /* space */
	key(tc, TC_KEY_ENTER);
	bus_wait(tc, bus, 200);
	tc_expect("TRU-5", r.done && r.response == 0 &&
		strcmp(r.results, "device=usb;remember=true;") == 0,
		"Allow answers 0 with the choices (%u, \"%s\")", r.response, r.results);
	sd_bus_slot_unref(slot);

	/* Request.Close withdraws it: answered, and focus comes back. */
	struct reply r2 = {0};
	slot = access_call(bus, "/org/test/request/2", &r2);
	bus_wait(tc, bus, 200);
	tc_expect("TRU-4", !tc_window_activated(a), "a second prompt holds the focus");
	sd_bus_call_method(bus, "org.freedesktop.impl.portal.desktop.twm", "/org/test/request/2",
		"org.freedesktop.impl.portal.Request", "Close", NULL, NULL, "");
	bus_wait(tc, bus, 200);
	tc_expect("TRU-4", r2.done && r2.response != 0 && tc_window_activated(a),
		"Request.Close: the prompt goes, the call is answered (%u)", r2.response);
	sd_bus_slot_unref(slot);

	/* Notifications: one root line, never a toast (ROOT-7). */
	shot(tc, &img);
	uint32_t before = 0;
	for (int y = 980; y < 1040; y++) {
		for (int x = 980; x < 1600; x++) {
			before += near(tc_pixel(&img, x, y), 0xC9CBBF, 60);
		}
	}
	sd_bus_message *reply = NULL;
	uint32_t id = 0;
	int ok = sd_bus_call_method(bus, "org.freedesktop.Notifications",
		"/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify",
		NULL, &reply, "susssasa{sv}i", "make", 0, "", "build finished",
		"twm compiled in 41 s", 0, 0, -1);
	if (ok >= 0) {
		sd_bus_message_read(reply, "u", &id);
	}
	sd_bus_message_unref(reply);
	bus_wait(tc, bus, 100);
	shot(tc, &img);
	uint32_t after = 0;
	for (int y = 980; y < 1040; y++) {
		for (int x = 980; x < 1600; x++) {
			after += near(tc_pixel(&img, x, y), 0xC9CBBF, 60);
		}
	}
	snprintf(path, sizeof(path), "%.*s-notify.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect("ROOT-7", ok >= 0 && id > 0 && after > before + 100,
		"Notify: id %u, a root line appears (%u -> %u lit px)", id, before, after);
	tc_image_free(&img);
	sd_bus_flush_close_unref(bus);
}
#endif

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

	/* ===== URG: urgency is polite ===== */
	struct tc_window *b = place(tc, 1000, 620, (struct tc_window_opts){
		.title = "irc â #twm", .app_id = "irc", .width = 498, .height = 272,
		.color = SURFACE});
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_2); /* to quiet desktop 2 */
	tc_wait(tc, 40);
	/* and a second one there, focused later, so a visit doesn't focus irc */
	struct tc_window *d = place(tc, 1000, 620, (struct tc_window_opts){
		.title = "notes", .app_id = "notes", .width = 298, .height = 172, .color = SURFACE});
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_2);
	tc_wait(tc, 40);
	struct tc_window *c = place(tc, 1100, 150, (struct tc_window_opts){
		.title = "mail", .app_id = "mail", .width = 398, .height = 200, .color = SURFACE});
	tc_pointer_move(tc, 500, 400);
	tc_pointer_click(tc, TC_BTN_LEFT); /* build again */
	tc_wait(tc, 40);
	tc_expect("setup", tc_window_activated(a), "build focused, irc on desktop 2, mail here");

	/* URG-3: on a quiet desktop, asking is held. */
	tc_window_request_activation(b);
	tc_wait(tc, 60);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-quiet.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_chord(tc, 2, TC_KEY_LEFTMETA, KEY_U);
	tc_wait(tc, 60);
	tc_expect("URG-3", tc_window_activated(a) && !tc_window_activated(b),
		"held urgency: M-u has nowhere to go, build keeps the focus");

	/* URG-1, URG-2: mail asks; it does not get the focus, its frame shows it. */
	tc_window_request_activation(c);
	tc_wait(tc, 60);
	shot(tc, &img);
	tc_expect("URG-1", tc_window_activated(a) && !tc_window_activated(c),
		"asking does not take the focus");
	tc_expect("URG-2", count_box(&img, 1100, 150, 1300, 151, ACCENT, 2) > 40,
		"mail's tab is outlined in accent (urgent)");

	/* URG-4: M-u goes to mail; M-u again comes back. */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, KEY_U);
	tc_wait(tc, 60);
	tc_expect("URG-4", tc_window_activated(c), "M-u: mail");
	tc_chord(tc, 2, TC_KEY_LEFTMETA, KEY_U);
	tc_wait(tc, 60);
	tc_expect("URG-4", tc_window_activated(a), "M-u again: back to build");

	/* URG-3: visiting desktop 2 lets irc's urgency through. */
	tc_pointer_move(tc, 960, 1060); /* the root, nothing under the pointer */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_1 + 1);
	tc_wait(tc, 60);
	tc_expect("URG-3", tc_window_activated(d) && !tc_window_activated(b),
		"desktop 2: notes has the focus, irc is urgent now");
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_1);
	tc_wait(tc, 60);
	tc_chord(tc, 2, TC_KEY_LEFTMETA, KEY_U);
	tc_wait(tc, 60);
	tc_expect("URG-3", tc_window_activated(b), "after a visit, M-u finds irc");
	tc_chord(tc, 2, TC_KEY_LEFTMETA, KEY_U);
	tc_wait(tc, 60);
	tc_window_destroy(b);
	tc_window_destroy(c);
	tc_window_destroy(d);
	tc_wait(tc, 60);
	tc_pointer_move(tc, 500, 400);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 40);

	/* ===== CAP: capture is always visible ===== */
	struct helper h = helper_start();
	tc_pointer_move(tc, 300, 650);
	helper_do(tc, &h, 'w');
	tc_wait(tc, 60);
	tc_pointer_click(tc, TC_BTN_LEFT); /* place obs */
	tc_wait(tc, 60);
	helper_do(tc, &h, 'c');
	tc_wait(tc, 400);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-capture.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	int marked = count_box(&img, 300, 650, 800, 650 + TH, ALERT, 30);
	tc_expect("CAP-1", marked > 20, "obs captures: SCREEN in its tab (%d alert px)", marked);
	helper_do(tc, &h, 's');
	tc_wait(tc, 2800);
	shot(tc, &img);
	marked = count_box(&img, 300, 650, 800, 650 + TH, ALERT, 30);
	tc_expect("CAP-1", marked == 0, "stopped: the marker goes (%d)", marked);
	helper_quit(tc, &h);

	/* CAP-3: capturing without a window: a root line while it lasts. */
	h = helper_start();
	helper_do(tc, &h, 'c');
	tc_wait(tc, 400);
	shot(tc, &img);
	int lit = count_box(&img, 980, 980, 1700, 1040, ACCENT, 20);
	snprintf(path, sizeof(path), "%.*s-capture-root.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	helper_do(tc, &h, 's');
	tc_wait(tc, 2800);
	shot(tc, &img);
	int lit2 = count_box(&img, 980, 980, 1700, 1040, ACCENT, 20);
	tc_expect("CAP-3", lit > 40 && lit2 == 0, "a root line while it captures, gone after (%d -> %d)",
		lit, lit2);
	helper_quit(tc, &h);

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

#if HAVE_PORTAL
	portal_checks(tc, out, a);
#endif

	tc_image_free(&img);
	tc_window_destroy(a);
	tc_disconnect(tc);
	return tc_report();
}
