#ifndef TWM_RCEDIT_H
#define TWM_RCEDIT_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Editing .twmrc text the way a person would (SET-7): change only the value
 * that changed, keep comments, spacing and order, append new keys at the end.
 * And the unified diff the Settings window shows (SET-5). Pure C, no
 * wlroots; unit-tested.
 */

/*
 * Set `key` to `value` (value text as it would be typed: `28`, `on`,
 * `{ edges thirds }`, `"foot"`). The last statement for the key is changed
 * in place; without one, `Key<pad>value` is appended. Returns a malloc'd
 * string.
 */
char *rcedit_set(const char *src, const char *key, const char *value);

/*
 * Set one row of a block-valued key: in `key { … }`, the row starting with
 * `head` (e.g. `"*"` in SqueezeTitle) gets `value` as its second item. The
 * row is added to the block if missing, and the key appended if missing.
 */
char *rcedit_set_row(const char *src, const char *key, const char *head, const char *value);

/* The value text of the last statement for `key`, or NULL (malloc'd). */
char *rcedit_get(const char *src, const char *key);

/* --- the diff --- */

enum rcdiff_kind {
	RCDIFF_SAME = ' ',
	RCDIFF_DEL = '-',
	RCDIFF_ADD = '+',
	RCDIFF_GAP = '~', /* lines skipped between hunks ("…") */
};

struct rcdiff_line {
	enum rcdiff_kind kind;
	int number;        /* old line for '-', new line otherwise; 0 for gaps */
	const char *text;  /* points into the source text; not NUL-terminated */
	int len;
};

struct rcdiff {
	struct rcdiff_line *lines;
	int n;
	int added, removed;
};

/* Line diff of a → b with `context` lines around each change. */
void rcdiff_compute(struct rcdiff *d, const char *a, const char *b, int context);
void rcdiff_free(struct rcdiff *d);

#endif
