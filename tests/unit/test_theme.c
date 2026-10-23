/* Unit tests for theme color math (§2.1). */
#include <stdio.h>
#include "theme.h"

static int failures;

static void check(const char *what, int ok) {
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}

int main(void) {
	struct twm_theme t;
	theme_init_defaults(&t);
	check("§2.1 accent default is amber", t.accent == 0xFFB000);
	check("§2.3 TitleHeight default 26", t.title_height == 26);
	check("FOC-2 FocusDelay default 80", t.focus_delay_ms == 80);

	struct twm_rgba dot = rgba_over(rgba_hex(t.ink, 0.07f), rgba_hex(t.ground, 1));
	check("weave dot is #1C1D1A", rgba_argb32(dot) == 0xFF1C1D1A);
	check("opaque ground packs to ARGB", rgba_argb32(rgba_hex(t.ground, 1)) == 0xFF0D0E0B);
	struct twm_rgba half = rgba_hex(0xFFFFFF, 0.5f);
	check("premultiplied half white", rgba_argb32(half) == 0x80808080);
	float p[4];
	rgba_premul(half, p);
	check("rgba_premul", p[0] == 0.5f && p[3] == 0.5f);
	return failures ? 1 : 0;
}
