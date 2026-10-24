#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include "config.h"
#include "desktop.h"
#include "group.h"
#include "info.h"
#include "rc.h"
#include "settings.h"
#include "view.h"

void info_count(struct twm_server *server, struct info_counts *c) {
	memset(c, 0, sizeof(*c));
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (v->type == TWM_VIEW_INTERNAL || !v->mapped) {
			continue;
		}
		c->open++;
		c->iconified += v->iconified;
		/* a group counts once: by its tab in front */
		c->groups += v->group && v->group->active == v && group_size(v->group) > 1;
	}
}

/* Lines in the file and whether it passes `twm --check` right now. */
static void file_state(struct twm_server *server, int *lines, const char **state) {
	char *src;
	size_t len;
	bool missing;
	*lines = -1;
	*state = "missing";
	if (!server->config_path || !rc_read(server->config_path, &src, &len, &missing)) {
		return;
	}
	*lines = 0;
	for (size_t i = 0; i < len; i++) {
		*lines += src[i] == '\n';
	}
	if (len && src[len - 1] != '\n') {
		(*lines)++;
	}
	struct twmrc_doc *doc;
	struct twm_config *cfg = config_build(src, len, &doc);
	*state = twmrc_count(doc, TWMRC_ERROR) ? "errors" : "ok";
	config_free(cfg);
	twmrc_free(doc);
	free(src);
}

#define ADD(...) do { \
	if (o < len) { \
		o += (size_t)snprintf(out + o, len - o, __VA_ARGS__); \
	} \
} while (0)

void info_write(struct twm_server *server, char *out, size_t len) {
	size_t o = 0;
	out[0] = '\0';
	const struct twm_config *cfg = server->config;
	ADD("version\t%s\n", TWM_VERSION);
#if HAVE_XWAYLAND
	ADD("xwayland\t%s\n", server->xwayland ? "on" : "off");
#else
	ADD("xwayland\toff\n");
#endif
	struct info_counts c;
	info_count(server, &c);
	ADD("frames\t%d\t%d\t%d\n", c.open, c.iconified, c.groups);
	int quiet = 0;
	for (int d = 1; d <= desktop_count(server); d++) {
		quiet += (server->quiet >> (d - 1)) & 1;
	}
	ADD("desktops\t%d\t%d\t%d\n", desktop_count(server), server->desktop, quiet);
	struct twm_output *op;
	wl_list_for_each(op, &server->outputs, link) {
		struct wlr_output *wo = op->wlr_output;
		ADD("output\t%s\t%d\t%d\t%g\n", wo->name, wo->width, wo->height, (double)wo->scale);
	}
	ADD("focus\t%s\t%d\n", cfg->focus_policy == TWM_FOCUS_CLICK ? "click" : "sloppy",
		cfg->focus_delay);
	ADD("font\t%s\t%d\n", server->theme.ui_font, server->theme.font_size);
	int lines;
	const char *state;
	file_state(server, &lines, &state);
	ADD("twmrc\t%s\t%d\t%s\t%lld\n", server->config_path ? server->config_path : "",
		lines, state, (long long)server->config_applied);
	const struct twm_theme *t = &server->theme;
	struct { const char *name; uint32_t rgb; } colors[] = {
		{"ground", t->ground}, {"surface", t->surface}, {"graphite", t->graphite},
		{"hairline", t->hairline}, {"hairline_strong", t->hairline_strong},
		{"ink", t->ink}, {"ink_2", t->ink_2}, {"muted", t->muted}, {"dim", t->dim},
		{"accent", t->accent}, {"on_accent", t->on_accent}, {"ok", t->ok},
		{"alert", t->alert},
	};
	for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
		ADD("color\t%s\t%06X\n", colors[i].name, colors[i].rgb & 0xFFFFFF);
	}
}
