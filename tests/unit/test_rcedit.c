/* Unit tests for .twmrc editing and the Settings diff (SET-5, SET-7). */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "rcedit.h"

static const char *B8 =
	"# my twmrc\n"
	"FocusPolicy   sloppy\n"
	"OpaqueMove    off   # keep outlines\n"
	"TitleHeight   26\n"
	"Effects       none\n"
	"SqueezeTitle  { \"xclock\" right  \"*\" left }\n"
	"Panner        { desktops 4 }\n";

static int line_eq(const struct rcdiff_line *l, char kind, int number, const char *text) {
	return l->kind == (enum rcdiff_kind)kind && l->number == number &&
		(!text || ((int)strlen(text) == l->len && memcmp(l->text, text, l->len) == 0));
}

int main(void) {
	/* SET-7: only the value changes; comments and spacing stay. */
	char *s = rcedit_set(B8, "TitleHeight", "28");
	CHECK("SET-7", strstr(s, "TitleHeight   28\n") != NULL);
	CHECK("SET-7", strncmp(s, "# my twmrc\nFocusPolicy   sloppy\n", 31) == 0);
	CHECK_EQ("SET-7", strlen(s), strlen(B8));
	free(s);
	s = rcedit_set(B8, "OpaqueMove", "on");
	CHECK("SET-7", strstr(s, "OpaqueMove    on   # keep outlines\n") != NULL);
	free(s);

	/* New keys are appended, values aligned at column 14. */
	s = rcedit_set(B8, "Snap", "{ edges thirds }");
	CHECK("SET-7", strncmp(s, B8, strlen(B8)) == 0);
	CHECK("SET-7", strcmp(s + strlen(B8), "Snap          { edges thirds }\n") == 0);
	free(s);
	s = rcedit_set("Accent #FFB000", "RememberGeometry", "off");
	CHECK("SET-7", strcmp(s, "Accent #FFB000\nRememberGeometry off\n") == 0);
	free(s);
	s = rcedit_set("", "Accent", "#7FD1AE");
	CHECK("SET-7", strcmp(s, "Accent        #7FD1AE\n") == 0);
	free(s);

	/* The last statement for a key is the one that counts. */
	s = rcedit_set("TitleHeight 22\nTitleHeight 24\n", "TitleHeight", "30");
	CHECK("SET-7", strcmp(s, "TitleHeight 22\nTitleHeight 30\n") == 0);
	free(s);
	/* Multi-line blocks are replaced whole. */
	s = rcedit_set("Snap {\n  edges\n}\nX 1\n", "Snap", "{ thirds }");
	CHECK("SET-7", strcmp(s, "Snap { thirds }\nX 1\n") == 0);
	free(s);

	char *v = rcedit_get(B8, "Panner");
	CHECK("SET-8", v && strcmp(v, "{ desktops 4 }") == 0);
	free(v);
	CHECK("SET-8", rcedit_get(B8, "Snap") == NULL);

	/* Block rows: SqueezeTitle's "*". */
	s = rcedit_set_row(B8, "SqueezeTitle", "\"*\"", "center");
	CHECK("SET-7", strstr(s, "SqueezeTitle  { \"xclock\" right  \"*\" center }\n") != NULL);
	free(s);
	s = rcedit_set_row("SqueezeTitle { \"xclock\" right }\n", "SqueezeTitle", "\"*\"", "off");
	CHECK("SET-7", strcmp(s, "SqueezeTitle { \"xclock\" right  \"*\" off }\n") == 0);
	free(s);
	s = rcedit_set_row("SqueezeTitle {\n\t\"xclock\" right\n}\n", "SqueezeTitle", "\"*\"", "left");
	CHECK("SET-7", strcmp(s, "SqueezeTitle {\n\t\"xclock\" right\n\t\"*\" left\n}\n") == 0);
	free(s);
	s = rcedit_set_row("Accent #FFB000\n", "SqueezeTitle", "\"*\"", "right");
	CHECK("SET-7", strcmp(s, "Accent #FFB000\nSqueezeTitle  { \"*\" right }\n") == 0);
	free(s);

	/* SET-5: B8's diff — one changed line, one appended, context around. */
	char *a = strdup(B8);
	char *b1 = rcedit_set(a, "TitleHeight", "28");
	char *b = rcedit_set(b1, "Snap", "{ edges thirds }");
	struct rcdiff d;
	rcdiff_compute(&d, a, b, 1);
	CHECK_EQ("SET-5", d.added, 2);
	CHECK_EQ("SET-5", d.removed, 1);
	CHECK_EQ("SET-5", d.n, 7);
	if (d.n == 7) {
		CHECK("SET-5", line_eq(&d.lines[0], ' ', 3, "OpaqueMove    off   # keep outlines"));
		CHECK("SET-5", line_eq(&d.lines[1], '-', 4, "TitleHeight   26"));
		CHECK("SET-5", line_eq(&d.lines[2], '+', 4, "TitleHeight   28"));
		CHECK("SET-5", line_eq(&d.lines[3], ' ', 5, "Effects       none"));
		CHECK("SET-5", line_eq(&d.lines[4], '~', 0, NULL));
		CHECK("SET-5", line_eq(&d.lines[5], ' ', 7, "Panner        { desktops 4 }"));
		CHECK("SET-5", line_eq(&d.lines[6], '+', 8, "Snap          { edges thirds }"));
	}
	rcdiff_free(&d);
	rcdiff_compute(&d, a, a, 2);
	CHECK_EQ("SET-5", d.n, 0);
	rcdiff_free(&d);
	rcdiff_compute(&d, "", "A 1\n", 2);
	CHECK("SET-5", d.n == 1 && line_eq(&d.lines[0], '+', 1, "A 1"));
	rcdiff_free(&d);
	free(a);
	free(b1);
	free(b);
	return UNIT_RESULT();
}
