#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "geometry.h"
#include "rc.h"
#include "remember.h"
#include "rootline.h"
#include "server.h"
#include "settings.h"
#include "view.h"

#define SAVE_DELAY_MS 1000 /* CFG-11: at most once a second */

static char *state_path(void) {
	char buf[4096];
	const char *state = getenv("XDG_STATE_HOME");
	const char *home = getenv("HOME");
	if (state && *state) {
		snprintf(buf, sizeof(buf), "%s/twm/geometry", state);
	} else if (home && *home) {
		snprintf(buf, sizeof(buf), "%s/.local/state/twm/geometry", home);
	} else {
		return NULL;
	}
	return strdup(buf);
}

/* mkdir -p for the file's directory. */
static void make_parents(const char *path) {
	char *dir = strdup(path);
	if (!dir) {
		return;
	}
	for (char *p = dir + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(dir, 0700);
			*p = '/';
		}
	}
	free(dir);
}

static void save(struct twm_server *server) {
	if (!server->geomem_path || !server->geomem_dirty) {
		return;
	}
	server->geomem_dirty = false;
	char *text = geomem_serialize(&server->geomem);
	if (!text) {
		return;
	}
	make_parents(server->geomem_path);
	char tmp[4200];
	snprintf(tmp, sizeof(tmp), "%s.%d.tmp", server->geomem_path, (int)getpid());
	FILE *f = fopen(tmp, "w");
	bool ok = f && fputs(text, f) >= 0;
	if (f) {
		ok = (fclose(f) == 0) && ok;
	}
	if (ok && rename(tmp, server->geomem_path) == 0) {
		wlr_log(WLR_DEBUG, "saved %s", server->geomem_path);
	} else {
		wlr_log(WLR_ERROR, "can't save %s: %s", server->geomem_path, strerror(errno));
		unlink(tmp);
	}
	free(text);
}

static int handle_timer(void *data) {
	save(data);
	return 0;
}

void remember_init(struct twm_server *server) {
	server->geomem_path = state_path();
	server->geomem_timer = wl_event_loop_add_timer(server->loop, handle_timer, server);
	if (!server->geomem_path) {
		return;
	}
	char *text;
	size_t len;
	bool missing;
	if (rc_read(server->geomem_path, &text, &len, &missing)) {
		geomem_parse(&server->geomem, text);
		free(text);
	}
}

void remember_finish(struct twm_server *server) {
	save(server);
	if (server->geomem_timer) {
		wl_event_source_remove(server->geomem_timer);
		server->geomem_timer = NULL;
	}
	geomem_clear(&server->geomem);
	free(server->geomem_path);
	server->geomem_path = NULL;
}

static bool rememberable(struct twm_server *server, struct twm_view *view) {
	if (!server->config->remember_geometry) {
		return false;
	}
	const char *app_id = view->impl->get_app_id(view);
	if (!app_id || !*app_id) {
		return false;
	}
	/* Dialogs follow their parent (OUT-15), not memory. */
	return !(view->impl->get_parent && view->impl->get_parent(view));
}

void remember_record(struct twm_server *server, struct twm_view *view) {
	if (!view->shown || !rememberable(server, view)) {
		return;
	}
	struct twm_output *output = output_at(server, view->x, view->y);
	if (!output && !wl_list_empty(&server->outputs)) {
		output = wl_container_of(server->outputs.next, output, link);
	}
	if (!output) {
		return;
	}
	geomem_set(&server->geomem, view->impl->get_app_id(view), output->wlr_output->name,
		view->x - output->box.x, view->y - output->box.y, view->width, view->height);
	server->geomem_dirty = true;
	wl_event_source_timer_update(server->geomem_timer, SAVE_DELAY_MS);
}

bool remember_initial_size(struct twm_server *server, const char *app_id, int *w, int *h) {
	if (!server->config->remember_geometry || !app_id || !*app_id) {
		return false;
	}
	const struct geomem_entry *e = geomem_find(&server->geomem, app_id);
	if (!e) {
		return false;
	}
	*w = e->w;
	*h = e->h;
	return true;
}

static struct twm_output *output_named(struct twm_server *server, const char *name) {
	struct twm_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (strcmp(output->wlr_output->name, name) == 0) {
			return output;
		}
	}
	return NULL;
}

bool remember_restore(struct twm_server *server, struct twm_view *view) {
	if (!rememberable(server, view)) {
		return false;
	}
	const char *app_id = view->impl->get_app_id(view);
	const struct geomem_entry *e = geomem_find(&server->geomem, app_id);
	if (!e) {
		return false;
	}
	struct twm_output *output = output_named(server, e->output);
	if (!output) {
		return false; /* that output is gone: place it by hand instead */
	}
	struct geomem_entry entry = *e;
	/* Keep the top-left on the output, whatever changed since. */
	struct wlr_box ob = output->box;
	int x = entry.x, y = entry.y;
	if (x > ob.width - 40) {
		x = ob.width - 40;
	}
	if (y > ob.height - 40) {
		y = ob.height - 40;
	}
	if (x < 0) {
		x = 0;
	}
	if (y < 0) {
		y = 0;
	}
	view->notitle = config_notitle(server->config, app_id, view->impl->get_title(view));
	view_move_resize(view, ob.x + x, ob.y + y, entry.w, entry.h);
	view->width = entry.w; /* the client follows; show the frame at the size it'll have */
	view->height = entry.h;
	view_show(view, false);

	struct wlr_box f = view_frame_box(view);
	char geometry[64];
	geo_format(geometry, sizeof(geometry), f.width, f.height, x, y);
	/* B7: "restore  firefox → 1120×900+40+60, from its last session" */
	rootline_post(server, ROOTLINE_INFO, "restore", "%s \xe2\x86\x92 %s, from %s",
		app_id, geometry, entry.from_disk ? "its last session" : "earlier");
	return true;
}
