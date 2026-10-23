#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "rc.h"
#include "server.h"
#include "watch.h"

#if defined(__linux__)
#include <sys/inotify.h>
#else
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#define DEBOUNCE_MS 150

struct twm_watch {
	struct twm_server *server;
	char *dir, *name, *path;
	int fd;
	struct wl_event_source *source;
	struct wl_event_source *debounce;
#if !defined(__linux__)
	int dir_fd, file_fd;
#endif
};

static int handle_debounce(void *data) {
	struct twm_watch *w = data;
	rc_reload(w->server, false, NULL, 0);
	return 0;
}

static void changed(struct twm_watch *w) {
	wl_event_source_timer_update(w->debounce, DEBOUNCE_MS);
}

#if defined(__linux__)

static int handle_readable(int fd, uint32_t mask, void *data) {
	struct twm_watch *w = data;
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		for (char *p = buf; p < buf + n;) {
			struct inotify_event *ev = (struct inotify_event *)p;
			if (ev->len && strcmp(ev->name, w->name) == 0) {
				changed(w);
			}
			p += sizeof(*ev) + ev->len;
		}
	}
	return 0;
}

static bool start(struct twm_watch *w) {
	w->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (w->fd < 0) {
		return false;
	}
	if (inotify_add_watch(w->fd, w->dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE |
			IN_DELETE | IN_MOVED_FROM | IN_ATTRIB) < 0) {
		close(w->fd);
		return false;
	}
	return true;
}

#else /* kqueue */

/* A directory vnode reports entries added, removed or renamed; writes into
 * an existing file are only seen on the file itself, so watch both and
 * re-open the file whenever the directory changes. */
static void watch_file(struct twm_watch *w) {
	if (w->file_fd >= 0) {
		close(w->file_fd);
	}
	w->file_fd = open(w->path, O_RDONLY | O_CLOEXEC);
	if (w->file_fd >= 0) {
		struct kevent ev;
		EV_SET(&ev, w->file_fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
			NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, NULL);
		kevent(w->fd, &ev, 1, NULL, 0, NULL);
	}
}

static int handle_readable(int fd, uint32_t mask, void *data) {
	struct twm_watch *w = data;
	struct kevent evs[8];
	struct timespec zero = {0};
	int n;
	while ((n = kevent(fd, NULL, 0, evs, 8, &zero)) > 0) {
		for (int i = 0; i < n; i++) {
			if ((int)evs[i].ident == w->dir_fd ||
					(evs[i].fflags & (NOTE_DELETE | NOTE_RENAME))) {
				watch_file(w);
			}
		}
		changed(w);
	}
	return 0;
}

static bool start(struct twm_watch *w) {
	w->file_fd = -1;
	w->fd = kqueue();
	if (w->fd < 0) {
		return false;
	}
	w->dir_fd = open(w->dir, O_RDONLY | O_CLOEXEC);
	if (w->dir_fd < 0) {
		close(w->fd);
		return false;
	}
	struct kevent ev;
	EV_SET(&ev, w->dir_fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_EXTEND, 0, NULL);
	if (kevent(w->fd, &ev, 1, NULL, 0, NULL) < 0) {
		close(w->dir_fd);
		close(w->fd);
		return false;
	}
	watch_file(w);
	return true;
}

#endif

void watch_init(struct twm_server *server, const char *path) {
	struct twm_watch *w = calloc(1, sizeof(*w));
	if (!w) {
		return;
	}
	w->server = server;
	w->path = strdup(path);
	const char *slash = strrchr(path, '/');
	w->dir = slash ? strndup(path, (size_t)(slash - path) + (slash == path)) : strdup(".");
	w->name = strdup(slash ? slash + 1 : path);
	if (!start(w)) {
		wlr_log(WLR_ERROR, "can't watch %s (%s); use M-S-r or twm --reload",
			w->dir, strerror(errno));
		free(w->dir);
		free(w->name);
		free(w->path);
		free(w);
		return;
	}
	w->source = wl_event_loop_add_fd(server->loop, w->fd, WL_EVENT_READABLE,
		handle_readable, w);
	w->debounce = wl_event_loop_add_timer(server->loop, handle_debounce, w);
	server->watch = w;
}

void watch_finish(struct twm_server *server) {
	struct twm_watch *w = server->watch;
	if (!w) {
		return;
	}
	wl_event_source_remove(w->source);
	wl_event_source_remove(w->debounce);
	close(w->fd);
#if !defined(__linux__)
	close(w->dir_fd);
	if (w->file_fd >= 0) {
		close(w->file_fd);
	}
#endif
	free(w->dir);
	free(w->name);
	free(w->path);
	free(w);
	server->watch = NULL;
}
