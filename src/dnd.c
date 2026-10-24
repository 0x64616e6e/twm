#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "dnd.h"
#include "server.h"
#include "share.h"

struct uri_read {
	struct twm_server *server;
	int fd;
	struct wl_event_source *source;
	char buf[8192];
	size_t len;
};

static void handle_request_start_drag(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, request_start_drag);
	struct wlr_seat_request_start_drag_event *ev = data;
	/* Only a drag that starts from a press the client really got, and never
	 * while a trusted prompt holds the input (TRU-4). */
	if (!server->prompt && !server->locked && wlr_seat_validate_pointer_grab_serial(server->seat, ev->origin, ev->serial)) {
		wlr_seat_start_pointer_drag(server->seat, ev->drag, ev->serial);
		return;
	}
	if (ev->drag->source) {
		wlr_data_source_destroy(ev->drag->source);
	}
}

static void handle_drag_destroy(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, drag_destroy);
	wl_list_remove(&server->drag_destroy.link);
	wl_list_init(&server->drag_destroy.link);
	server->drag_icon = NULL; /* the scene tree goes with the icon */
	input_update_pointer_focus(server, 0);
}

static void handle_start_drag(struct wl_listener *listener, void *data) {
	struct twm_server *server = wl_container_of(listener, server, start_drag);
	struct wlr_drag *drag = data;
	if (drag->icon) {
		server->drag_icon = wlr_scene_drag_icon_create(server->layers[TWM_LAYER_DRAG], drag->icon);
		dnd_motion(server);
	}
	wl_list_remove(&server->drag_destroy.link);
	server->drag_destroy.notify = handle_drag_destroy;
	wl_signal_add(&drag->events.destroy, &server->drag_destroy);
}

void dnd_motion(struct twm_server *server) {
	if (server->drag_icon) {
		wlr_scene_node_set_position(&server->drag_icon->node,
			(int)server->cursor->x, (int)server->cursor->y);
	}
}

static int hexval(char c) {
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
		c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The first file:// path of a text/uri-list, decoded; false if none. */
static bool first_file(const char *list, char *out, size_t len) {
	for (const char *l = list; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		if (*l == '#' || strncmp(l, "file://", 7) != 0) {
			continue;
		}
		const char *p = l + 7;
		p = strchr(p, '/'); /* past an optional host */
		if (!p) {
			continue;
		}
		size_t o = 0;
		for (; *p && *p != '\r' && *p != '\n' && o + 1 < len; p++) {
			int hi, lo;
			if (*p == '%' && (hi = hexval(p[1])) >= 0 && (lo = hexval(p[2])) >= 0) {
				out[o++] = (char)(hi << 4 | lo);
				p += 2;
			} else {
				out[o++] = *p;
			}
		}
		out[o] = '\0';
		return o > 0;
	}
	return false;
}

static void uri_done(struct uri_read *r) {
	r->buf[r->len] = '\0';
	char path[4096], reply[1024];
	if (first_file(r->buf, path, sizeof(path))) {
		share_absorb(r->server, path, reply, sizeof(reply)); /* it says on the root line */
	}
	wl_event_source_remove(r->source);
	close(r->fd);
	free(r);
}

static int uri_readable(int fd, uint32_t mask, void *data) {
	struct uri_read *r = data;
	if (mask & WL_EVENT_READABLE) {
		ssize_t n = read(fd, r->buf + r->len, sizeof(r->buf) - 1 - r->len);
		if (n > 0) {
			r->len += (size_t)n;
			if (r->len < sizeof(r->buf) - 1) {
				return 0;
			}
		} else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			return 0;
		}
	}
	uri_done(r);
	return 0;
}

void dnd_release(struct twm_server *server) {
	struct wlr_drag *drag = server->seat->drag;
	if (!drag || !drag->source) {
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct twm_hit hit = server_hit_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (surface || (hit.kind != TWM_HIT_ROOT && hit.kind != TWM_HIT_CARD)) {
		return; /* a client's drop: wlroots carries it */
	}
	bool uris = false;
	char **mime;
	wl_array_for_each(mime, &drag->source->mime_types) {
		uris |= strcmp(*mime, "text/uri-list") == 0;
	}
	if (!uris) {
		return;
	}
	int fds[2];
	if (pipe(fds) < 0) {
		return;
	}
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	struct uri_read *r = calloc(1, sizeof(*r));
	if (!r) {
		close(fds[0]);
		close(fds[1]);
		return;
	}
	r->server = server;
	r->fd = fds[0];
	r->source = wl_event_loop_add_fd(server->loop, fds[0], WL_EVENT_READABLE, uri_readable, r);
	/* SHR-1: the source writes its files; wlroots closes our end */
	wlr_data_source_send(drag->source, "text/uri-list", fds[1]);
}

void dnd_init(struct twm_server *server) {
	wl_list_init(&server->drag_destroy.link);
	server->request_start_drag.notify = handle_request_start_drag;
	wl_signal_add(&server->seat->events.request_start_drag, &server->request_start_drag);
	server->start_drag.notify = handle_start_drag;
	wl_signal_add(&server->seat->events.start_drag, &server->start_drag);
}

void dnd_finish(struct twm_server *server) {
	wl_list_remove(&server->request_start_drag.link);
	wl_list_remove(&server->start_drag.link);
	wl_list_remove(&server->drag_destroy.link);
}
