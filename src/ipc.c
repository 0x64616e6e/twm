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
#include "prompt.h"
#include "rc.h"
#include "server.h"

#define REQUEST_MAX 8192
#define REPLY_MAX 8192
#define CHOICE_MAX 512

struct ipc_client {
	struct twm_server *server;
	int fd;
	struct wl_event_source *source;
	char buf[REQUEST_MAX];
	size_t len;
	bool answered; /* the request is in; only a hangup matters now */
	/* "choose": the prompt it waits on and the lines, verbatim */
	struct twm_prompt *prompt;
	bool closing;
	char lines[PROMPT_MAX_CHOICES][CHOICE_MAX];
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
	if (c->prompt) {
		/* The chooser went away: withdraw its prompt (counts as denied). */
		c->closing = true;
		prompt_close(c->server, c->prompt);
	}
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

static void reply(struct ipc_client *c, const char *text) {
	/* The socket is non-blocking; the reply is small, so flip it back for
	 * the one write. */
	int flags = fcntl(c->fd, F_GETFL);
	fcntl(c->fd, F_SETFL, flags & ~O_NONBLOCK);
	write_all(c->fd, text, strlen(text));
}

static void choose_done(void *data, bool granted, int choice, bool checked) {
	struct ipc_client *c = data;
	c->prompt = NULL;
	if (c->closing) {
		return; /* client_close is already freeing it */
	}
	char out[CHOICE_MAX + 16];
	if (granted && choice >= 0 && choice < PROMPT_MAX_CHOICES && c->lines[choice][0]) {
		snprintf(out, sizeof(out), "chose %s\n", c->lines[choice]);
	} else {
		snprintf(out, sizeof(out), "denied\n");
	}
	reply(c, out);
	client_close(c);
}

/*
 * "choose\t<who is asking>\n<line>\n<line>\n…\n\n" (`twm --choose`): show
 * the lines in a trusted prompt (TRU-1); answer "chose <line>" or "denied".
 * The heading is twm's own; the asker's words go in the ink-2 line (TRU-6).
 */
static bool start_choose(struct ipc_client *c, char *req) {
	char *rest = strchr(req, '\n');
	*rest++ = '\0';
	const char *who = req + strlen("choose");
	who += *who == '\t';
	struct prompt_spec spec = {0};
	snprintf(spec.purpose, sizeof(spec.purpose), "share screen");
	snprintf(spec.heading, sizeof(spec.heading), "Choose what to share");
	snprintf(spec.client, sizeof(spec.client), "%s",
		*who ? who : "An app asked to share your screen through the portal.");
	snprintf(spec.legend, sizeof(spec.legend), "SHARE");
	snprintf(spec.deny, sizeof(spec.deny), "Deny");
	snprintf(spec.grant, sizeof(spec.grant), "Share");
	for (char *line = rest, *nl; *line && (nl = strchr(line, '\n')); line = nl + 1) {
		*nl = '\0';
		if (!*line || spec.nchoices == PROMPT_MAX_CHOICES) {
			break;
		}
		snprintf(c->lines[spec.nchoices], CHOICE_MAX, "%s", line);
		snprintf(spec.choices[spec.nchoices], sizeof(spec.choices[0]), "%s", line);
		spec.nchoices++;
	}
	if (spec.nchoices == 0) {
		reply(c, "denied: nothing to choose from\n");
		return false;
	}
	spec.done = choose_done;
	spec.data = c;
	c->prompt = prompt_open(c->server, &spec);
	if (!c->prompt) {
		reply(c, "denied: out of memory\n");
		return false;
	}
	c->answered = true;
	return true;
}

/* Returns true when the client stays open (waiting on a prompt). */
static bool handle_request(struct ipc_client *c, char *req) {
	char out[REPLY_MAX];
	if (strcmp(req, "reload") == 0) {
		rc_reload(c->server, true, out, sizeof(out));
	} else if (strncmp(req, "choose", 6) == 0 && (req[6] == '\t' || req[6] == '\n')) {
		return start_choose(c, req);
	} else if (strncmp(req, "touch ", 6) == 0) {
		touch_test_request(c->server, req, out, sizeof(out)); /* --test only */
	} else {
		snprintf(out, sizeof(out), "unknown request \"%s\" (try: reload, choose)\n", req);
	}
	reply(c, out);
	return false;
}

/* A request is one line; "choose" runs to an empty line. */
static bool request_complete(const char *buf) {
	if (strncmp(buf, "choose", 6) == 0) {
		return strstr(buf, "\n\n") != NULL;
	}
	return strchr(buf, '\n') != NULL;
}

static int handle_client(int fd, uint32_t mask, void *data) {
	struct ipc_client *c = data;
	/* A client may write its request and hang up at once: read it first. */
	if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) && !(mask & WL_EVENT_READABLE)) {
		client_close(c);
		return 0;
	}
	if (c->answered) {
		/* Waiting on a prompt: anything readable is either more bytes
		 * (ignored) or the end of the connection. */
		char junk[64];
		ssize_t n = read(fd, junk, sizeof(junk));
		if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
			client_close(c);
		}
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
	bool full = c->len >= sizeof(c->buf) - 1;
	if (!request_complete(c->buf) && !full) {
		return 0;
	}
	if (strncmp(c->buf, "choose", 6) != 0) {
		char *nl = strchr(c->buf, '\n');
		if (nl) {
			*nl = '\0';
		}
	} else if (full && !request_complete(c->buf)) {
		reply(c, "denied: request too long\n");
		client_close(c);
		return 0;
	}
	if (!handle_request(c, c->buf)) {
		client_close(c);
	}
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

static int ipc_connect(void) {
	char path[108];
	const char *sock = getenv("TWM_SOCK");
	if (sock && *sock) {
		snprintf(path, sizeof(path), "%s", sock);
	} else if (!ipc_socket_path(getenv("WAYLAND_DISPLAY"), path, sizeof(path))) {
		fprintf(stderr, "twm: can't find a running twm (set WAYLAND_DISPLAY or TWM_SOCK)\n");
		return -1;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "twm: can't reach %s: %s\n", path, strerror(errno));
		if (fd >= 0) {
			close(fd);
		}
		return -1;
	}
	return fd;
}

static void read_reply(int fd, char *buf, size_t len) {
	size_t n = 0;
	ssize_t r;
	while (n < len - 1 && (r = read(fd, buf + n, len - 1 - n)) != 0) {
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		n += (size_t)r;
	}
	buf[n] = '\0';
}

int ipc_request(const char *request) {
	int fd = ipc_connect();
	if (fd < 0) {
		return 2;
	}
	char line[REQUEST_MAX];
	snprintf(line, sizeof(line), "%s\n", request);
	write_all(fd, line, strlen(line));
	char reply[REPLY_MAX];
	read_reply(fd, reply, sizeof(reply));
	close(fd);
	fputs(reply, stdout);
	return strstr(reply, "config ok") != NULL ? 0 : 1;
}

/* Keep a line to one line, without tabs (they separate fields). */
static void flatten(char *s) {
	for (; *s; s++) {
		if (*s == '\t' || *s == '\n' || *s == '\r') {
			*s = ' ';
		}
	}
}

int ipc_choose(const char *who) {
	char req[REQUEST_MAX];
	char w[256];
	snprintf(w, sizeof(w), "%s", who ? who : "");
	flatten(w);
	size_t n = (size_t)snprintf(req, sizeof(req), "choose\t%s\n", w);
	char line[CHOICE_MAX];
	int count = 0;
	while (count < PROMPT_MAX_CHOICES && fgets(line, sizeof(line), stdin)) {
		line[strcspn(line, "\r\n")] = '\0';
		flatten(line);
		if (!*line || n + strlen(line) + 3 >= sizeof(req)) {
			continue;
		}
		n += (size_t)snprintf(req + n, sizeof(req) - n, "%s\n", line);
		count++;
	}
	if (count == 0) {
		fprintf(stderr, "twm: --choose got nothing to choose from on stdin\n");
		return 1;
	}
	snprintf(req + n, sizeof(req) - n, "\n");
	int fd = ipc_connect();
	if (fd < 0) {
		return 2;
	}
	write_all(fd, req, strlen(req));
	char reply[REPLY_MAX];
	read_reply(fd, reply, sizeof(reply));
	close(fd);
	if (strncmp(reply, "chose ", 6) == 0) {
		fputs(reply + 6, stdout);
		return 0;
	}
	fputs(reply, stderr);
	return 1;
}
