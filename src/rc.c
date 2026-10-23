#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/util/log.h>
#include "desktop.h"
#include "frame.h"
#include "menu.h"
#include "portal.h"
#include "rc.h"
#include "rootline.h"
#include "server.h"
#include "settings.h"
#include "view.h"
#include "watch.h"

char *rc_path(const char *override) {
	if (override && *override) {
		return strdup(override);
	}
	const char *env = getenv("TWMRC");
	if (env && *env) {
		return strdup(env);
	}
	const char *home = getenv("HOME");
	char buf[4096];
	snprintf(buf, sizeof(buf), "%s/.twmrc", home && *home ? home : ".");
	return strdup(buf);
}

bool rc_read(const char *path, char **out, size_t *len, bool *missing) {
	*out = NULL;
	*len = 0;
	*missing = false;
	FILE *f = fopen(path, "rb");
	if (!f) {
		*missing = errno == ENOENT;
		return false;
	}
	size_t cap = 4096, n = 0;
	char *buf = malloc(cap);
	for (;;) {
		if (n + 1024 > cap) {
			cap *= 2;
			if (cap > (1u << 22)) { /* 4 MiB is not a config file */
				free(buf);
				fclose(f);
				errno = EFBIG;
				return false;
			}
			buf = realloc(buf, cap);
		}
		size_t got = fread(buf + n, 1, cap - n - 1, f);
		n += got;
		if (got == 0) {
			break;
		}
	}
	bool ok = !ferror(f);
	fclose(f);
	if (!ok) {
		free(buf);
		return false;
	}
	buf[n] = '\0';
	*out = buf;
	*len = n;
	return true;
}

static const char *basename_of(const char *path) {
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static int diag_cmp(const void *a, const void *b) {
	const struct twmrc_diag *x = a, *y = b;
	return x->line - y->line;
}

int rc_check(const char *path, FILE *out, FILE *err) {
	char *src;
	size_t len;
	bool missing;
	if (!rc_read(path, &src, &len, &missing)) {
		fprintf(err, "%s: %s\n", path, missing ? "no such file" : strerror(errno));
		return 1;
	}
	struct twmrc_doc *doc;
	struct twm_config *cfg = config_build(src, len, &doc);
	qsort(doc->diags, doc->ndiags, sizeof(*doc->diags), diag_cmp);
	for (size_t i = 0; i < doc->ndiags; i++) {
		const struct twmrc_diag *d = &doc->diags[i];
		fprintf(err, "%s:%d: %s%s\n", path, d->line,
			d->severity == TWMRC_WARNING ? "warning: " : "", d->message);
	}
	size_t errors = twmrc_count(doc, TWMRC_ERROR);
	size_t warnings = twmrc_count(doc, TWMRC_WARNING);
	if (errors == 0) {
		fprintf(out, "config ok \xc2\xb7 %zu warning%s\n", warnings, warnings == 1 ? "" : "s");
	}
	config_free(cfg);
	twmrc_free(doc);
	free(src);
	return errors ? 1 : 0;
}

static void theme_from_config(struct twm_theme *t, const struct twm_config *c) {
	t->accent = c->accent;
	snprintf(t->ui_font, sizeof(t->ui_font), "%s", c->font);
	t->font_size = c->font_size;
	t->title_height = c->title_height;
	t->border_width = c->border_width;
	t->title_highlight = c->title_highlight;
	t->root_weave = c->root_weave;
	t->root_wordmark = c->root_wordmark;
	t->focus_delay_ms = c->focus_delay;
}

void rc_apply(struct twm_server *server, const struct twm_config *old) {
	const struct twm_config *cfg = server->config;
	menu_close_all(server); /* open menus point into the old config */
	theme_from_config(&server->theme, cfg);
	server->theme_gen++;
	server->quiet = cfg->quiet; /* URG-3: the file decides again */

	if (old && old->xwayland != cfg->xwayland) {
		rootline_post(server, ROOTLINE_INFO, ".twmrc", "XWayland %s after a restart",
			cfg->xwayland ? "starts" : "stops");
	}
	output_apply_config(server);
	portal_config_changed(server); /* Notifications on/off */

	/* Roots: weave on/off; frames: colors, sizes, squeeze, NoTitle. */
	struct twm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		root_update(output);
	}
	root_update_text(server);
	desktops_reconfigure(server);
	struct twm_view *view;
	wl_list_for_each(view, &server->views, link) {
		view_relayout(view);
	}
	rootline_update(server);
}

/* One error line for the root line: the first error, B7's wording. */
static void post_errors(struct twm_server *server, struct twmrc_doc *doc,
		const char *kept) {
	const struct twmrc_diag *d = twmrc_first_error(doc);
	size_t more = twmrc_count(doc, TWMRC_ERROR) - 1;
	char extra[48] = "";
	if (more) {
		snprintf(extra, sizeof(extra), " (+%zu more)", more);
	}
	rootline_post(server, ROOTLINE_ERROR, basename_of(server->config_path),
		"line %d: %s%s \xc2\xb7 %s", d->line, d->message, extra, kept);
}

static void format_reply(struct twm_server *server, struct twmrc_doc *doc, bool applied,
		double ms, char *reply, size_t len) {
	if (!reply) {
		return;
	}
	size_t o = 0;
	reply[0] = '\0';
	if (doc) {
		qsort(doc->diags, doc->ndiags, sizeof(*doc->diags), diag_cmp);
		for (size_t i = 0; i < doc->ndiags && o < len; i++) {
			const struct twmrc_diag *d = &doc->diags[i];
			o += (size_t)snprintf(reply + o, len - o, "%s:%d: %s%s\n", server->config_path,
				d->line, d->severity == TWMRC_WARNING ? "warning: " : "", d->message);
		}
	}
	if (o >= len) {
		return;
	}
	if (applied) {
		size_t w = doc ? twmrc_count(doc, TWMRC_WARNING) : 0;
		snprintf(reply + o, len - o, "config ok \xc2\xb7 %zu warning%s \xc2\xb7 reloaded in %.1f ms\n",
			w, w == 1 ? "" : "s", ms);
	} else {
		snprintf(reply + o, len - o, "previous config kept\n");
	}
}

bool rc_reload(struct twm_server *server, bool force, char *reply, size_t reply_len) {
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	char *src;
	size_t len;
	bool missing;
	if (!rc_read(server->config_path, &src, &len, &missing)) {
		if (missing) {
			/* No file: the built-in defaults, as at startup (CFG-1). */
			if (!force && server->config_text == NULL) {
				format_reply(server, NULL, true, 0, reply, reply_len);
				return false;
			}
			src = strdup("");
			len = 0;
		} else {
			rootline_post(server, ROOTLINE_ERROR, basename_of(server->config_path),
				"%s \xc2\xb7 previous config kept", strerror(errno));
			if (reply) {
				snprintf(reply, reply_len, "%s: %s\nprevious config kept\n",
					server->config_path, strerror(errno));
			}
			return false;
		}
	}
	if (!force && server->config_text && strcmp(server->config_text, src) == 0) {
		free(src);
		format_reply(server, NULL, true, 0, reply, reply_len);
		return false; /* nothing changed (editors save several times) */
	}

	/* CFG-7: build and validate completely, then swap. */
	struct twmrc_doc *doc;
	struct twm_config *cfg = config_build(src, len, &doc);
	if (twmrc_count(doc, TWMRC_ERROR)) {
		post_errors(server, doc, "previous config kept"); /* CFG-6 */
		format_reply(server, doc, false, 0, reply, reply_len);
		config_free(cfg);
		twmrc_free(doc);
		free(src);
		return false;
	}
	struct twm_config *old = server->config;
	server->config = cfg;
	free(server->config_text);
	server->config_text = missing ? NULL : src;
	if (missing) {
		free(src);
	}
	rc_apply(server, old);
	config_free(old);

	clock_gettime(CLOCK_MONOTONIC, &t1);
	double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
		(double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
	size_t warnings = twmrc_count(doc, TWMRC_WARNING);
	rootline_post(server, ROOTLINE_INFO, basename_of(server->config_path),
		"reloaded \xc2\xb7 %zu warning%s", warnings, warnings == 1 ? "" : "s");
	format_reply(server, doc, true, ms, reply, reply_len);
	twmrc_free(doc);
	return true;
}

void rc_init(struct twm_server *server, const char *path_override) {
	server->config_path = rc_path(path_override);
	char *src = NULL;
	size_t len = 0;
	bool missing = false;
	bool have = rc_read(server->config_path, &src, &len, &missing);

	struct twmrc_doc *doc = NULL;
	struct twm_config *cfg = config_build(have ? src : NULL, len, &doc);
	if (have && twmrc_count(doc, TWMRC_ERROR)) {
		/* No previous config to keep at startup: the built-in defaults. */
		post_errors(server, doc, "built-in defaults used");
		config_free(cfg);
		cfg = config_build(NULL, 0, NULL);
		free(src);
		src = NULL;
	} else if (have && twmrc_count(doc, TWMRC_WARNING)) {
		size_t w = twmrc_count(doc, TWMRC_WARNING);
		const struct twmrc_diag *d = &doc->diags[0];
		rootline_post(server, ROOTLINE_INFO, basename_of(server->config_path),
			"%zu warning%s, first at line %d: %s", w, w == 1 ? "" : "s", d->line, d->message);
	} else if (!have && missing) {
		rootline_post(server, ROOTLINE_INFO, basename_of(server->config_path),
			"not found \xc2\xb7 built-in defaults");
	} else if (!have) {
		rootline_post(server, ROOTLINE_ERROR, basename_of(server->config_path),
			"%s \xc2\xb7 built-in defaults used", strerror(errno));
	}
	twmrc_free(doc);
	server->config = cfg;
	server->config_text = src;
	rc_apply(server, NULL);
	watch_init(server, server->config_path);
}

void rc_finish(struct twm_server *server) {
	watch_finish(server);
	config_free(server->config);
	server->config = NULL;
	free(server->config_text);
	server->config_text = NULL;
	free(server->config_path);
	server->config_path = NULL;
}
