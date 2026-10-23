#ifndef TWM_TWMRC_H
#define TWM_TWMRC_H

#include <stdbool.h>
#include <stddef.h>

/*
 * .twmrc syntax (CFG-2, DESIGN §5.1). A lossless parse: the document keeps
 * its source and every node knows its byte span, so a tool can rewrite one
 * value and leave comments, order and spacing alone (CFG-8). Pure C, no
 * wlroots; unit-tested.
 */

enum twmrc_node_kind {
	TWMRC_WORD,   /* bare word: sloppy, f.move, top-right, Mod4 */
	TWMRC_STRING, /* "…", text holds the unescaped value */
	TWMRC_NUMBER, /* 26, 1.25, -3 */
	TWMRC_COLOR,  /* #RRGGBB */
	TWMRC_EXEC,   /* !"cmd", text holds cmd */
	TWMRC_BLOCK,  /* { … }, rows of nodes, one row per line */
};

struct twmrc_row;

struct twmrc_node {
	enum twmrc_node_kind kind;
	char *text;    /* decoded value (NULL for blocks) */
	double number; /* TWMRC_NUMBER */
	unsigned color; /* TWMRC_COLOR, 0xRRGGBB */
	int line;
	size_t start, end; /* byte span in the source */
	struct twmrc_row *rows; /* TWMRC_BLOCK */
	size_t nrows;
};

struct twmrc_row {
	struct twmrc_node *nodes;
	size_t n;
};

enum twmrc_stmt_kind {
	TWMRC_SETTING, /* Key value… */
	TWMRC_BUTTON,  /* ButtonN = [mods :] context : function */
	TWMRC_KEY,     /* "keyspec" : function */
};

struct twmrc_stmt {
	enum twmrc_stmt_kind kind;
	char *key;     /* setting name, "ButtonN", or the key spec */
	int line;
	size_t start, end;
	/* SETTING: one segment, the values. BUTTON/KEY: the parts between ':'. */
	struct twmrc_row *segments;
	size_t nsegments;
};

enum twmrc_severity {
	TWMRC_ERROR,
	TWMRC_WARNING,
};

struct twmrc_diag {
	enum twmrc_severity severity;
	int line;
	char message[160];
};

struct twmrc_doc {
	char *src;
	size_t len;
	struct twmrc_stmt *stmts;
	size_t nstmts;
	struct twmrc_diag *diags;
	size_t ndiags;
};

/* Parse `len` bytes of source; never NULL (diags hold syntax errors). */
struct twmrc_doc *twmrc_parse(const char *src, size_t len);
void twmrc_free(struct twmrc_doc *doc);

void twmrc_diag(struct twmrc_doc *doc, enum twmrc_severity sev, int line,
	const char *fmt, ...) __attribute__((format(printf, 4, 5)));
size_t twmrc_count(const struct twmrc_doc *doc, enum twmrc_severity sev);
/* First error, or NULL. */
const struct twmrc_diag *twmrc_first_error(const struct twmrc_doc *doc);

#endif
