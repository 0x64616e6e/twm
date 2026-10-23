#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_screencopy_v1.h>
#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/user.h>
#endif
#include "capture.h"
#include "rootline.h"
#include "server.h"
#include "ui.h"
#include "view.h"

#define IDLE_MS 2000  /* a capture "stops" this long after its last frame */
#define TICK_MS 500

struct capturer {
	struct wl_list link; /* twm_server.capturers */
	struct twm_server *server;
	struct wl_client *client;
	char output[32];
	uint64_t last_ms;
	int sessions;        /* open ext-image-copy-capture sessions */
	uint64_t sticky_key; /* CAP-3 root line, when it has no windows */
	struct wl_listener client_destroy;
};

struct session_watch {
	struct capturer *capturer;
	struct twm_server *server;
	struct wl_listener destroy;
};

static uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void capture_process_name(pid_t pid, char *buf, size_t len) {
	snprintf(buf, len, "pid %d", (int)pid);
#if defined(__linux__)
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
	FILE *f = fopen(path, "r");
	if (f) {
		if (fgets(buf, (int)len, f)) {
			buf[strcspn(buf, "\n")] = '\0';
		}
		fclose(f);
	}
#elif defined(__FreeBSD__)
	int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid};
	struct kinfo_proc kp;
	size_t sz = sizeof(kp);
	if (sysctl(mib, 4, &kp, &sz, NULL, 0) == 0 && sz == sizeof(kp)) {
		snprintf(buf, len, "%s", kp.ki_comm);
	}
#endif
}

bool capture_client_exempt(struct twm_server *server, struct wl_client *client) {
	if (!server->opts.test_mode || !client || server->startup_pid <= 0) {
		return false;
	}
	pid_t pid;
	wl_client_get_credentials(client, &pid, NULL, NULL);
	return pid == server->startup_pid;
}

static struct wl_client *view_client(struct twm_view *v) {
	return v->surface && v->surface->resource ? wl_resource_get_client(v->surface->resource) : NULL;
}

/* Recompute which windows carry the marker; windowless capturers get a
 * sticky root line (CAP-3). */
static void update(struct twm_server *server) {
	struct twm_view *v;
	wl_list_for_each(v, &server->views, link) {
		bool capturing = false;
		struct wl_client *c = view_client(v);
		struct capturer *cap;
		wl_list_for_each(cap, &server->capturers, link) {
			capturing |= c && cap->client == c;
		}
		if (capturing != v->capturing) {
			v->capturing = capturing;
			view_update_frame(v);
		}
	}
	struct capturer *cap;
	wl_list_for_each(cap, &server->capturers, link) {
		bool has_windows = false;
		wl_list_for_each(v, &server->views, link) {
			has_windows |= view_client(v) == cap->client;
		}
		if (!has_windows && !cap->sticky_key) {
			pid_t pid;
			wl_client_get_credentials(cap->client, &pid, NULL, NULL);
			char name[64], text[160];
			capture_process_name(pid, name, sizeof(name));
			snprintf(text, sizeof(text), "%s is recording %s", name, cap->output);
			cap->sticky_key = ++server->sticky_counter;
			rootline_stick(server, cap->sticky_key, ROOTLINE_ERROR, "capture", text);
		} else if (has_windows && cap->sticky_key) {
			rootline_unstick(server, cap->sticky_key);
			cap->sticky_key = 0;
		}
	}
}

static void capturer_free(struct twm_server *server, struct capturer *cap) {
	if (cap->sticky_key) {
		rootline_unstick(server, cap->sticky_key);
	}
	wl_list_remove(&cap->client_destroy.link);
	wl_list_remove(&cap->link);
	free(cap);
}

static void handle_client_destroy(struct wl_listener *listener, void *data) {
	struct capturer *cap = wl_container_of(listener, cap, client_destroy);
	struct twm_server *server = cap->server;
	capturer_free(server, cap);
	update(server);
	ui_changed(server);
}

static struct capturer *note(struct twm_server *server, struct wl_client *client,
		const char *output) {
	if (!client || capture_client_exempt(server, client)) {
		return NULL;
	}
	struct capturer *cap;
	wl_list_for_each(cap, &server->capturers, link) {
		if (cap->client == client) {
			cap->last_ms = now_ms();
			return cap;
		}
	}
	cap = calloc(1, sizeof(*cap));
	if (!cap) {
		return NULL;
	}
	cap->server = server;
	cap->client = client;
	cap->last_ms = now_ms();
	snprintf(cap->output, sizeof(cap->output), "%s", output);
	cap->client_destroy.notify = handle_client_destroy;
	wl_client_add_destroy_listener(client, &cap->client_destroy);
	wl_list_insert(&server->capturers, &cap->link);
	update(server);
	ui_changed(server);
	return cap;
}

static int handle_tick(void *data) {
	struct twm_server *server = data;
	uint64_t now = now_ms();
	struct capturer *cap, *tmp;
	bool changed = false;
	wl_list_for_each_safe(cap, tmp, &server->capturers, link) {
		if (cap->sessions == 0 && now - cap->last_ms > IDLE_MS) {
			capturer_free(server, cap); /* CAP-1: until capture stops */
			changed = true;
		}
	}
	if (changed) {
		update(server);
		ui_changed(server);
	}
	wl_event_source_timer_update(server->capture_timer, TICK_MS);
	return 0;
}

void capture_output_frame(struct twm_output *output) {
	struct twm_server *server = output->server;
	if (!server->screencopy) {
		return;
	}
	struct wlr_screencopy_frame_v1 *frame;
	wl_list_for_each(frame, &server->screencopy->frames, link) {
		if (frame->output == output->wlr_output && frame->resource) {
			note(server, wl_resource_get_client(frame->resource), output->wlr_output->name);
		}
	}
}

static void handle_session_destroy(struct wl_listener *listener, void *data) {
	struct session_watch *w = wl_container_of(listener, w, destroy);
	struct capturer *cap;
	wl_list_for_each(cap, &w->server->capturers, link) {
		if (cap == w->capturer && cap->sessions > 0) {
			cap->sessions--;
			cap->last_ms = now_ms();
		}
	}
	wl_list_remove(&w->destroy.link);
	free(w);
}

static void handle_new_session(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, new_capture_session);
	struct wlr_ext_image_copy_capture_session_v1 *session = data;
	struct capturer *cap = note(server, wl_resource_get_client(session->resource), "the screen");
	if (!cap) {
		return;
	}
	cap->sessions++;
	struct session_watch *w = calloc(1, sizeof(*w));
	if (!w) {
		return;
	}
	w->capturer = cap;
	w->server = server;
	w->destroy.notify = handle_session_destroy;
	wl_signal_add(&session->events.destroy, &w->destroy);
}

void capture_init(struct twm_server *server) {
	wl_list_init(&server->capturers);
	server->screencopy = wlr_screencopy_manager_v1_create(server->display);
	struct wlr_ext_image_copy_capture_manager_v1 *ext =
		wlr_ext_image_copy_capture_manager_v1_create(server->display, 1);
	wlr_ext_output_image_capture_source_manager_v1_create(server->display, 1);
	if (ext) {
		server->new_capture_session.notify = handle_new_session;
		wl_signal_add(&ext->events.new_session, &server->new_capture_session);
		server->ext_capture = ext;
	}
	server->capture_timer = wl_event_loop_add_timer(server->loop, handle_tick, server);
	wl_event_source_timer_update(server->capture_timer, TICK_MS);
}

void capture_finish(struct twm_server *server) {
	if (server->capture_timer) {
		wl_event_source_remove(server->capture_timer);
		server->capture_timer = NULL;
	}
	if (server->ext_capture) {
		wl_list_remove(&server->new_capture_session.link);
		server->ext_capture = NULL;
	}
	struct capturer *cap, *tmp;
	wl_list_for_each_safe(cap, tmp, &server->capturers, link) {
		wl_list_remove(&cap->client_destroy.link);
		wl_list_remove(&cap->link);
		free(cap);
	}
}
