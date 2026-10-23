/*
 * M7 screenshot test: the Settings window (SET-1 … SET-9, HIS-5). It edits
 * a real ~/.twmrc through the window's controls, checks the diff it shows,
 * writes the file and watches twm apply it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "client.h"

#define GROUND 0x0D0E0B
#define SURFACE 0x121310
#define SUNK 0x0F100D
#define GRAPHITE 0x23251F
#define ACCENT 0xFFB000
#define MINT 0x7FD1AE
#define OK 0x9FD8B8
#define ALERT 0xFF8A65
#define TH 26
#define KEY_COMMA 51
#define KEY_S 31
#define KEY_D 32
#define KEY_LEFTCTRL 29
#define KEY_BACKSPACE 14

static char rc_path[4096];

static const char *RC =
	"# ~/.twmrc — as on B8\n"
	"Accent        #FFB000\n"
	"Font          \"IBM Plex Mono\" 12\n"
	"\n"
	"FocusPolicy   sloppy    # FocusDelay is the default\n"
	"OpaqueMove    off\n"
	"TitleHeight   26\n"
	"Effects       none\n"
	"Panner        { desktops 4 }\n";

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

static char *read_file(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return strdup("");
	}
	static char buf[65536];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return strdup(buf);
}

static void key(struct tc *tc, uint32_t k) {
	tc_key(tc, k, true);
	tc_key(tc, k, false);
	tc_wait(tc, 30);
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "m7-settings.png";
	struct tc *tc = tc_connect();
	struct tc_image img = {0};
	char path[4096];

	snprintf(rc_path, sizeof(rc_path), "%s/.twmrc", getenv("TWM_TEST_HOME"));
	FILE *f = fopen(rc_path, "w");
	fputs(RC, f);
	fclose(f);
	tc_wait(tc, 500);

	/* SET-1: M-, opens it, focused, centred: frame at (391, 262), the
	 * body 1135 x 527 at (392, 289). */
	tc_pointer_move(tc, 960, 1040);
	tc_chord(tc, 2, TC_KEY_LEFTMETA, KEY_COMMA);
	tc_wait(tc, 100);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-open.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
#define X0 392
#define Y0 289
	tc_expect_pixel("SET-1", &img, X0 - 1, Y0 + 100, ACCENT, 2); /* focused frame */
	tc_expect("SET-2", count_box(&img, X0, Y0 + 51 + 33, X0 + 3, Y0 + 51 + 66, ACCENT, 2) > 60,
		"opens on Frames, as B8 (the nav bar)");
	tc_expect_pixel("SET-5", &img, X0 + 1135 - 100, Y0 + 200, SUNK, 0); /* the diff pane */
	tc_expect("SET-5", count_box(&img, X0 + 771 + 200, Y0 + 408, X0 + 1135, Y0 + 423, OK, 40) > 10,
		"twm --check: ok");
	int rows_before = count_box(&img, X0 + 3, 262, X0 + 4, 300, ACCENT, 2);

	/* SET-3, SET-9: Title height 28 on the slider. */
	tc_pointer_move(tc, X0 + 350 + 105, Y0 + 66 + 24);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
	/* Snap: tick "output centre". */
	tc_pointer_move(tc, X0 + 357, Y0 + 280);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
	shot(tc, &img);
	tc_image_write_png(&img, out);
	tc_expect("SET-3", count_box(&img, X0 + 600, Y0 + 66, X0 + 747, Y0 + 115, ACCENT, 30) > 40,
		"TitleHeight turns accent with a dot, the value too");
	tc_expect("SET-5", count_box(&img, X0 + 771, Y0 + 52, X0 + 1135, Y0 + 52 + 8 * 19,
		ALERT, 40) > 20, "a removed line, in alert");
	tc_expect("SET-5", count_box(&img, X0 + 771, Y0 + 52, X0 + 1135, Y0 + 52 + 8 * 19,
		OK, 40) > 40, "added lines, in ok");
	tc_expect("SET-5", count_box(&img, X0 + 1000, Y0, X0 + 1135, Y0 + 38, ACCENT, 40) > 10,
		"\"2 changes\" in accent");
	char *disk = read_file(rc_path);
	tc_expect("SET-1", strcmp(disk, RC) == 0, "nothing written yet");
	free(disk);

	/* SET-4: the preview follows the pending values; the desktop doesn't. */
	tc_pointer_move(tc, X0 + 398, Y0 + 325); /* the mint swatch */
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 60);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-preview.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect("SET-4", count_box(&img, X0 + 189, Y0 + 527 - 17 - 28, X0 + 700, Y0 + 527 - 17,
		MINT, 2) > 500, "the preview tab is mint and 28 px");
	tc_expect_pixel("SET-4", &img, X0 - 1, Y0 + 100, ACCENT, 2); /* the frame still amber */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_Z);                    /* HIS-5: undo the swatch */
	tc_wait(tc, 60);
	shot(tc, &img);
	tc_expect("HIS-5", count_box(&img, X0 + 189, Y0 + 527 - 17 - 28, X0 + 700, Y0 + 527 - 17,
		MINT, 2) == 0, "M-z takes the pending swatch back");

	/* SET-6: Write applies at once; SET-7: comments and order kept. */
	tc_pointer_move(tc, X0 + 1011, Y0 + 454);
	tc_pointer_click(tc, TC_BTN_LEFT);
	tc_wait(tc, 150);
	disk = read_file(rc_path);
	const char *want =
		"# ~/.twmrc — as on B8\n"
		"Accent        #FFB000\n"
		"Font          \"IBM Plex Mono\" 12\n"
		"\n"
		"FocusPolicy   sloppy    # FocusDelay is the default\n"
		"OpaqueMove    off\n"
		"TitleHeight   28\n"
		"Effects       none\n"
		"Panner        { desktops 4 }\n"
		"Snap          { edges thirds center }\n";
	tc_expect("SET-7", strcmp(disk, want) == 0, "the file: one value changed, one key appended");
	if (strcmp(disk, want) != 0) {
		printf("---\n%s---\n", disk);
	}
	free(disk);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-written.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	int rows_after = count_box(&img, X0 + 3, 262, X0 + 4, 300, ACCENT, 2);
	tc_expect("SET-6", rows_before == TH + 1 && rows_after == 28 + 1,
		"applied: the tab is 28 px now (%d -> %d rows)", rows_before, rows_after);
	tc_expect("SET-5", count_box(&img, X0 + 771, Y0 + 52, X0 + 1135, Y0 + 52 + 8 * 19 + 2,
		OK, 40) < 5, "the diff is empty again");

	/* HIS-5: M-z undoes the write; M-S-z does it again. */
	tc_chord(tc, 2, TC_KEY_LEFTMETA, TC_KEY_Z);
	tc_wait(tc, 150);
	disk = read_file(rc_path);
	shot(tc, &img);
	tc_expect("HIS-5", strcmp(disk, RC) == 0 &&
		count_box(&img, X0 + 3, 262, X0 + 4, 300, ACCENT, 2) == TH + 1,
		"M-z: the old file is back, and applied");
	free(disk);
	tc_chord(tc, 3, TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_Z);
	tc_wait(tc, 150);
	disk = read_file(rc_path);
	tc_expect("HIS-5", strcmp(disk, want) == 0, "M-S-z: written again");
	free(disk);

	/* SET-8: a hand edit moves the controls. */
	f = fopen(rc_path, "w");
	fputs("# by hand\nTitleHeight 30\n", f);
	fclose(f);
	tc_wait(tc, 600);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-hand.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	int pos30 = X0 + 350 + (30 - 22) * 210 / 12;
	tc_expect("SET-8", count_box(&img, pos30 - 4, Y0 + 66 + 30 + 4 - 2 + 2, pos30 + 4,
		Y0 + 66 + 30 + 4 + 6, ACCENT, 2) > 0 || count_box(&img, pos30 - 6, Y0 + 66,
		pos30 + 6, Y0 + 66 + 60, ACCENT, 2) > 40, "the slider moved to 30");
	tc_expect("SET-8", count_box(&img, X0 + 3, 262, X0 + 4, 300, ACCENT, 2) == 30 + 1,
		"and twm applied it");

	/* The tab is 30 px now: the body sits 4 px lower. */
#undef Y0
#define Y0 (289 + 4)
	/* An invalid value is shown, not written: the check says why. */
	tc_pointer_move(tc, X0 + 495, Y0 + 325);
	tc_pointer_click(tc, TC_BTN_LEFT); /* the hex text */
	tc_wait(tc, 40);
	for (int i = 0; i < 7; i++) {
		key(tc, KEY_BACKSPACE);
	}
	tc_chord(tc, 2, TC_KEY_LEFTSHIFT, TC_KEY_3); /* # */
	key(tc, TC_KEY_1);
	key(tc, TC_KEY_ENTER);
	tc_wait(tc, 60);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-error.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect("CFG-6", count_box(&img, X0 + 771 + 120, Y0 + 408, X0 + 1135, Y0 + 423,
		ALERT, 40) > 10, "#1 is not a color: the check line says so, in alert");
	tc_pointer_move(tc, X0 + 1011, Y0 + 454);
	tc_pointer_click(tc, TC_BTN_LEFT); /* Write is disabled */
	tc_wait(tc, 100);
	disk = read_file(rc_path);
	tc_expect("SET-6", strcmp(disk, "# by hand\nTitleHeight 30\n") == 0, "and is not written");
	free(disk);
	/* Ctrl-D discards. */
	tc_chord(tc, 2, KEY_LEFTCTRL, KEY_D);
	tc_wait(tc, 60);
	shot(tc, &img);
	tc_expect("SET-5", count_box(&img, X0 + 771 + 200, Y0 + 408, X0 + 1135, Y0 + 423, OK, 40) > 10,
		"Ctrl-D: discarded, the check is ok again");

	/* SET-2: typing filters the controls. */
	key(tc, KEY_S);
	key(tc, 49); /* n */
	key(tc, 30); /* a */
	key(tc, 25); /* p */
	tc_wait(tc, 40);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-filter.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_expect("SET-2", count_box(&img, X0 + 340, Y0, X0 + 760, Y0 + 527, MINT, 2) == 0 &&
		count_box(&img, X0 + 10, Y0 + 12, X0 + 155, Y0 + 13, ACCENT, 2) > 100,
		"\"snap\": the Accent row is gone, the filter field is lit");
	tc_expect("SET-2", count_box(&img, X0, Y0 + 51 + 33, X0 + 3, Y0 + 51 + 66, ACCENT, 2) == 0,
		"no section is selected while filtering");

	/* P-8: the keyboard alone. Esc clears the filter, Tab walks the
	 * sections, Down picks a row, Right changes it, Ctrl-S writes. */
	key(tc, TC_KEY_ESC);
	key(tc, TC_KEY_TAB); /* Frames -> Focus */
	key(tc, TC_KEY_DOWN);
	key(tc, TC_KEY_RIGHT); /* Focus: click */
	tc_wait(tc, 40);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-focus.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	tc_chord(tc, 2, KEY_LEFTCTRL, KEY_S);
	tc_wait(tc, 150);
	disk = read_file(rc_path);
	tc_expect("SET-6", strcmp(disk, "# by hand\nTitleHeight 30\nFocusPolicy   click\n") == 0,
		"keys only: FocusPolicy click, written with Ctrl-S");
	free(disk);
	static const char *const names[] = {"keys", "desktops", "outputs", "privacy", "menus",
		"look"};
	for (int i = 0; i < 6; i++) {
		key(tc, TC_KEY_TAB);
		tc_wait(tc, 30);
		shot(tc, &img);
		snprintf(path, sizeof(path), "%.*s-%s.png", (int)strlen(out) - 4, out, names[i]);
		tc_image_write_png(&img, path);
	}

	/* B8's resize box: a bigger window, more room for the diff. */
	int tab_end = 391;
	while (tab_end < 1900 && (tc_pixel(&img, tab_end, 263) >> 16 & 0xff) > 0x80) {
		tab_end++;
	}
	tc_pointer_move(tc, tab_end - 15, 262 + 15);
	tc_pointer_button(tc, TC_BTN_LEFT, true);
	for (int i = 1; i <= 6; i++) {
		tc_pointer_move(tc, tab_end - 15 + 200 * i / 6, 262 + 15 + 120 * i / 6);
	}
	tc_pointer_button(tc, TC_BTN_LEFT, false);
	tc_wait(tc, 100);
	shot(tc, &img);
	snprintf(path, sizeof(path), "%.*s-resized.png", (int)strlen(out) - 4, out);
	tc_image_write_png(&img, path);
	int right = 1919;
	while (right > 391 && !near(tc_pixel(&img, right, 600), ACCENT, 2)) {
		right--;
	}
	tc_expect("SET-1", right > 391 + 1137 + 150 && near(tc_pixel(&img, right - 100, 880), SUNK, 0),
		"resized: wider (right edge %d) and taller, the diff pane follows", right);

	tc_image_free(&img);
	tc_disconnect(tc);
	return tc_report();
}
