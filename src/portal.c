#include "config.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/util/log.h>
#include SDBUS_HEADER
#include "portal.h"
#include "prompt.h"
#include "rootline.h"
#include "server.h"
#include "settings.h"

#define PORTAL_NAME "org.freedesktop.impl.portal.desktop.twm"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define ACCESS_IFACE "org.freedesktop.impl.portal.Access"
#define REQUEST_IFACE "org.freedesktop.impl.portal.Request"
#define NOTIFY_NAME "org.freedesktop.Notifications"
#define NOTIFY_PATH "/org/freedesktop/Notifications"

/* org.freedesktop.impl.portal response codes */
#define RESPONSE_GRANTED 0
#define RESPONSE_CANCELLED 1
#define RESPONSE_OTHER 2

struct portal {
	struct twm_server *server;
	sd_bus *bus;
	struct wl_event_source *fd_source, *timer, *idle;
	sd_bus_slot *access_slot, *notify_slot;
	bool notifications; /* we own org.freedesktop.Notifications */
	uint32_t notify_id;
	struct wl_list requests; /* access_req.link */
};

/* One AccessDialog call waiting on its prompt. */
struct access_req {
	struct wl_list link;
	struct portal *portal;
	sd_bus_message *call;
	sd_bus_slot *request_slot; /* the Request object at the call's handle */
	struct twm_prompt *prompt;
	/* The first choice with options becomes the rows; the first boolean
	 * choice, the checkbox. Their ids, to answer with. */
	char rows_id[64];
	char row_ids[PROMPT_MAX_CHOICES][64];
	char check_id[64];
};

static struct portal *portal_of(struct twm_server *server) {
	return server->portal;
}

/* --- the event loop --- */

static void pump(struct portal *p);

static int handle_fd(int fd, uint32_t mask, void *data) {
	pump(data);
	return 0;
}

static int handle_timer(void *data) {
	pump(data);
	return 0;
}

static void handle_idle(void *data) {
	struct portal *p = data;
	p->idle = NULL;
	pump(p);
}

/* Replies sent from outside a dispatch go out on the next idle. */
static void schedule(struct portal *p) {
	if (!p->idle && p->server->loop) {
		p->idle = wl_event_loop_add_idle(p->server->loop, handle_idle, p);
	}
}

static void pump(struct portal *p) {
	int r;
	while ((r = sd_bus_process(p->bus, NULL)) > 0) {
	}
	if (r < 0) {
		wlr_log(WLR_ERROR, "D-Bus: %s; portal and notifications stop", strerror(-r));
		wl_event_source_fd_update(p->fd_source, 0);
		wl_event_source_timer_update(p->timer, 0);
		return;
	}
	int ev = sd_bus_get_events(p->bus);
	uint32_t mask = 0;
	if (ev > 0 && (ev & POLLIN)) {
		mask |= WL_EVENT_READABLE;
	}
	if (ev > 0 && (ev & POLLOUT)) {
		mask |= WL_EVENT_WRITABLE;
	}
	wl_event_source_fd_update(p->fd_source, mask);
	uint64_t usec = UINT64_MAX;
	int ms = 0;
	if (sd_bus_get_timeout(p->bus, &usec) >= 0 && usec != UINT64_MAX) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		uint64_t n = (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
		ms = usec > n ? (int)((usec - n + 999) / 1000) : 1;
	}
	wl_event_source_timer_update(p->timer, ms);
}

/* --- Access --- */

/* One line each (TRU-6: verbatim, but never a second line). */
static void flatten(char *s) {
	for (; *s; s++) {
		if (*s == '\n' || *s == '\r' || *s == '\t') {
			*s = ' ';
		}
	}
}

static void copy(char *dst, size_t len, const char *src) {
	snprintf(dst, len, "%s", src ? src : "");
	flatten(dst);
}

static void access_free(struct access_req *req) {
	wl_list_remove(&req->link);
	sd_bus_slot_unref(req->request_slot);
	sd_bus_message_unref(req->call);
	free(req);
}

static void access_reply(struct access_req *req, uint32_t response, int choice, bool checked) {
	sd_bus_message *m = NULL;
	if (sd_bus_message_new_method_return(req->call, &m) < 0) {
		return;
	}
	sd_bus_message_append(m, "u", response);
	sd_bus_message_open_container(m, 'a', "{sv}");
	if (response == RESPONSE_GRANTED && (req->rows_id[0] || req->check_id[0])) {
		sd_bus_message_open_container(m, 'e', "sv");
		sd_bus_message_append(m, "s", "choices");
		sd_bus_message_open_container(m, 'v', "a(ss)");
		sd_bus_message_open_container(m, 'a', "(ss)");
		if (req->rows_id[0] && choice >= 0 && choice < PROMPT_MAX_CHOICES) {
			sd_bus_message_append(m, "(ss)", req->rows_id, req->row_ids[choice]);
		}
		if (req->check_id[0]) {
			sd_bus_message_append(m, "(ss)", req->check_id, checked ? "true" : "false");
		}
		sd_bus_message_close_container(m);
		sd_bus_message_close_container(m);
		sd_bus_message_close_container(m);
	}
	sd_bus_message_close_container(m);
	sd_bus_send(NULL, m, NULL);
	sd_bus_message_unref(m);
	schedule(req->portal);
}

static void access_done(void *data, bool granted, int choice, bool checked) {
	struct access_req *req = data;
	req->prompt = NULL;
	access_reply(req, granted ? RESPONSE_GRANTED : RESPONSE_CANCELLED, choice, checked);
	access_free(req);
}

/* Request.Close: the asker gave up; the prompt goes away. */
static int method_close(sd_bus_message *m, void *data, sd_bus_error *err) {
	struct access_req *req = data;
	int r = sd_bus_reply_method_return(m, "");
	if (req->prompt) {
		/* access_done runs from here and frees req (sd-bus holds a
		 * reference to this slot until the callback returns). */
		prompt_close(req->portal->server, req->prompt);
	}
	return r;
}

static const sd_bus_vtable request_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Close", "", "", method_close, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

/* a(ssa(ss)s): id, label, options (none: a boolean), initial. */
static int read_choices(sd_bus_message *m, struct access_req *req, struct prompt_spec *spec) {
	int r = sd_bus_message_enter_container(m, 'a', "(ssa(ss)s)");
	if (r < 0) {
		return r;
	}
	while ((r = sd_bus_message_enter_container(m, 'r', "ssa(ss)s")) > 0) {
		const char *id, *label, *initial;
		if ((r = sd_bus_message_read(m, "ss", &id, &label)) < 0 ||
				(r = sd_bus_message_enter_container(m, 'a', "(ss)")) < 0) {
			return r;
		}
		bool take_rows = !req->rows_id[0];
		int n = 0;
		while ((r = sd_bus_message_enter_container(m, 'r', "ss")) > 0) {
			const char *oid, *olabel;
			if ((r = sd_bus_message_read(m, "ss", &oid, &olabel)) < 0) {
				return r;
			}
			if (take_rows && n < PROMPT_MAX_CHOICES) {
				copy(req->row_ids[n], sizeof(req->row_ids[n]), oid);
				copy(spec->choices[n], sizeof(spec->choices[n]), olabel);
				n++;
			}
			sd_bus_message_exit_container(m);
		}
		if (r < 0 || (r = sd_bus_message_exit_container(m)) < 0 ||
				(r = sd_bus_message_read(m, "s", &initial)) < 0) {
			return r;
		}
		if (n > 0 && take_rows) {
			copy(req->rows_id, sizeof(req->rows_id), id);
			copy(spec->legend, sizeof(spec->legend), label);
			spec->nchoices = n;
			for (int i = 0; i < n; i++) {
				if (strcmp(req->row_ids[i], initial) == 0) {
					spec->selected = i;
				}
			}
		} else if (n == 0 && !req->check_id[0]) {
			copy(req->check_id, sizeof(req->check_id), id);
			copy(spec->checkbox, sizeof(spec->checkbox), label);
			spec->checked = strcmp(initial, "true") == 0;
		}
		sd_bus_message_exit_container(m);
	}
	if (r < 0) {
		return r;
	}
	return sd_bus_message_exit_container(m);
}

/*
 * AccessDialog(o handle, s app_id, s parent_window, s title, s subtitle,
 * s body, a{sv} options) -> (u response, a{sv} results). Asynchronous: the
 * reply goes out when the prompt is answered.
 */
static int method_access_dialog(sd_bus_message *m, void *data, sd_bus_error *err) {
	struct portal *p = data;
	const char *handle, *app_id, *parent, *title, *subtitle, *body;
	int r = sd_bus_message_read(m, "osssss", &handle, &app_id, &parent, &title,
		&subtitle, &body);
	if (r < 0) {
		return r;
	}
	struct access_req *req = calloc(1, sizeof(*req));
	if (!req) {
		return -ENOMEM;
	}
	wl_list_init(&req->link);
	req->portal = p;
	struct prompt_spec spec = {0};
	/* twm's words in the heading; the asker's verbatim below (TRU-6). */
	snprintf(spec.purpose, sizeof(spec.purpose), "access");
	snprintf(spec.heading, sizeof(spec.heading), "An app asks for permission");
	if (app_id[0]) {
		char line[sizeof(spec.client)];
		snprintf(line, sizeof(line), "%s \xc2\xb7 %s", app_id, title);
		copy(spec.client, sizeof(spec.client), line);
	} else {
		copy(spec.client, sizeof(spec.client), title);
	}
	copy(spec.detail, sizeof(spec.detail), subtitle[0] ? subtitle : body);
	snprintf(spec.deny, sizeof(spec.deny), "Deny");
	snprintf(spec.grant, sizeof(spec.grant), "Allow");

	if ((r = sd_bus_message_enter_container(m, 'a', "{sv}")) < 0) {
		goto fail;
	}
	while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
		const char *key, *s;
		if ((r = sd_bus_message_read(m, "s", &key)) < 0) {
			goto fail;
		}
		if (strcmp(key, "deny_label") == 0 || strcmp(key, "grant_label") == 0) {
			if ((r = sd_bus_message_read(m, "v", "s", &s)) < 0) {
				goto fail;
			}
			if (s[0]) {
				bool deny = key[0] == 'd';
				copy(deny ? spec.deny : spec.grant, sizeof(spec.deny), s);
			}
		} else if (strcmp(key, "choices") == 0) {
			if ((r = sd_bus_message_enter_container(m, 'v', "a(ssa(ss)s)")) < 0 ||
					(r = read_choices(m, req, &spec)) < 0 ||
					(r = sd_bus_message_exit_container(m)) < 0) {
				goto fail;
			}
		} else if ((r = sd_bus_message_skip(m, "v")) < 0) {
			goto fail;
		}
		if ((r = sd_bus_message_exit_container(m)) < 0) {
			goto fail;
		}
	}
	if (r < 0 || (r = sd_bus_message_exit_container(m)) < 0) {
		goto fail;
	}

	r = sd_bus_add_object_vtable(p->bus, &req->request_slot, handle, REQUEST_IFACE,
		request_vtable, req);
	if (r < 0) {
		goto fail;
	}
	spec.done = access_done;
	spec.data = req;
	req->call = sd_bus_message_ref(m);
	wl_list_insert(&p->requests, &req->link);
	req->prompt = prompt_open(p->server, &spec);
	if (!req->prompt) {
		r = -ENOMEM;
		req->call = sd_bus_message_unref(req->call);
		goto fail;
	}
	return 1; /* answered later, by access_done */
fail:
	access_free(req);
	return r;
}

static const sd_bus_vtable access_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("AccessDialog", "osssssa{sv}", "ua{sv}", method_access_dialog,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

/* --- Notifications: one root line each (ROOT-7: never a toast) --- */

static int method_notify(sd_bus_message *m, void *data, sd_bus_error *err) {
	struct portal *p = data;
	const char *app, *icon, *summary, *body;
	uint32_t replaces;
	int r = sd_bus_message_read(m, "susss", &app, &replaces, &icon, &summary, &body);
	if (r < 0 || (r = sd_bus_message_skip(m, "as")) < 0) {
		return r;
	}
	/* hints: only urgency matters; critical ones are written in accent. */
	uint8_t urgency = 1;
	if ((r = sd_bus_message_enter_container(m, 'a', "{sv}")) < 0) {
		return r;
	}
	while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
		const char *key;
		if ((r = sd_bus_message_read(m, "s", &key)) < 0) {
			return r;
		}
		if (strcmp(key, "urgency") == 0 && sd_bus_message_read(m, "v", "y", &urgency) >= 0) {
			/* read */
		} else if ((r = sd_bus_message_skip(m, "v")) < 0) {
			return r;
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);

	char source[16], text[400];
	copy(source, sizeof(source), app[0] ? app : "notify");
	if (body[0]) {
		snprintf(text, sizeof(text), "%s \xe2\x80\x94 %s", summary, body);
	} else {
		snprintf(text, sizeof(text), "%s", summary);
	}
	flatten(text);
	rootline_post(p->server, urgency >= 2 ? ROOTLINE_ERROR : ROOTLINE_INFO, source, "%s", text);
	uint32_t id = replaces ? replaces : ++p->notify_id;
	return sd_bus_reply_method_return(m, "u", id);
}

static int method_close_notification(sd_bus_message *m, void *data, sd_bus_error *err) {
	uint32_t id;
	int r = sd_bus_message_read(m, "u", &id);
	if (r < 0) {
		return r;
	}
	/* Reason 3: closed by a call to CloseNotification. */
	sd_bus_emit_signal(sd_bus_message_get_bus(m), NOTIFY_PATH, NOTIFY_NAME,
		"NotificationClosed", "uu", id, (uint32_t)3);
	return sd_bus_reply_method_return(m, "");
}

static int method_get_capabilities(sd_bus_message *m, void *data, sd_bus_error *err) {
	return sd_bus_reply_method_return(m, "as", 1, "body");
}

static int method_get_server_information(sd_bus_message *m, void *data, sd_bus_error *err) {
	return sd_bus_reply_method_return(m, "ssss", "twm", "twm", TWM_VERSION, "1.2");
}

static const sd_bus_vtable notify_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", method_notify, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("CloseNotification", "u", "", method_close_notification,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("GetCapabilities", "", "as", method_get_capabilities,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("GetServerInformation", "", "ssss", method_get_server_information,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
	SD_BUS_VTABLE_END,
};

static void notifications_update(struct portal *p) {
	bool want = p->server->config && p->server->config->notifications;
	if (want && !p->notifications) {
		/* Only if nobody else serves them: no replacing, no queueing. */
		int r = sd_bus_request_name(p->bus, NOTIFY_NAME, 0);
		if (r < 0) {
			wlr_log(WLR_INFO, "D-Bus: %s is taken; notifications stay with its owner",
				NOTIFY_NAME);
			return;
		}
		if (sd_bus_add_object_vtable(p->bus, &p->notify_slot, NOTIFY_PATH, NOTIFY_NAME,
				notify_vtable, p) < 0) {
			sd_bus_release_name(p->bus, NOTIFY_NAME);
			return;
		}
		p->notifications = true;
	} else if (!want && p->notifications) {
		p->notify_slot = sd_bus_slot_unref(p->notify_slot);
		sd_bus_release_name(p->bus, NOTIFY_NAME);
		p->notifications = false;
	}
}

void portal_config_changed(struct twm_server *server) {
	struct portal *p = portal_of(server);
	if (p) {
		notifications_update(p);
		schedule(p);
	}
}

/* --- the activation environment --- */

static void update_environment(struct portal *p) {
	static const char *const names[] = {
		"WAYLAND_DISPLAY", "DISPLAY", "XDG_CURRENT_DESKTOP", "TWM_SOCK",
	};
	sd_bus_message *m = NULL;
	if (sd_bus_message_new_method_call(p->bus, &m, "org.freedesktop.DBus",
			"/org/freedesktop/DBus", "org.freedesktop.DBus",
			"UpdateActivationEnvironment") >= 0) {
		sd_bus_message_open_container(m, 'a', "{ss}");
		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			const char *v = getenv(names[i]);
			if (v) {
				sd_bus_message_append(m, "{ss}", names[i], v);
			}
		}
		sd_bus_message_close_container(m);
		sd_bus_call_async(p->bus, NULL, m, NULL, NULL, 0);
		sd_bus_message_unref(m);
	}
	/* systemd --user, where there is one; an error reply is fine. */
	m = NULL;
	if (sd_bus_message_new_method_call(p->bus, &m, "org.freedesktop.systemd1",
			"/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
			"SetEnvironment") >= 0) {
		sd_bus_message_open_container(m, 'a', "s");
		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			const char *v = getenv(names[i]);
			if (v) {
				char kv[512];
				snprintf(kv, sizeof(kv), "%s=%s", names[i], v);
				sd_bus_message_append(m, "s", kv);
			}
		}
		sd_bus_message_close_container(m);
		sd_bus_message_set_auto_start(m, false);
		sd_bus_call_async(p->bus, NULL, m, NULL, NULL, 0);
		sd_bus_message_unref(m);
	}
}

/* --- setup --- */

void portal_init(struct twm_server *server) {
	/* xdg-desktop-portal picks backends by this name (twm-portals.conf). */
	setenv("XDG_CURRENT_DESKTOP", "twm", true);
	sd_bus *bus = NULL;
	int r = sd_bus_open_user(&bus);
	if (r < 0) {
		wlr_log(WLR_INFO, "no D-Bus session bus (%s): no portal, no notifications",
			strerror(-r));
		return;
	}
	struct portal *p = calloc(1, sizeof(*p));
	if (!p) {
		sd_bus_unref(bus);
		return;
	}
	p->server = server;
	p->bus = bus;
	wl_list_init(&p->requests);
	server->portal = p;

	r = sd_bus_add_object_vtable(bus, &p->access_slot, PORTAL_PATH, ACCESS_IFACE,
		access_vtable, p);
	if (r >= 0) {
		r = sd_bus_request_name(bus, PORTAL_NAME, 0);
	}
	if (r < 0) {
		wlr_log(WLR_ERROR, "D-Bus: can't serve %s: %s", PORTAL_NAME, strerror(-r));
	}
	notifications_update(p);
	if (server->opts.session) {
		update_environment(p);
	}
	p->fd_source = wl_event_loop_add_fd(server->loop, sd_bus_get_fd(bus),
		WL_EVENT_READABLE, handle_fd, p);
	p->timer = wl_event_loop_add_timer(server->loop, handle_timer, p);
	pump(p);
}

void portal_finish(struct twm_server *server) {
	struct portal *p = portal_of(server);
	if (!p) {
		return;
	}
	/* Prompts are gone by now (prompt_finish answered them); flush. */
	struct access_req *req, *tmp;
	wl_list_for_each_safe(req, tmp, &p->requests, link) {
		access_reply(req, RESPONSE_OTHER, 0, false);
		access_free(req);
	}
	if (p->idle) {
		wl_event_source_remove(p->idle);
	}
	if (p->fd_source) {
		wl_event_source_remove(p->fd_source);
	}
	if (p->timer) {
		wl_event_source_remove(p->timer);
	}
	sd_bus_slot_unref(p->notify_slot);
	sd_bus_slot_unref(p->access_slot);
	sd_bus_flush_close_unref(p->bus);
	free(p);
	server->portal = NULL;
}
