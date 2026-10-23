#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rcedit.h"
#include "twmrc.h"

/* Appended keys line up their values here, as B8's diff does. */
#define KEY_COLUMN 14

static char *splice(const char *src, size_t from, size_t to, const char *with) {
	size_t len = strlen(src), wl = strlen(with);
	char *out = malloc(len - (to - from) + wl + 1);
	memcpy(out, src, from);
	memcpy(out + from, with, wl);
	memcpy(out + from + wl, src + to, len - to + 1);
	return out;
}

static const struct twmrc_stmt *last_setting(const struct twmrc_doc *doc, const char *key) {
	const struct twmrc_stmt *found = NULL;
	for (size_t i = 0; i < doc->nstmts; i++) {
		const struct twmrc_stmt *st = &doc->stmts[i];
		if (st->kind == TWMRC_SETTING && strcmp(st->key, key) == 0) {
			found = st;
		}
	}
	return found;
}

static char *append(const char *src, const char *key, const char *value) {
	size_t len = strlen(src);
	int pad = (int)strlen(key) < KEY_COLUMN ? KEY_COLUMN - (int)strlen(key) : 1;
	size_t n = len + strlen(key) + (size_t)pad + strlen(value) + 3;
	char *out = malloc(n);
	snprintf(out, n, "%s%s%s%*s%s\n", src, len && src[len - 1] != '\n' ? "\n" : "", key,
		pad, "", value);
	return out;
}

/* The span of a setting's values; for a bare key, the point after it. */
static void value_span(const struct twmrc_stmt *st, size_t *from, size_t *to) {
	if (st->nsegments > 0 && st->segments[0].n > 0) {
		*from = st->segments[0].nodes[0].start;
		*to = st->segments[0].nodes[st->segments[0].n - 1].end;
	} else {
		*from = *to = st->start + strlen(st->key);
	}
}

char *rcedit_set(const char *src, const char *key, const char *value) {
	struct twmrc_doc *doc = twmrc_parse(src, strlen(src));
	const struct twmrc_stmt *st = last_setting(doc, key);
	char *out;
	if (st) {
		size_t from, to;
		value_span(st, &from, &to);
		if (from == to) {
			char with[512];
			snprintf(with, sizeof(with), " %s", value);
			out = splice(doc->src, from, to, with);
		} else {
			out = splice(doc->src, from, to, value);
		}
	} else {
		out = append(doc->src, key, value);
	}
	twmrc_free(doc);
	return out;
}

char *rcedit_get(const char *src, const char *key) {
	struct twmrc_doc *doc = twmrc_parse(src, strlen(src));
	const struct twmrc_stmt *st = last_setting(doc, key);
	char *out = NULL;
	if (st) {
		size_t from, to;
		value_span(st, &from, &to);
		out = strndup(doc->src + from, to - from);
	}
	twmrc_free(doc);
	return out;
}

static bool node_is(const struct twmrc_node *n, const char *text) {
	return (n->kind == TWMRC_STRING || n->kind == TWMRC_WORD) && n->text &&
		strcmp(n->text, text) == 0;
}

char *rcedit_set_row(const char *src, const char *key, const char *head, const char *value) {
	/* `head` is given as typed ("\"*\""); match on its decoded text. */
	char want[128];
	snprintf(want, sizeof(want), "%s", head);
	size_t wl = strlen(want);
	if (wl >= 2 && want[0] == '"' && want[wl - 1] == '"') {
		memmove(want, want + 1, wl - 2);
		want[wl - 2] = '\0';
	}
	struct twmrc_doc *doc = twmrc_parse(src, strlen(src));
	const struct twmrc_stmt *st = last_setting(doc, key);
	char *out = NULL;
	char row[256];
	snprintf(row, sizeof(row), "%s %s", head, value);
	if (!st || st->nsegments == 0 || st->segments[0].n != 1 ||
			st->segments[0].nodes[0].kind != TWMRC_BLOCK) {
		/* No block to edit: write the whole value as a one-row block. */
		char block[300];
		snprintf(block, sizeof(block), "{ %s }", row);
		twmrc_free(doc);
		return rcedit_set(src, key, block);
	}
	const struct twmrc_node *blk = &st->segments[0].nodes[0];
	/* Rows are lines; a one-line block holds every pair in one row. */
	for (size_t i = blk->nrows; i-- > 0;) {
		const struct twmrc_row *r = &blk->rows[i];
		for (size_t k = r->n; k-- > 0;) {
			if (k % 2 != 0 || !node_is(&r->nodes[k], want)) {
				continue; /* patterns sit at even places: pattern value … */
			}
			if (k + 1 < r->n) {
				out = splice(doc->src, r->nodes[k + 1].start, r->nodes[k + 1].end, value);
			} else {
				char with[200];
				snprintf(with, sizeof(with), " %s", value);
				out = splice(doc->src, r->nodes[k].end, r->nodes[k].end, with);
			}
			twmrc_free(doc);
			return out;
		}
	}
	/* Add the row before the closing brace, in the block's own layout. */
	size_t close = blk->end - 1;
	bool multiline = memchr(doc->src + blk->start, '\n', blk->end - blk->start) != NULL;
	if (!multiline) {
		size_t at = close;
		while (at > blk->start + 1 && doc->src[at - 1] == ' ') {
			at--;
		}
		char with[300];
		snprintf(with, sizeof(with), "%s%s ", at == blk->start + 1 ? " " : "  ", row);
		out = splice(doc->src, at, close, with);
	} else {
		/* Indent like the block's first row, on a line of its own. */
		char indent[64] = "\t";
		if (blk->nrows > 0) {
			size_t s = blk->rows[0].nodes[0].start, b = s;
			while (b > 0 && (doc->src[b - 1] == ' ' || doc->src[b - 1] == '\t')) {
				b--;
			}
			if (b > 0 && doc->src[b - 1] == '\n' && s - b < sizeof(indent)) {
				memcpy(indent, doc->src + b, s - b);
				indent[s - b] = '\0';
			}
		}
		size_t line = close;
		while (line > 0 && doc->src[line - 1] != '\n') {
			line--;
		}
		bool alone = true; /* the brace starts its own line */
		for (size_t k = line; k < close; k++) {
			alone &= doc->src[k] == ' ' || doc->src[k] == '\t';
		}
		char with[400];
		if (alone) {
			snprintf(with, sizeof(with), "%s%s\n", indent, row);
			out = splice(doc->src, line, line, with);
		} else {
			snprintf(with, sizeof(with), "\n%s%s\n", indent, row);
			out = splice(doc->src, close, close, with);
		}
	}
	twmrc_free(doc);
	return out;
}

/* --- diff --- */

struct lines {
	const char **s;
	int *len;
	int n;
};

static void split(const char *t, struct lines *l) {
	int cap = 16;
	l->s = malloc(sizeof(*l->s) * cap);
	l->len = malloc(sizeof(*l->len) * cap);
	l->n = 0;
	while (*t) {
		const char *nl = strchr(t, '\n');
		int len = nl ? (int)(nl - t) : (int)strlen(t);
		if (l->n == cap) {
			cap *= 2;
			l->s = realloc(l->s, sizeof(*l->s) * cap);
			l->len = realloc(l->len, sizeof(*l->len) * cap);
		}
		l->s[l->n] = t;
		l->len[l->n] = len;
		l->n++;
		t += len + (nl ? 1 : 0);
	}
}

static bool same(const struct lines *a, int i, const struct lines *b, int j) {
	return a->len[i] == b->len[j] && memcmp(a->s[i], b->s[j], a->len[i]) == 0;
}

struct op {
	enum rcdiff_kind kind;
	int ai, bi;
};

static void push(struct rcdiff *d, int *cap, enum rcdiff_kind k, int number,
		const char *text, int len) {
	if (d->n == *cap) {
		*cap = *cap ? *cap * 2 : 32;
		d->lines = realloc(d->lines, sizeof(*d->lines) * *cap);
	}
	d->lines[d->n++] = (struct rcdiff_line){k, number, text, len};
}

void rcdiff_compute(struct rcdiff *d, const char *a, const char *b, int context) {
	*d = (struct rcdiff){0};
	struct lines la, lb;
	split(a, &la);
	split(b, &lb);
	/* Common prefix and suffix first; the middle is small for edits. */
	int pre = 0;
	while (pre < la.n && pre < lb.n && same(&la, pre, &lb, pre)) {
		pre++;
	}
	int suf = 0;
	while (suf < la.n - pre && suf < lb.n - pre &&
			same(&la, la.n - 1 - suf, &lb, lb.n - 1 - suf)) {
		suf++;
	}
	int n = la.n - pre - suf, m = lb.n - pre - suf;
	int nops = 0;
	struct op *ops = malloc(sizeof(*ops) * (size_t)(la.n + lb.n + 1));
	for (int i = 0; i < pre; i++) {
		ops[nops++] = (struct op){RCDIFF_SAME, i, i};
	}
	if ((long)n * m <= 4000000L && n > 0 && m > 0) {
		/* LCS table over the middle. */
		int *t = calloc((size_t)(n + 1) * (m + 1), sizeof(int));
#define T(i, j) t[(size_t)(i) * (m + 1) + (j)]
		for (int i = n - 1; i >= 0; i--) {
			for (int j = m - 1; j >= 0; j--) {
				T(i, j) = same(&la, pre + i, &lb, pre + j) ? T(i + 1, j + 1) + 1 :
					(T(i + 1, j) > T(i, j + 1) ? T(i + 1, j) : T(i, j + 1));
			}
		}
		int i = 0, j = 0;
		while (i < n || j < m) {
			if (i < n && j < m && same(&la, pre + i, &lb, pre + j)) {
				ops[nops++] = (struct op){RCDIFF_SAME, pre + i++, pre + j++};
			} else if (i < n && (j == m || T(i + 1, j) >= T(i, j + 1))) {
				ops[nops++] = (struct op){RCDIFF_DEL, pre + i++, pre + j};
			} else {
				ops[nops++] = (struct op){RCDIFF_ADD, pre + i, pre + j++};
			}
		}
#undef T
		free(t);
	} else {
		for (int i = 0; i < n; i++) {
			ops[nops++] = (struct op){RCDIFF_DEL, pre + i, pre};
		}
		for (int j = 0; j < m; j++) {
			ops[nops++] = (struct op){RCDIFF_ADD, pre + n, pre + j};
		}
	}
	for (int k = 0; k < suf; k++) {
		ops[nops++] = (struct op){RCDIFF_SAME, la.n - suf + k, lb.n - suf + k};
	}

	/* Keep changes and `context` lines around them; mark the gaps. */
	int cap = 0;
	int last_kept = -1;
	for (int k = 0; k < nops; k++) {
		bool keep = ops[k].kind != RCDIFF_SAME;
		for (int c = 1; !keep && c <= context; c++) {
			keep = (k - c >= 0 && ops[k - c].kind != RCDIFF_SAME) ||
				(k + c < nops && ops[k + c].kind != RCDIFF_SAME);
		}
		if (!keep) {
			continue;
		}
		if (k > last_kept + 1 && d->n > 0) {
			push(d, &cap, RCDIFF_GAP, 0, NULL, 0);
		}
		last_kept = k;
		const struct op *o = &ops[k];
		if (o->kind == RCDIFF_DEL) {
			push(d, &cap, RCDIFF_DEL, o->ai + 1, la.s[o->ai], la.len[o->ai]);
			d->removed++;
		} else {
			push(d, &cap, o->kind, o->bi + 1, lb.s[o->bi], lb.len[o->bi]);
			d->added += o->kind == RCDIFF_ADD;
		}
	}
	free(ops);
	free(la.s);
	free(la.len);
	free(lb.s);
	free(lb.len);
}

void rcdiff_free(struct rcdiff *d) {
	free(d->lines);
	*d = (struct rcdiff){0};
}
