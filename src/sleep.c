#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "config.h"
#include "lock.h"
#include "server.h"
#include "settings.h"
#include "sleep.h"
#include SDBUS_HEADER

#define LOGIN1 "org.freedesktop.login1"
#define LOGIN1_PATH "/org/freedesktop/login1"
#define LOGIN1_MANAGER "org.freedesktop.login1.Manager"

struct sleepw {
	struct twm_server *server;
	sd_bus *bus;
	sd_bus_slot *slot;
	int fd; /* the delay inhibitor, or -1 */
	struct wl_event_source *fd_source, *timer, *release;
};

static struct sleepw *the;

static void take(struct sleepw *s) {
	if (s->fd >= 0 || !s->server->config->lock_on_sleep) {
		return;
	}
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int r = sd_bus_call_method(s->bus, LOGIN1, LOGIN1_PATH, LOGIN1_MANAGER, "Inhibit", &err,
		&reply, "ssss", "sleep", "Tom's WM", "Lock the screen first", "delay");
	int fd = -1;
	if (r >= 0) {
		r = sd_bus_message_read(reply, "h", &fd);
	}
	if (r >= 0 && fd >= 0) {
		s->fd = fcntl(fd, F_DUPFD_CLOEXEC, 3); /* the message owns its copy */
	} else {
		wlr_log(WLR_INFO, "logind: no sleep inhibitor (%s): no lock before sleep",
			err.message ? err.message : strerror(-r));
	}
	sd_bus_error_free(&err);
	sd_bus_message_unref(reply);
}

static void drop(struct sleepw *s) {
	if (s->fd >= 0) {
		close(s->fd);
		s->fd = -1;
	}
}

static int let_go(void *data) {
	drop(data); /* the lock has drawn: sleep may go on */
	return 0;
}

static int prepare_for_sleep(sd_bus_message *m, void *data, sd_bus_error *err) {
	struct sleepw *s = data;
	int going = 0;
	if (sd_bus_message_read(m, "b", &going) < 0) {
		return 0;
	}
	if (going) {
		if (s->server->config->lock_on_sleep) {
			lock_engage(s->server, "before sleep");
		}
		wl_event_source_timer_update(s->release, 300); /* a frame or two */
	} else {
		take(s); /* awake again: hold off the next sleep until locked */
		lock_activity(s->server);
	}
	return 0;
}

static void pump(struct sleepw *s) {
	while (sd_bus_process(s->bus, NULL) > 0) {
	}
	int ev = sd_bus_get_events(s->bus);
	uint32_t mask = 0;
	if (ev > 0 && (ev & POLLIN)) {
		mask |= WL_EVENT_READABLE;
	}
	if (ev > 0 && (ev & POLLOUT)) {
		mask |= WL_EVENT_WRITABLE;
	}
	wl_event_source_fd_update(s->fd_source, mask);
	uint64_t usec = UINT64_MAX;
	int ms = 0;
	if (sd_bus_get_timeout(s->bus, &usec) >= 0 && usec != UINT64_MAX) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		uint64_t n = (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
		ms = usec > n ? (int)((usec - n + 999) / 1000) : 1;
	}
	wl_event_source_timer_update(s->timer, ms);
}

static int on_fd(int fd, uint32_t mask, void *data) {
	pump(data);
	return 0;
}

static int on_timer(void *data) {
	pump(data);
	return 0;
}

void sleep_init(struct twm_server *server) {
	if (!server->opts.session) {
		return; /* nested or headless: the outer session's business */
	}
	sd_bus *bus = NULL;
	if (sd_bus_open_system(&bus) < 0) {
		return;
	}
	struct sleepw *s = calloc(1, sizeof(*s));
	if (!s) {
		sd_bus_unref(bus);
		return;
	}
	s->server = server;
	s->bus = bus;
	s->fd = -1;
	if (sd_bus_match_signal(bus, &s->slot, LOGIN1, LOGIN1_PATH, LOGIN1_MANAGER,
			"PrepareForSleep", prepare_for_sleep, s) < 0) {
		wlr_log(WLR_INFO, "logind: can't watch for sleep");
	}
	s->fd_source = wl_event_loop_add_fd(server->loop, sd_bus_get_fd(bus), WL_EVENT_READABLE, on_fd, s);
	s->timer = wl_event_loop_add_timer(server->loop, on_timer, s);
	s->release = wl_event_loop_add_timer(server->loop, let_go, s);
	the = s;
	take(s);
	pump(s);
}

void sleep_configure(struct twm_server *server) {
	if (!the) {
		return;
	}
	if (server->config->lock_on_sleep) {
		take(the);
	} else {
		drop(the);
	}
}

void sleep_finish(struct twm_server *server) {
	struct sleepw *s = the;
	if (!s) {
		return;
	}
	drop(s);
	wl_event_source_remove(s->fd_source);
	wl_event_source_remove(s->timer);
	wl_event_source_remove(s->release);
	sd_bus_slot_unref(s->slot);
	sd_bus_flush_close_unref(s->bus);
	free(s);
	the = NULL;
}
