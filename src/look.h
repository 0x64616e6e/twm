#ifndef TWM_LOOK_H
#define TWM_LOOK_H

#include <stdbool.h>

/*
 * Sharing a look (SHR-*), pure: from a .twmrc carried in a fetch shot, the
 * appearance keys alone (SHR-2) — never a binding, menu or command — and
 * the recipient's file with them merged in.
 */

#define LOOK_MAX 16

struct look_edit {
	char key[32];
	char value[512]; /* value text, as written */
};

struct look {
	struct look_edit e[LOOK_MAX];
	int n;
	int refused; /* statements left out: not appearance */
};

/* Whether a shared look may set `key` (SHR-2). */
bool look_key_allowed(const char *key);

/* The appearance keys of a shared .twmrc, last statement winning. */
void look_extract(const char *shared, struct look *out);

/* `mine` with the look's keys set (comments and order kept). NULL if the
 * result has errors or would change anything but appearance keys. */
char *look_merge(const char *mine, const struct look *look);

/* Every statement other than an appearance key is the same in both. */
bool look_only_appearance(const char *before, const char *after);

#endif
