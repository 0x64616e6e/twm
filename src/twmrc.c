#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "twmrc.h"

enum tok_kind {
	TOK_WORD,
	TOK_STRING,
	TOK_NUMBER,
	TOK_COLOR,
	TOK_LBRACE,
	TOK_RBRACE,
	TOK_EQUALS,
	TOK_COLON,
	TOK_BANG,
	TOK_NEWLINE,
	TOK_EOF,
	TOK_BAD,
};

struct tok {
	enum tok_kind kind;
	size_t start, end;
	int line;
	char *text; /* owned; words, numbers, decoded strings */
	double number;
	unsigned color;
};

struct parser {
	struct twmrc_doc *doc;
	size_t pos;
	int line;
	struct tok cur;
	bool have;
};

static void *grow(void *ptr, size_t *cap, size_t n, size_t size) {
	if (n < *cap) {
		return ptr;
	}
	*cap = *cap ? *cap * 2 : 4;
	void *p = realloc(ptr, *cap * size);
	if (!p) {
		abort();
	}
	return p;
}

void twmrc_diag(struct twmrc_doc *doc, enum twmrc_severity sev, int line,
		const char *fmt, ...) {
	struct twmrc_diag *d = realloc(doc->diags, (doc->ndiags + 1) * sizeof(*d));
	if (!d) {
		abort();
	}
	doc->diags = d;
	d = &doc->diags[doc->ndiags++];
	d->severity = sev;
	d->line = line;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(d->message, sizeof(d->message), fmt, ap);
	va_end(ap);
}

size_t twmrc_count(const struct twmrc_doc *doc, enum twmrc_severity sev) {
	size_t n = 0;
	for (size_t i = 0; i < doc->ndiags; i++) {
		n += doc->diags[i].severity == sev;
	}
	return n;
}

const struct twmrc_diag *twmrc_first_error(const struct twmrc_doc *doc) {
	const struct twmrc_diag *first = NULL;
	for (size_t i = 0; i < doc->ndiags; i++) {
		const struct twmrc_diag *d = &doc->diags[i];
		if (d->severity == TWMRC_ERROR && (!first || d->line < first->line)) {
			first = d;
		}
	}
	return first;
}

/* --- lexer --- */

static bool is_hex(char c) {
	return isxdigit((unsigned char)c) != 0;
}

static bool is_delim(char c) {
	return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
		c == '{' || c == '}' || c == '=' || c == ':' || c == '"';
}

static char at(struct parser *p, size_t i) {
	return i < p->doc->len ? p->doc->src[i] : '\0';
}

static struct tok lex(struct parser *p) {
	const char *s = p->doc->src;
	for (;;) {
		char c = at(p, p->pos);
		if (c == ' ' || c == '\t' || c == '\r') {
			p->pos++;
		} else if (c == '\\' && at(p, p->pos + 1) == '\n') {
			p->pos += 2; /* line continuation */
			p->line++;
		} else if (c == '#') {
			/* #RRGGBB is a color (B0: "Accent #FFB000"); any other # starts
			 * a comment. */
			bool color = true;
			for (int i = 1; i <= 6; i++) {
				color &= is_hex(at(p, p->pos + (size_t)i));
			}
			color &= is_delim(at(p, p->pos + 7)) || at(p, p->pos + 7) == '#';
			if (color) {
				struct tok t = {.kind = TOK_COLOR, .start = p->pos,
					.end = p->pos + 7, .line = p->line};
				char hex[7];
				memcpy(hex, s + p->pos + 1, 6);
				hex[6] = '\0';
				t.color = (unsigned)strtoul(hex, NULL, 16);
				p->pos += 7;
				return t;
			}
			while (at(p, p->pos) != '\n' && at(p, p->pos) != '\0') {
				p->pos++;
			}
		} else {
			break;
		}
	}

	struct tok t = {.start = p->pos, .line = p->line};
	char c = at(p, p->pos);
	switch (c) {
	case '\0':
		t.kind = TOK_EOF;
		t.end = p->pos;
		return t;
	case '\n':
		t.kind = TOK_NEWLINE;
		p->pos++;
		p->line++;
		t.end = p->pos;
		return t;
	case '{':
	case '}':
	case '=':
	case ':':
	case '!':
		t.kind = c == '{' ? TOK_LBRACE : c == '}' ? TOK_RBRACE :
			c == '=' ? TOK_EQUALS : c == ':' ? TOK_COLON : TOK_BANG;
		p->pos++;
		t.end = p->pos;
		return t;
	case '"': {
		size_t cap = 16, n = 0;
		char *out = malloc(cap);
		p->pos++;
		for (;;) {
			char ch = at(p, p->pos);
			if (ch == '\0' || ch == '\n') {
				twmrc_diag(p->doc, TWMRC_ERROR, t.line, "unterminated string");
				free(out);
				t.kind = TOK_BAD;
				t.end = p->pos;
				return t;
			}
			p->pos++;
			if (ch == '"') {
				break;
			}
			if (ch == '\\') {
				char e = at(p, p->pos);
				if (e != '\0' && e != '\n') {
					p->pos++;
					ch = e == 'n' ? '\n' : e == 't' ? '\t' : e;
				}
			}
			out = grow(out, &cap, n + 1, 1);
			out[n++] = ch;
		}
		out = grow(out, &cap, n + 1, 1);
		out[n] = '\0';
		t.kind = TOK_STRING;
		t.text = out;
		t.end = p->pos;
		return t;
	}
	default:
		break;
	}

	while (!is_delim(at(p, p->pos))) {
		p->pos++;
	}
	t.end = p->pos;
	size_t n = t.end - t.start;
	t.text = malloc(n + 1);
	memcpy(t.text, s + t.start, n);
	t.text[n] = '\0';
	char *endp;
	t.number = strtod(t.text, &endp);
	bool numeric = *endp == '\0' && (isdigit((unsigned char)t.text[0]) ||
		((t.text[0] == '-' || t.text[0] == '+' || t.text[0] == '.') &&
		 isdigit((unsigned char)t.text[1])));
	t.kind = numeric ? TOK_NUMBER : TOK_WORD;
	return t;
}

static struct tok *peek(struct parser *p) {
	if (!p->have) {
		p->cur = lex(p);
		p->have = true;
	}
	return &p->cur;
}

/* Take the current token (caller owns its text). */
static struct tok next(struct parser *p) {
	peek(p);
	p->have = false;
	return p->cur;
}

static void skip_line(struct parser *p) {
	for (;;) {
		struct tok t = next(p);
		free(t.text);
		if (t.kind == TOK_NEWLINE || t.kind == TOK_EOF) {
			return;
		}
	}
}

static const char *tok_name(enum tok_kind k) {
	switch (k) {
	case TOK_LBRACE: return "'{'";
	case TOK_RBRACE: return "'}'";
	case TOK_EQUALS: return "'='";
	case TOK_COLON: return "':'";
	case TOK_BANG: return "'!'";
	case TOK_NEWLINE: return "end of line";
	case TOK_EOF: return "end of file";
	default: return "token";
	}
}

/* --- parser --- */

static void row_push(struct twmrc_row *row, size_t *cap, struct twmrc_node node) {
	row->nodes = grow(row->nodes, cap, row->n, sizeof(*row->nodes));
	row->nodes[row->n++] = node;
}

static void free_node(struct twmrc_node *n);

static void free_row(struct twmrc_row *row) {
	for (size_t i = 0; i < row->n; i++) {
		free_node(&row->nodes[i]);
	}
	free(row->nodes);
}

static void free_node(struct twmrc_node *n) {
	free(n->text);
	for (size_t i = 0; i < n->nrows; i++) {
		free_row(&n->rows[i]);
	}
	free(n->rows);
}

static bool parse_value(struct parser *p, struct twmrc_node *out);

static bool parse_block(struct parser *p, struct tok open, struct twmrc_node *out) {
	*out = (struct twmrc_node){.kind = TWMRC_BLOCK, .line = open.line, .start = open.start};
	size_t rows_cap = 0, nodes_cap = 0;
	struct twmrc_row row = {0};
	for (;;) {
		struct tok *t = peek(p);
		if (t->kind == TOK_NEWLINE || t->kind == TOK_RBRACE) {
			struct tok tt = next(p);
			if (row.n > 0) {
				out->rows = grow(out->rows, &rows_cap, out->nrows, sizeof(*out->rows));
				out->rows[out->nrows++] = row;
				row = (struct twmrc_row){0};
				nodes_cap = 0;
			}
			if (tt.kind == TOK_RBRACE) {
				out->end = tt.end;
				return true;
			}
			continue;
		}
		if (t->kind == TOK_EOF) {
			twmrc_diag(p->doc, TWMRC_ERROR, open.line, "'{' is never closed");
			free_row(&row);
			free_node(out);
			*out = (struct twmrc_node){0};
			return false;
		}
		struct twmrc_node node;
		if (!parse_value(p, &node)) {
			free_row(&row);
			free_node(out);
			*out = (struct twmrc_node){0};
			return false;
		}
		row_push(&row, &nodes_cap, node);
	}
}

static bool parse_value(struct parser *p, struct twmrc_node *out) {
	struct tok t = next(p);
	*out = (struct twmrc_node){.line = t.line, .start = t.start, .end = t.end};
	switch (t.kind) {
	case TOK_WORD:
		out->kind = TWMRC_WORD;
		out->text = t.text;
		return true;
	case TOK_STRING:
		out->kind = TWMRC_STRING;
		out->text = t.text;
		return true;
	case TOK_NUMBER:
		out->kind = TWMRC_NUMBER;
		out->text = t.text;
		out->number = t.number;
		return true;
	case TOK_COLOR:
		out->kind = TWMRC_COLOR;
		out->color = t.color;
		return true;
	case TOK_BANG: {
		struct tok s = next(p);
		if (s.kind != TOK_STRING) {
			twmrc_diag(p->doc, TWMRC_ERROR, t.line, "'!' needs a quoted command");
			if (s.kind == TOK_NEWLINE || s.kind == TOK_EOF) {
				p->cur = s; /* leave the line end for recovery */
				p->have = true;
			} else {
				free(s.text);
			}
			return false;
		}
		out->kind = TWMRC_EXEC;
		out->text = s.text;
		out->end = s.end;
		return true;
	}
	case TOK_LBRACE:
		return parse_block(p, t, out);
	case TOK_BAD:
		return false;
	default:
		twmrc_diag(p->doc, TWMRC_ERROR, t.line, "unexpected %s", tok_name(t.kind));
		free(t.text);
		return false;
	}
}

/* Values up to the end of the line, split at ':' when `colons` is set. */
static bool parse_segments(struct parser *p, struct twmrc_stmt *st, bool colons) {
	size_t seg_cap = 0, nodes_cap = 0;
	struct twmrc_row seg = {0};
	for (;;) {
		struct tok *t = peek(p);
		if (t->kind == TOK_NEWLINE || t->kind == TOK_EOF || (colons && t->kind == TOK_COLON)) {
			bool colon = t->kind == TOK_COLON;
			struct tok tt = next(p);
			st->segments = grow(st->segments, &seg_cap, st->nsegments, sizeof(*st->segments));
			st->segments[st->nsegments++] = seg;
			seg = (struct twmrc_row){0};
			nodes_cap = 0;
			if (!colon) {
				(void)tt;
				return true;
			}
			continue;
		}
		struct twmrc_node node;
		if (!parse_value(p, &node)) {
			free_row(&seg);
			return false;
		}
		st->end = node.end;
		row_push(&seg, &nodes_cap, node);
	}
}

static void free_stmt(struct twmrc_stmt *st) {
	free(st->key);
	for (size_t i = 0; i < st->nsegments; i++) {
		free_row(&st->segments[i]);
	}
	free(st->segments);
}

struct twmrc_doc *twmrc_parse(const char *src, size_t len) {
	struct twmrc_doc *doc = calloc(1, sizeof(*doc));
	doc->src = malloc(len + 1);
	memcpy(doc->src, src, len);
	doc->src[len] = '\0';
	doc->len = strlen(doc->src) < len ? strlen(doc->src) : len; /* stop at NUL */

	struct parser p = {.doc = doc, .line = 1};
	size_t cap = 0;
	for (;;) {
		struct tok *t = peek(&p);
		if (t->kind == TOK_EOF) {
			break;
		}
		if (t->kind == TOK_NEWLINE) {
			next(&p);
			continue;
		}
		struct twmrc_stmt st = {.line = t->line, .start = t->start, .end = t->end};
		struct tok first = next(&p);
		bool ok;
		if (first.kind == TOK_WORD && peek(&p)->kind == TOK_EQUALS) {
			struct tok eq = next(&p);
			(void)eq;
			st.kind = TWMRC_BUTTON;
			st.key = first.text;
			ok = parse_segments(&p, &st, true);
		} else if (first.kind == TOK_WORD) {
			st.kind = TWMRC_SETTING;
			st.key = first.text;
			ok = parse_segments(&p, &st, false);
		} else if (first.kind == TOK_STRING) {
			st.kind = TWMRC_KEY;
			st.key = first.text;
			if (peek(&p)->kind != TOK_COLON) {
				twmrc_diag(doc, TWMRC_ERROR, st.line,
					"expected ':' after key \"%s\"", st.key);
				ok = false;
			} else {
				next(&p);
				ok = parse_segments(&p, &st, true);
			}
		} else {
			if (first.kind != TOK_BAD) {
				twmrc_diag(doc, TWMRC_ERROR, st.line, "unexpected %s at line start",
					first.kind == TOK_NUMBER ? "number" :
					first.kind == TOK_COLOR ? "color" : tok_name(first.kind));
			}
			free(first.text);
			st.key = NULL;
			ok = false;
		}
		if (!ok) {
			free_stmt(&st);
			if (p.have && (p.cur.kind == TOK_NEWLINE || p.cur.kind == TOK_EOF)) {
				p.have = false;
			} else {
				skip_line(&p);
			}
			continue;
		}
		doc->stmts = grow(doc->stmts, &cap, doc->nstmts, sizeof(*doc->stmts));
		doc->stmts[doc->nstmts++] = st;
	}
	if (p.have) {
		free(p.cur.text);
	}
	return doc;
}

void twmrc_free(struct twmrc_doc *doc) {
	if (!doc) {
		return;
	}
	for (size_t i = 0; i < doc->nstmts; i++) {
		free_stmt(&doc->stmts[i]);
	}
	free(doc->stmts);
	free(doc->diags);
	free(doc->src);
	free(doc);
}
