#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "greetd.h"

/*
 * greetd's IPC: each message is a native-endian u32 length and that much
 * JSON. The greeter asks create_session; greetd answers auth_message (to
 * answer with post_auth_message_response), success or error; then
 * start_session.
 */

enum stage { ST_AUTH, ST_LAUNCH, ST_CANCEL };

struct greetd {
	int fd;
	struct wl_event_source *source;
	auth_fn fn;
	void *data;
	enum stage stage;
	bool pending_info; /* an info or error message waits for an empty answer */
	uint8_t buf[16384];
	size_t len;
	bool done;
};

static void wipe(void *p, size_t n) {
	volatile unsigned char *v = p;
	while (n--) {
		*v++ = 0;
	}
}

static bool send_msg(struct greetd *g, const char *json) {
	uint32_t n = (uint32_t)strlen(json);
	int flags = fcntl(g->fd, F_GETFL);
	fcntl(g->fd, F_SETFL, flags & ~O_NONBLOCK);
	bool ok = write(g->fd, &n, sizeof(n)) == sizeof(n) && write(g->fd, json, n) == (ssize_t)n;
	fcntl(g->fd, F_SETFL, flags);
	return ok;
}

static void stop(struct greetd *g) {
	if (g->source) {
		wl_event_source_remove(g->source);
		g->source = NULL;
	}
	if (g->fd >= 0) {
		close(g->fd);
		g->fd = -1;
	}
	g->done = true;
}

/* Returns false once `g` may be gone (the conversation ended). */
static bool handle(struct greetd *g, const char *json) {
	char type[64] = "";
	json_string(json, "type", type, sizeof(type));
	if (strcmp(type, "auth_message") == 0) {
		char kind[32] = "", msg[512] = "";
		json_string(json, "auth_message_type", kind, sizeof(kind));
		json_string(json, "auth_message", msg, sizeof(msg));
		if (strcmp(kind, "secret") == 0 || strcmp(kind, "visible") == 0) {
			g->fn(g->data, AUTH_PROMPT, msg, strcmp(kind, "visible") == 0);
		} else {
			/* info and error want an empty answer to go on */
			g->fn(g->data, strcmp(kind, "error") == 0 ? AUTH_ERROR : AUTH_INFO, msg, false);
			if (!g->done) {
				send_msg(g, "{\"type\":\"post_auth_message_response\"}");
			}
		}
		return true;
	}
	if (strcmp(type, "success") == 0) {
		/* authenticated (ST_AUTH) or the session is starting (ST_LAUNCH):
		 * both are the good news; the caller knows which it asked */
		if (g->stage == ST_LAUNCH) {
			stop(g);
		}
		g->fn(g->data, AUTH_OK, "", false);
		return false;
	}
	if (strcmp(type, "error") == 0) {
		char desc[512] = "authentication failed";
		json_string(json, "description", desc, sizeof(desc));
		/* the conversation is over: tell greetd, then say so */
		send_msg(g, "{\"type\":\"cancel_session\"}");
		stop(g);
		g->fn(g->data, AUTH_FAIL, desc, false);
		return false;
	}
	return true;
}

static int readable(int fd, uint32_t mask, void *data) {
	struct greetd *g = data;
	if (mask & WL_EVENT_READABLE) {
		ssize_t n = read(fd, g->buf + g->len, sizeof(g->buf) - 1 - g->len);
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			return 0;
		}
		if (n > 0) {
			g->len += (size_t)n;
			while (g->len >= 4) {
				uint32_t m;
				memcpy(&m, g->buf, 4);
				if (m > sizeof(g->buf) - 5) {
					break; /* too big: the hangup path ends it */
				}
				if (g->len < 4 + m) {
					return 0;
				}
				char json[sizeof(g->buf)];
				memcpy(json, g->buf + 4, m);
				json[m] = '\0';
				memmove(g->buf, g->buf + 4 + m, g->len - 4 - m);
				g->len -= 4 + m;
				if (!handle(g, json)) {
					return 0; /* `g` may be freed by now */
				}
			}
			return 0;
		}
	}
	if (!g->done) {
		stop(g);
		g->fn(g->data, AUTH_FAIL, "greetd went away", false);
	}
	return 0;
}

struct greetd *greetd_start(struct wl_event_loop *loop, const char *user, auth_fn fn, void *data) {
	const char *path = getenv("GREETD_SOCK");
	if (!path || !*path) {
		return NULL;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		wlr_log(WLR_ERROR, "greetd: can't reach %s: %s", path, strerror(errno));
		if (fd >= 0) {
			close(fd);
		}
		return NULL;
	}
	struct greetd *g = calloc(1, sizeof(*g));
	if (!g) {
		close(fd);
		return NULL;
	}
	g->fd = fd;
	g->fn = fn;
	g->data = data;
	fcntl(fd, F_SETFL, O_NONBLOCK);
	g->source = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, readable, g);
	char req[512];
	const char *kv[] = {"username", user, NULL};
	greetd_request(req, sizeof(req), "create_session", kv);
	send_msg(g, req);
	return g;
}

void greetd_respond(struct greetd *g, char *response) {
	if (g && !g->done) {
		char req[2048];
		const char *kv[] = {"response", response, NULL};
		size_t n = greetd_request(req, sizeof(req), "post_auth_message_response", kv);
		send_msg(g, req);
		wipe(req, n < sizeof(req) ? n : sizeof(req));
	}
	wipe(response, strlen(response));
}

void greetd_launch(struct greetd *g, const char *const *cmd, const char *const *env) {
	if (!g || g->done) {
		return;
	}
	char req[4096];
	greetd_start_session(req, sizeof(req), cmd, env);
	g->stage = ST_LAUNCH;
	send_msg(g, req);
}

void greetd_cancel(struct greetd *g) {
	if (!g) {
		return;
	}
	if (!g->done && g->fd >= 0) {
		send_msg(g, "{\"type\":\"cancel_session\"}");
	}
	stop(g);
	free(g);
}
