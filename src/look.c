#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "look.h"
#include "rcedit.h"
#include "settings.h"
#include "twmrc.h"

/* SHR-2: how it looks, never what it does. */
static const char *const allowed[] = {
	"Theme", "Accent", "Font", "TitleHeight", "BorderWidth", "TitleHighlight",
	"SqueezeTitle", "CloseBox", "RootWeave", "RootWordmark", "RootCard", "IconManager",
	"IconsOnRoot", "Effects",
};

bool look_key_allowed(const char *key) {
	for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
		if (strcmp(key, allowed[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* A value that holds a command anywhere (!"…") is never taken. */
static bool row_has_exec(const struct twmrc_row *r) {
	for (size_t i = 0; i < r->n; i++) {
		const struct twmrc_node *n = &r->nodes[i];
		if (n->kind == TWMRC_EXEC) {
			return true;
		}
		for (size_t k = 0; n->kind == TWMRC_BLOCK && k < n->nrows; k++) {
			if (row_has_exec(&n->rows[k])) {
				return true;
			}
		}
	}
	return false;
}

void look_extract(const char *shared, struct look *out) {
	memset(out, 0, sizeof(*out));
	struct twmrc_doc *doc = twmrc_parse(shared, strlen(shared));
	for (size_t i = 0; i < doc->nstmts; i++) {
		const struct twmrc_stmt *st = &doc->stmts[i];
		bool ok = st->kind == TWMRC_SETTING && look_key_allowed(st->key);
		for (size_t s = 0; ok && s < st->nsegments; s++) {
			ok = !row_has_exec(&st->segments[s]);
		}
		if (!ok) {
			out->refused++;
			continue;
		}
		char *v = rcedit_get(shared, st->key); /* the last one wins */
		if (!v) {
			continue;
		}
		struct look_edit *e = NULL;
		for (int k = 0; k < out->n; k++) {
			if (strcmp(out->e[k].key, st->key) == 0) {
				e = &out->e[k];
			}
		}
		if (!e && out->n < LOOK_MAX) {
			e = &out->e[out->n++];
		}
		if (e && strlen(v) < sizeof(e->value)) {
			snprintf(e->key, sizeof(e->key), "%s", st->key);
			snprintf(e->value, sizeof(e->value), "%s", v);
		}
		free(v);
	}
	twmrc_free(doc);
}

/* The source text of every statement that isn't an appearance setting,
 * joined: equal for two files means only the look differs. */
static char *rest_of(const char *src) {
	struct twmrc_doc *doc = twmrc_parse(src, strlen(src));
	size_t cap = strlen(src) + 1, o = 0;
	char *out = malloc(cap + doc->nstmts + 1);
	if (!out) {
		twmrc_free(doc);
		return NULL;
	}
	for (size_t i = 0; i < doc->nstmts; i++) {
		const struct twmrc_stmt *st = &doc->stmts[i];
		if (st->kind == TWMRC_SETTING && look_key_allowed(st->key)) {
			continue;
		}
		memcpy(out + o, src + st->start, st->end - st->start);
		o += st->end - st->start;
		out[o++] = '\n';
	}
	out[o] = '\0';
	twmrc_free(doc);
	return out;
}

bool look_only_appearance(const char *before, const char *after) {
	char *a = rest_of(before), *b = rest_of(after);
	bool same = a && b && strcmp(a, b) == 0;
	free(a);
	free(b);
	return same;
}

char *look_merge(const char *mine, const struct look *look) {
	char *text = strdup(mine);
	for (int i = 0; text && i < look->n; i++) {
		char *next = rcedit_set(text, look->e[i].key, look->e[i].value);
		free(text);
		text = next;
	}
	if (!text) {
		return NULL;
	}
	struct twmrc_doc *doc;
	struct twm_config *cfg = config_build(text, strlen(text), &doc);
	bool ok = twmrc_count(doc, TWMRC_ERROR) == 0 && look_only_appearance(mine, text);
	config_free(cfg);
	twmrc_free(doc);
	if (!ok) {
		free(text);
		return NULL;
	}
	return text;
}
