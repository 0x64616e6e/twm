/*
 * Screenshot test, after M7: twm --greeter (GRT-1 … GRT-8), against a
 * stand-in greetd on $GREETD_SOCK: the sign-in screen, greetd's questions,
 * a failure, another user, another session, and the session it starts.
 * twm runs with --greeter, TWM_TEST_GREETER_USERS="tom ana" and the
 * sessions in tests/data/greeter (meson sets them).
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "client.h"

#define ACCENT 0xFFB000
#define ALERT 0xFF8A65
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

/* --- a stand-in greetd --- */

static int lfd = -1, cfd = -1;

/* The next message from the greeter (accepting a connection if need be);
 * empty if none came in `ms`. */
static void next_msg(struct tc *tc, char *out, size_t len, int ms) {
	out[0] = '\0';
	for (int waited = 0; waited < ms; waited += 10) {
		if (cfd < 0) {
			cfd = accept(lfd, NULL, NULL);
			if (cfd >= 0) {
				fcntl(cfd, F_SETFL, 0);
			}
		}
		if (cfd >= 0) {
			struct pollfd p = {.fd = cfd, .events = POLLIN};
			if (poll(&p, 1, 0) > 0) {
				uint32_t n;
				ssize_t r = read(cfd, &n, 4);
				if (r != 4) {
					close(cfd); /* the greeter hung up (a cancelled session) */
					cfd = -1;
					continue;
				}
				size_t got = 0;
				while (got < n && got < len - 1) {
					r = read(cfd, out + got, n - got);
					if (r <= 0) {
						break;
					}
					got += (size_t)r;
				}
				out[got] = '\0';
				return;
			}
		}
		tc_wait(tc, 10);
	}
}

static void reply(const char *json) {
	uint32_t n = (uint32_t)strlen(json);
	if (cfd < 0 || write(cfd, &n, 4) != 4 || write(cfd, json, n) != (ssize_t)n) {
		perror("greetd reply");
	}
}

/* Messages until one that has `want` in it (cancels and hangups go by). */
static bool expect_msg(struct tc *tc, const char *want, char *buf, size_t len) {
	for (int i = 0; i < 6; i++) {
		next_msg(tc, buf, len, 1500);
		if (!buf[0]) {
			return false;
		}
		if (strstr(buf, want)) {
			return true;
		}
	}
	return false;
}

static void type(struct tc *tc, const char *s) {
	static const char *const row = "qwertyuiop", *const row2 = "asdfghjkl", *const row3 = "zxcvbnm";
	for (; *s; s++) {
		const char *p;
		int code = 57;
		if ((p = strchr(row, *s))) {
			code = 16 + (int)(p - row);
		} else if ((p = strchr(row2, *s))) {
			code = 30 + (int)(p - row2);
		} else if ((p = strchr(row3, *s))) {
			code = 44 + (int)(p - row3);
		} else if (*s >= '1' && *s <= '9') {
			code = 2 + *s - '1';
		}
		tc_key(tc, (uint32_t)code, true);
		tc_key(tc, (uint32_t)code, false);
		tc_wait(tc, 15);
	}
}

static void key(struct tc *tc, uint32_t k) {
	tc_key(tc, k, true);
	tc_key(tc, k, false);
	tc_wait(tc, 80);
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "greeter.png";
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	char msg[4096];

	const char *sock = getenv("GREETD_SOCK");
	lfd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock ? sock : "");
	unlink(addr.sun_path);
	tc_expect("GRT-1", sock && bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
		listen(lfd, 4) == 0, "a stand-in greetd on $GREETD_SOCK");
	fcntl(lfd, F_SETFL, O_NONBLOCK);
	tc_wait(tc, 200);

	/* --- GRT-2, GRT-3: the sign-in screen --- */
	shot(tc, &img, out, "start");
	tc_expect("GRT-2", count_box(&img, 600, 250, 1320, 850, ACCENT, 30) > 1500,
		"the trusted frame, centred");
	tc_expect("GRT-3", count_box(&img, 700, 330, 1220, 420, ACCENT, 40) > 40,
		"the first user chosen: accent square and edge");
	tc_expect("GRT-1", count_box(&img, 0, 0, W, H, 0x23251F, 2) < 200000,
		"no Icon Manager or Panner: only the greeter");

	/* --- GRT-6: Return starts the conversation; greetd asks --- */
	key(tc, TC_KEY_ENTER);
	tc_expect("GRT-6", expect_msg(tc, "\"create_session\"", msg, sizeof(msg)) &&
		strstr(msg, "\"username\":\"tom\""), "create_session for tom: %.80s", msg);
	/* GRT-11: pam_fprintd first; it times out and PAM asks for the password
	 * with nothing typed: the greeter begins again, so the reader listens */
	reply("{\"type\":\"auth_message\",\"auth_message_type\":\"info\","
		"\"auth_message\":\"Place your right index finger on the fingerprint reader\"}");
	next_msg(tc, msg, sizeof(msg), 500); /* the empty answer to the info */
	reply("{\"type\":\"auth_message\",\"auth_message_type\":\"secret\",\"auth_message\":\"Password:\"}");
	tc_expect("GRT-11", expect_msg(tc, "\"create_session\"", msg, sizeof(msg)),
		"the reader timed out, nothing typed: a new conversation, the reader again");
	reply("{\"type\":\"auth_message\",\"auth_message_type\":\"info\","
		"\"auth_message\":\"Place your right index finger on the fingerprint reader\"}");
	next_msg(tc, msg, sizeof(msg), 500);

	/* --- GRT-11, GRT-5: typed while the reader listens, sent when PAM asks --- */
	type(tc, "wrong");
	key(tc, TC_KEY_ENTER);
	next_msg(tc, msg, sizeof(msg), 300);
	tc_expect("GRT-11", msg[0] == '\0', "nothing goes while the reader still listens (%.40s)", msg);
	reply("{\"type\":\"auth_message\",\"auth_message_type\":\"secret\",\"auth_message\":\"Password:\"}");
	tc_expect("GRT-6", expect_msg(tc, "post_auth_message_response", msg, sizeof(msg)) &&
		strstr(msg, "\"response\":\"wrong\""), "the moment PAM asks, the typed answer goes");
	reply("{\"type\":\"error\",\"error_type\":\"auth_error\",\"description\":\"Authentication failed\"}");
	tc_expect("GRT-5", expect_msg(tc, "\"create_session\"", msg, sizeof(msg)),
		"and after the failure, a new conversation");
	reply("{\"type\":\"auth_message\",\"auth_message_type\":\"secret\",\"auth_message\":\"Password:\"}");
	tc_wait(tc, 250);
	shot(tc, &img, out, "failed");
	tc_expect("GRT-5", count_box(&img, 600, 250, 1320, 850, ALERT, 30) > 30, "the failure, in alert");

	/* --- GRT-3: another user, another session --- */
	key(tc, TC_KEY_DOWN);
	tc_expect("GRT-3", expect_msg(tc, "\"username\":\"ana\"", msg, sizeof(msg)), "↓: ana's conversation");
	reply("{\"type\":\"auth_message\",\"auth_message_type\":\"secret\",\"auth_message\":\"Password:\"}");
	tc_wait(tc, 150);
	shot(tc, &img, out, "ana");
	key(tc, TC_KEY_UP);
	tc_expect("GRT-3", expect_msg(tc, "\"username\":\"tom\"", msg, sizeof(msg)), "↑: back to tom");
	reply("{\"type\":\"auth_message\",\"auth_message_type\":\"secret\",\"auth_message\":\"Password:\"}");
	tc_wait(tc, 150);
	key(tc, TC_KEY_TAB); /* Tom's WM -> Sway */
	shot(tc, &img, out, "sway");
	key(tc, TC_KEY_TAB); /* and back */

	/* --- GRT-8: the right one: greetd starts the chosen session --- */
	type(tc, "hunter2");
	key(tc, TC_KEY_ENTER);
	tc_expect("GRT-6", expect_msg(tc, "\"response\":\"hunter2\"", msg, sizeof(msg)), "the password goes");
	reply("{\"type\":\"success\"}");
	tc_expect("GRT-8", expect_msg(tc, "start_session", msg, sizeof(msg)) &&
		strstr(msg, "\"cmd\":[\"twm-session\"]") && strstr(msg, "XDG_SESSION_DESKTOP=twm") &&
		strstr(msg, "XDG_CURRENT_DESKTOP=twm"), "start_session: Tom's WM (%.120s)", msg);
	reply("{\"type\":\"success\"}");
	tc_wait(tc, 300);
	shot(tc, &img, out, "started");

	/* GRT-3: remembered for next time */
	char state[4096], text[256] = "";
	const char *st = getenv("XDG_STATE_HOME");
	snprintf(state, sizeof(state), "%s/twm/greeter", st ? st : "");
	FILE *f = fopen(state, "r");
	size_t n = f ? fread(text, 1, sizeof(text) - 1, f) : 0;
	text[n] = '\0';
	if (f) {
		fclose(f);
	}
	tc_expect("GRT-3", strstr(text, "last\ttom\n") && strstr(text, "tom\ttwm\n"),
		"the greeter remembers tom and Tom's WM");

	if (cfd >= 0) {
		close(cfd);
	}
	close(lfd);
	tc_disconnect(tc);
	return tc_report();
}
