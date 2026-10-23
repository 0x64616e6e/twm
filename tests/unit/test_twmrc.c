/* Unit tests for the .twmrc syntax (CFG-2, CFG-8). */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "twmrc.h"

static char *slurp(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	char *buf = malloc(1 << 16);
	*len = fread(buf, 1, (1 << 16) - 1, f);
	fclose(f);
	buf[*len] = '\0';
	return buf;
}

static struct twmrc_doc *parse(const char *s) {
	return twmrc_parse(s, strlen(s));
}

static char *span(const struct twmrc_doc *doc, size_t start, size_t end) {
	static char buf[256];
	size_t n = end - start < sizeof(buf) - 1 ? end - start : sizeof(buf) - 1;
	memcpy(buf, doc->src + start, n);
	buf[n] = '\0';
	return buf;
}

int main(int argc, char *argv[]) {
	/* CFG-9: B0's sample config parses cleanly. */
	size_t len = 0;
	char *b0 = slurp(argc > 1 ? argv[1] : "b0.twmrc", &len);
	CHECK("CFG-9", b0 != NULL);
	struct twmrc_doc *doc = twmrc_parse(b0, len);
	CHECK_EQ("CFG-9", doc->ndiags, 0);
	CHECK_EQ("CFG-2", doc->nstmts, 14);
	struct twmrc_stmt *st = &doc->stmts[0];
	CHECK("CFG-2", st->kind == TWMRC_SETTING && strcmp(st->key, "Theme") == 0);
	CHECK("CFG-2", st->segments[0].nodes[0].kind == TWMRC_STRING &&
		strcmp(st->segments[0].nodes[0].text, "phosphor") == 0);
	st = &doc->stmts[1]; /* Font "IBM Plex Mono" 11 */
	CHECK("CFG-2", st->segments[0].n == 2 && st->segments[0].nodes[1].kind == TWMRC_NUMBER &&
		st->segments[0].nodes[1].number == 11);
	st = &doc->stmts[2]; /* Accent #FFB000: a color, not a comment */
	CHECK("CFG-2", st->segments[0].n == 1 && st->segments[0].nodes[0].kind == TWMRC_COLOR &&
		st->segments[0].nodes[0].color == 0xFFB000);
	st = &doc->stmts[5]; /* OpaqueMove off # comment */
	CHECK("CFG-2", st->segments[0].n == 1 && strcmp(st->segments[0].nodes[0].text, "off") == 0);
	st = &doc->stmts[4]; /* SqueezeTitle { "*" left } */
	struct twmrc_node *blk = &st->segments[0].nodes[0];
	CHECK("CFG-2", blk->kind == TWMRC_BLOCK && blk->nrows == 1 && blk->rows[0].n == 2);
	st = &doc->stmts[10]; /* Button1 = root : f.menu "main" */
	CHECK("CFG-2", st->kind == TWMRC_BUTTON && strcmp(st->key, "Button1") == 0 &&
		st->nsegments == 2);
	CHECK("CFG-2", strcmp(st->segments[1].nodes[0].text, "f.menu") == 0 &&
		strcmp(st->segments[1].nodes[1].text, "main") == 0);
	st = &doc->stmts[13]; /* "M-Return" : !"foot" */
	CHECK("CFG-2", st->kind == TWMRC_KEY && strcmp(st->key, "M-Return") == 0 &&
		st->segments[0].nodes[0].kind == TWMRC_EXEC &&
		strcmp(st->segments[0].nodes[0].text, "foot") == 0);
	CHECK_EQ("CFG-2", st->line, 18);

	/* CFG-8: spans point at the source, comments untouched around them. */
	st = &doc->stmts[5];
	struct twmrc_node *v = &st->segments[0].nodes[0];
	CHECK("CFG-8", strcmp(span(doc, v->start, v->end), "off") == 0);
	CHECK("CFG-8", strcmp(span(doc, st->start, st->end), "OpaqueMove    off") == 0);
	CHECK("CFG-8", strcmp(doc->src, b0) == 0);
	twmrc_free(doc);
	free(b0);

	/* Multi-line blocks: one row per line (menus). */
	doc = parse("Menu \"main\" {\n  \"xterm\"  !\"xterm\"\n\n  \"move\"   f.move\n}\n");
	CHECK_EQ("CFG-2", doc->ndiags, 0);
	blk = &doc->stmts[0].segments[0].nodes[1];
	CHECK("CFG-2", blk->kind == TWMRC_BLOCK && blk->nrows == 2);
	CHECK("CFG-2", blk->rows[1].nodes[1].kind == TWMRC_WORD &&
		strcmp(blk->rows[1].nodes[1].text, "f.move") == 0);
	twmrc_free(doc);

	/* Buttons with a modifier part. */
	doc = parse("Button1 = M : window : f.move\n");
	CHECK("CFG-2", doc->ndiags == 0 && doc->stmts[0].nsegments == 3);
	twmrc_free(doc);

	/* Escapes in strings; '#' inside a string isn't a comment. */
	doc = parse("Terminal \"foot -e \\\"#x\\\"\" # c\n");
	CHECK("CFG-2", doc->ndiags == 0 &&
		strcmp(doc->stmts[0].segments[0].nodes[0].text, "foot -e \"#x\"") == 0);
	twmrc_free(doc);

	/* Syntax errors carry their line; parsing goes on after them. */
	doc = parse("Accent #FFB000\nTerminal \"foot\nTitleHeight 28\nSnap { edges\n");
	CHECK_EQ("CFG-4", twmrc_count(doc, TWMRC_ERROR), 2);
	CHECK_EQ("CFG-4", twmrc_first_error(doc)->line, 2);
	CHECK("CFG-4", strstr(twmrc_first_error(doc)->message, "unterminated") != NULL);
	CHECK_EQ("CFG-4", doc->nstmts, 2); /* Accent and TitleHeight survive */
	twmrc_free(doc);

	doc = parse("\"M-x\" f.move\n: oops\n");
	CHECK_EQ("CFG-4", twmrc_count(doc, TWMRC_ERROR), 2);
	twmrc_free(doc);

	/* A word that merely starts with digits is a word, not a number. */
	doc = parse("Output \"DP-1\" { mode 2560x1440 scale 1.25 }\n");
	blk = &doc->stmts[0].segments[0].nodes[1];
	CHECK("CFG-2", blk->rows[0].nodes[1].kind == TWMRC_WORD &&
		blk->rows[0].nodes[3].kind == TWMRC_NUMBER && blk->rows[0].nodes[3].number == 1.25);
	twmrc_free(doc);
	return UNIT_RESULT();
}
