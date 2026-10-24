/*
 * Screenshot test, after M7: twmfetch (FET-*), the root card (CARD-*),
 * screenshots and fetch shots (SHOT-*) and taking a look from one (SHR-*).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define ACCENT 0xFFB000
#define INK2 0xC9C7BF
#define W 1920
#define H 1080
#define TH 26

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

/* Pixels clearly brighter than the root (text and bars). */
static int count_lit(const struct tc_image *img, int x0, int y0, int x1, int y1) {
	int n = 0;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			uint32_t p = tc_pixel(img, x, y);
			n += ((p >> 16 & 0xff) + (p >> 8 & 0xff) + (p & 0xff)) > 3 * 0x40;
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

static void write_rc(const char *text) {
	char rc[4096];
	snprintf(rc, sizeof(rc), "%s/.twmrc", getenv("TWM_TEST_HOME"));
	FILE *f = fopen(rc, "w");
	fputs(text, f);
	fclose(f);
}

static int reload(void) {
	char cmd[4200];
	snprintf(cmd, sizeof(cmd), "\"%s\" --reload >/dev/null", getenv("TWM_BIN"));
	return system(cmd);
}

/* Run a shell line, its stdout into buf; its exit status. */
static int run(const char *line, char *buf, size_t len) {
	FILE *p = popen(line, "r");
	if (!p) {
		return -1;
	}
	size_t n = fread(buf, 1, len - 1, p);
	buf[n] = '\0';
	int st = pclose(p);
	return WEXITSTATUS(st);
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "fetch.png";
	const char *home = getenv("TWM_TEST_HOME");
	const char *bin = getenv("TWM_TEST_BIN");
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	char buf[16384], cmd[8192];

	/* twmfetch sits beside twm in the build */
	char fetch[4096];
	snprintf(fetch, sizeof(fetch), "%s", getenv("TWM_BIN"));
	char *slash = strrchr(fetch, '/');
	snprintf(slash + 1, sizeof(fetch) - (size_t)(slash + 1 - fetch), "twmfetch");

	/* --- FET-3: twm answers twmfetch: counts, never titles --- */
	tc_pointer_move(tc, 300, 200);
	struct tc_window *a = tc_window_create(tc, &(struct tc_window_opts){
		.title = "secret plans", .app_id = "notes", .width = 400, .height = 300, .color = 0x3060C0});
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 100);
	snprintf(cmd, sizeof(cmd), "'%s' --json --no-shot", fetch);
	int st = run(cmd, buf, sizeof(buf));
	tc_expect("FET-3", st == 0 && strstr(buf, "\"frames\":{\"open\":1,") &&
		strstr(buf, "\"outputs\":[{\"name\":\"HEADLESS-1\",\"width\":1920,\"height\":1080"),
		"twmfetch --json: one frame, the output (%.60s…)", buf);
	tc_expect("FET-3", !strstr(buf, "secret plans") && !strstr(buf, "notes"),
		"and no window's title or app id");
	snprintf(cmd, sizeof(cmd), "'%s' --no-shot", fetch);
	run(cmd, buf, sizeof(buf));
	tc_expect("FET-1", strstr(buf, "frames    1 open") && strstr(buf, " ■ tom's wm ░░░░ ") &&
		!strstr(buf, "\x1b["), "the plain fetch: B9's logo, the facts, no escapes into a pipe");
	tc_window_destroy(a);
	tc_wait(tc, 100);

	/* --- CARD-1: off by default --- */
	shot(tc, &img, out, "nocard");
	int bare = count_lit(&img, 80, H - 300, 700, H - 80);
	tc_expect("CARD-1", bare < 50, "no card by default (%d lit px bottom-left)", bare);

	/* --- CARD-1, CARD-2: RootCard on: bottom-left, 80 px in --- */
	write_rc("RootCard { at bottom-left fields host uptime load mem frames }\n"
		"Terminal \"sh -c 'touch $HOME/term-ran'\"\n");
	tc_expect("CARD-1", reload() == 0, "RootCard { … } loads");
	tc_wait(tc, 300);
	shot(tc, &img, out, "card");
	int lit = count_lit(&img, 80, H - 300, 700, H - 80);
	int accent = count_box(&img, 80, H - 300, 700, H - 80, ACCENT, 40);
	tc_expect("CARD-2", lit > 800, "the card is drawn bottom-left (%d lit px)", lit);
	tc_expect("CARD-2", accent > 60, "with its load and memory cells in accent (%d)", accent);
	tc_expect("CARD-2", count_lit(&img, 0, H - 300, 78, H) < 20 && count_lit(&img, 80, H - 79, 700, H) < 20,
		"80 px in from the edges");

	/* --- CARD-1: it lives under every window --- */
	tc_pointer_move(tc, 60, H - 330);
	struct tc_window *b = tc_window_create(tc, &(struct tc_window_opts){
		.title = "over", .app_id = "over", .width = 700, .height = 300, .color = 0x3060C0});
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 120);
	shot(tc, &img, out, "card-under");
	tc_expect("CARD-1", count_box(&img, 100, H - 250, 600, H - 100, 0x3060C0, 6) > 70000,
		"a window over it hides it");
	tc_window_destroy(b);
	tc_wait(tc, 120);

	/* --- CARD-3: B1 on it opens twmfetch in the Terminal --- */
	char ran[4200];
	snprintf(ran, sizeof(ran), "%s/term-ran", home);
	tc_pointer_move(tc, 150, H - 140);
	tc_wait(tc, 40);
	tc_pointer_click(tc, TC_BTN_LEFT);
	int waited = 0;
	while (access(ran, F_OK) != 0 && waited < 2000) {
		tc_wait(tc, 50);
		waited += 50;
	}
	tc_expect("CARD-3", access(ran, F_OK) == 0, "B1 on the card runs the Terminal (%d ms)", waited);
	tc_wait(tc, 60);
	shot(tc, &img, out, "card-click");
	tc_expect("CARD-3", count_box(&img, 60, 60, 600, 700, 0x121310, 0) < 20000,
		"and no root menu");
	(void)bin;

	/* --- CARD-1: top-right moves it --- */
	write_rc("RootCard { at top-right fields host frames }\n");
	reload();
	tc_wait(tc, 300);
	shot(tc, &img, out, "card-topright");
	tc_expect("CARD-1", count_lit(&img, W - 600, 80, W - 80, 220) > 300 &&
		count_lit(&img, 80, H - 300, 700, H - 80) < 50, "at top-right instead");
	write_rc("RootCard off\n");
	reload();
	tc_wait(tc, 200);

	tc_disconnect(tc);
	return tc_report();
}
