#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "ipc.h"
#include "rc.h"
#include "server.h"

#define REQUEST_MAX 256
#define REPLY_MAX 8192

struct ipc_client {
	struct twm_server *server;
	int fd;
	struct wl_event_source *source;
	char buf[REQUEST_MAX];
	size_t len;
};

bool ipc_socket_path(const char *display, char *buf, size_t len) {
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (!dir || !*dir || !display || !*display) {
		return false;
	}
	int n = snprintf(buf, len, "%s/twm.%s.sock", dir, display);
	return n > 0 && (size_t)n < len && (size_t)n < sizeof(((struct sockaddr_un *)0)->sun_path);
}

static void client_close(struct ipc_client *c) {
	wl_event_source_remove(c->source);
	close(c->fd);
	free(c);
}

static void write_all(int fd, const char *s, size_t n) {
	while (n > 0) {
		ssize_t w = write(fd, s, n);
		if (w < 0 && errno == EINTR) {
			continue;
		}
		if (w <= 0) {
			return;
		}
		s += w;
		n -= (size_t)w;
	}
}

static void handle_request(struct ipc_client *c, const char *req) {
	char reply[REPLY_MAX];
	if (strcmp(req, "reload") == 0) {
		rc_reload(c->server, true, reply, sizeof(reply));
	} else {
		snprintf(reply, sizeof(reply), "unknown request \"%s\" (try: reload)\n", req);
	}
	/* The socket is non-blocking; the reply is small, so flip it back for
	 * the one write. */
	int flags = fcntl(c->fd, F_GETFL);
	fcntl(c->fd, F_SETFL, flags & ~O_NONBLOCK);
	write_all(c->fd, reply, strlen(reply));
}

static int handle_client(int fd, uint32_t mask, void *data) {
	struct ipc_client *c = data;
	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		client_close(c);
		return 0;
	}
	ssize_t n = read(fd, c->buf + c->len, sizeof(c->buf) - 1 - c->len);
	if (n <= 0) {
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			return 0;
		}
		client_close(c);
		return 0;
	}
	c->len += (size_t)n;
	c->buf[c->len] = '\0';
	char *nl = strchr(c->buf, '\n');
	if (!nl && c->len < sizeof(c->buf) - 1) {
		return 0;
	}
	if (nl) {
		*nl = '\0';
	}
	handle_request(c, c->buf);
	client_close(c);
	return 0;
}

static int handle_listen(int fd, uint32_t mask, void *data) {
	struct twm_server *server = data;
	int cfd = accept(fd, NULL, NULL);
	if (cfd < 0) {
		return 0;
	}
	fcntl(cfd, F_SETFD, FD_CLOEXEC);
	fcntl(cfd, F_SETFL, O_NONBLOCK);
	struct ipc_client *c = calloc(1, sizeof(*c));
	if (!c) {
		close(cfd);
		return 0;
	}
	c->server = server;
	c->fd = cfd;
	c->source = wl_event_loop_add_fd(server->loop, cfd, WL_EVENT_READABLE, handle_client, c);
	return 0;
}

void ipc_init(struct twm_server *server) {
	char path[108];
	if (!ipc_socket_path(server->socket, path, sizeof(path))) {
		wlr_log(WLR_ERROR, "no IPC socket: XDG_RUNTIME_DIR is unset or too long");
		return;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fcntl(fd, F_SETFL, O_NONBLOCK);
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	unlink(path); /* a stale socket from a crashed twm on the same display */
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 4) < 0) {
		wlr_log_errno(WLR_ERROR, "IPC socket %s", path);
		close(fd);
		return;
	}
	server->ipc_fd = fd;
	server->ipc_path = strdup(path);
	server->ipc_source = wl_event_loop_add_fd(server->loop, fd, WL_EVENT_READABLE,
		handle_listen, server);
	setenv("TWM_SOCK", path, true);
}

void ipc_finish(struct twm_server *server) {
	if (!server->ipc_source) {
		return;
	}
	wl_event_source_remove(server->ipc_source);
	close(server->ipc_fd);
	unlink(server->ipc_path);
	free(server->ipc_path);
	server->ipc_source = NULL;
	server->ipc_path = NULL;
}

int ipc_request(const char *request) {
	char path[108];
	const char *sock = getenv("TWM_SOCK");
	if (sock && *sock) {
		snprintf(path, sizeof(path), "%s", sock);
	} else if (!ipc_socket_path(getenv("WAYLAND_DISPLAY"), path, sizeof(path))) {
		fprintf(stderr, "twm: can't find a running twm (set WAYLAND_DISPLAY or TWM_SOCK)\n");
		return 2;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "twm: can't reach %s: %s\n", path, strerror(errno));
		if (fd >= 0) {
			close(fd);
		}
		return 2;
	}
	char line[REQUEST_MAX];
	snprintf(line, sizeof(line), "%s\n", request);
	write_all(fd, line, strlen(line));
	char reply[REPLY_MAX];
	size_t n = 0;
	ssize_t r;
	while (n < sizeof(reply) - 1 && (r = read(fd, reply + n, sizeof(reply) - 1 - n)) > 0) {
		n += (size_t)r;
	}
	close(fd);
	reply[n] = '\0';
	fputs(reply, stdout);
	return strstr(reply, "config ok") != NULL ? 0 : 1;
}
