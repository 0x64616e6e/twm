/*
 * Screenshot test, after M7: volume and brightness keys (LVL-*). Stand-ins
 * for wpctl and brightnessctl (first on twm's PATH, run-shot.sh) keep a level
 * in a file; the real media keys move it, and the root line says the new
 * level in one line that changes in place.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "client.h"

#define KEY_VOLUMEUP 115
#define KEY_VOLUMEDOWN 114
#define KEY_MUTE 113
#define KEY_BRIGHTNESSUP 225
/* the root line's region (ROOT-5): the left 40 % in, above the status line */
#define RL_X0 980
#define RL_X1 1700
#define RL_Y0 960
#define RL_Y1 1046

static char bin[4096];

static void tool(const char *name, const char *body) {
	char path[4200];
	snprintf(path, sizeof(path), "%s/%s", bin, name);
	FILE *f = fopen(path, "w");
	fprintf(f, "#!/bin/sh\nstate=\"%s/%s.level\"\n[ -f \"$state\" ] || echo 40 > \"$state\"\n%s", bin, name, body);
	fclose(f);
	chmod(path, 0755);
}

static int level_of(const char *name) {
	char path[4200];
	snprintf(path, sizeof(path), "%s/%s.level", bin, name);
	FILE *f = fopen(path, "r");
	int v = -1;
	if (f) {
		if (fscanf(f, "%d", &v) != 1) {
			v = -1;
		}
		fclose(f);
	}
	return v;
}

/* Rows of the root line region with lit pixels, as text lines (each ~18 px). */
static int lit_rows(struct tc *tc, int *first, int *last) {
	struct tc_image img = {0};
	tc_screenshot(tc, &img);
	*first = *last = -1;
	int rows = 0;
	for (int y = RL_Y0; y < RL_Y1; y++) {
		int lit = 0;
		for (int x = RL_X0; x < RL_X1; x++) {
			uint32_t p = tc_pixel(&img, x, y);
			lit += ((p >> 16 & 0xff) + (p >> 8 & 0xff) + (p & 0xff)) / 3 > 0x60;
		}
		if (lit > 2) {
			rows++;
			if (*first < 0) {
				*first = y;
			}
			*last = y;
		}
	}
	tc_image_free(&img);
	return rows;
}

static void press(struct tc *tc, uint32_t key) {
	tc_key(tc, key, true);
	tc_key(tc, key, false);
	tc_wait(tc, 150);
}

int main(int argc, char *argv[]) {
	const char *out = argc > 1 ? argv[1] : "levels.png";
	snprintf(bin, sizeof(bin), "%s", getenv("TWM_TEST_BIN") ? getenv("TWM_TEST_BIN") : ".");
	/* wpctl: set-volume [-l L] @SINK@ N%+|N%-, set-mute @…@ toggle, get-volume @…@ */
	tool("wpctl",
		"v=$(cat \"$state\"); m=$(cat \"$state.mute\" 2>/dev/null || echo 0)\n"
		"case \"$1\" in\n"
		"set-volume) for a; do n=$(echo \"$a\" | tr -dc 0-9); case \"$a\" in *%+) v=$((v + n)) ;; *%-) v=$((v - n)) ;; esac; done\n"
		"  [ $v -gt 100 ] && v=100; [ $v -lt 0 ] && v=0; echo $v > \"$state\" ;;\n"
		"set-mute) echo $((1 - m)) > \"$state.mute\" ;;\n"
		"get-volume) printf 'Volume: %d.%02d' $((v / 100)) $((v % 100)); [ \"$m\" = 1 ] && printf ' [MUTED]'; echo ;;\n"
		"esac\n");
	tool("brightnessctl",
		"v=$(cat \"$state\")\n"
		"for a; do n=$(echo \"$a\" | tr -dc 0-9); case \"$a\" in *%+) v=$((v + n)) ;; *%-) v=$((v - n)) ;; -m) echo \"fake,backlight,$v,$v%,100\"; exit 0 ;; esac; done\n"
		"[ $v -gt 100 ] && v=100; echo $v > \"$state\"\n");
	struct tc *tc = tc_connect();
	tc_wait(tc, 400);
	tc_pointer_move(tc, 600, 400);
	int f0, l0, f1, l1;
	int before = lit_rows(tc, &f0, &l0);

	/* LVL-1: the volume key, three times: 40 -> 55 */
	press(tc, KEY_VOLUMEUP);
	int one = lit_rows(tc, &f1, &l1);
	press(tc, KEY_VOLUMEUP);
	press(tc, KEY_VOLUMEUP);
	tc_expect("LVL-1", level_of("wpctl") == 55, "three presses: 40%% -> %d%%", level_of("wpctl"));
	struct tc_image img = {0};
	tc_screenshot(tc, &img);
	tc_image_write_png(&img, out);
	tc_image_free(&img);
	int f2, l2;
	int three = lit_rows(tc, &f2, &l2);
	/* LVL-4: one line, changed in place: the same rows lit after 1 press and after 3 */
	tc_expect("LVL-4", one > before && three == one && f2 == f1,
		"one root line, changed in place (%d rows before, %d after one press, %d after three)",
		before, one, three);
	press(tc, KEY_VOLUMEDOWN);
	tc_expect("LVL-1", level_of("wpctl") == 50, "the volume down key: %d%%", level_of("wpctl"));
	press(tc, KEY_MUTE);
	char path[4200];
	snprintf(path, sizeof(path), "%s/wpctl.level.mute", bin);
	FILE *f = fopen(path, "r");
	int muted = f ? fgetc(f) == '1' : 0;
	if (f) {
		fclose(f);
	}
	tc_expect("LVL-1", muted, "the mute key mutes");

	/* LVL-1: brightness, on its own line */
	press(tc, KEY_BRIGHTNESSUP);
	tc_expect("LVL-1", level_of("brightnessctl") == 45, "the brightness key: 40%% -> %d%%",
		level_of("brightnessctl"));
	int f3, l3;
	int two_lines = lit_rows(tc, &f3, &l3);
	tc_expect("LVL-4", two_lines > three, "brightness has a line of its own (%d rows)", two_lines);
	/* LVL-4: 3 s after the last press, the lines go */
	tc_wait(tc, 3400);
	int after = lit_rows(tc, &f3, &l3);
	tc_expect("LVL-4", after == before, "gone 3 s later (%d rows)", after);
	tc_disconnect(tc);
	return tc_report();
}
