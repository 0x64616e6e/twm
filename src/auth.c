#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "auth.h"
#include "config.h"
#if HAVE_PAM
#include <security/pam_appl.h>
#endif

struct auth {
	int fd;
	pid_t pid;
	struct wl_event_source *source;
	auth_fn fn;
	void *data;
	char buf[2048];
	size_t len;
	bool done;
};

/* Wipe a secret where the compiler can't skip it. */
static void wipe(void *p, size_t n) {
	volatile unsigned char *v = p;
	while (n--) {
		*v++ = 0;
	}
}

bool auth_available(void) {
	return HAVE_PAM;
}

const char *auth_service(void) {
	if (access("/etc/pam.d/twm", F_OK) == 0 || access("/usr/lib/pam.d/twm", F_OK) == 0 ||
			access("/usr/local/etc/pam.d/twm", F_OK) == 0) {
		return "twm";
	}
	return "login";
}

/* --- the child --- */

static int child_fd = -1;

static void child_send(const char *kind, const char *text) {
	char line[1024];
	int n = snprintf(line, sizeof(line), "%s %s\n", kind, text ? text : "");
	for (char *p = line; p < line + n - 1; p++) {
		if (*p == '\n') {
			*p = ' '; /* one line a message */
		}
	}
	if (n > 0 && write(child_fd, line, (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1)) < 0) {
		_exit(3);
	}
}

/* One answer line from the parent, malloc'd. */
static char *child_read(void) {
	char *buf = calloc(1, 1024);
	size_t n = 0;
	while (buf && n < 1023) {
		ssize_t r = read(child_fd, buf + n, 1);
		if (r <= 0) {
			free(buf);
			_exit(4); /* the lock went away */
		}
		if (buf[n] == '\n') {
			buf[n] = '\0';
			return buf;
		}
		n++;
	}
	return buf;
}

#if HAVE_PAM
static int conv(int num, const struct pam_message **msg, struct pam_response **resp, void *data) {
	struct pam_response *r = calloc((size_t)num, sizeof(*r));
	if (!r) {
		return PAM_BUF_ERR;
	}
	for (int i = 0; i < num; i++) {
		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
		case PAM_PROMPT_ECHO_ON:
			child_send(msg[i]->msg_style == PAM_PROMPT_ECHO_ON ? "P1" : "P0", msg[i]->msg);
			r[i].resp = child_read();
			break;
		case PAM_ERROR_MSG:
			child_send("E", msg[i]->msg);
			break;
		case PAM_TEXT_INFO:
			child_send("I", msg[i]->msg);
			break;
		}
	}
	*resp = r;
	return PAM_SUCCESS;
}
#endif

static void child_main(const char *service, const char *user, const char *test_password) {
	if (test_password) {
		/* GRT-9: --test's stand-in for PAM */
		child_send("P0", "Password: ");
		char *got = child_read();
		bool ok = strcmp(got, test_password) == 0;
		wipe(got, strlen(got));
		child_send(ok ? "OK" : "NO", ok ? "" : "authentication failed");
		_exit(0);
	}
#if HAVE_PAM
	struct pam_conv c = {conv, NULL};
	pam_handle_t *h = NULL;
	int rc = pam_start(service, user, &c, &h);
	if (rc == PAM_SUCCESS) {
		rc = pam_authenticate(h, 0);
	}
	if (rc == PAM_SUCCESS) {
		rc = pam_acct_mgmt(h, 0);
	}
	if (rc == PAM_SUCCESS) {
		pam_setcred(h, PAM_REFRESH_CRED); /* Kerberos tickets and the like */
		child_send("OK", "");
	} else {
		child_send("NO", h ? pam_strerror(h, rc) : "PAM failed to start");
	}
	if (h) {
		pam_end(h, rc);
	}
#else
	child_send("NO", "twm was built without PAM");
#endif
	_exit(0);
}

/* --- the parent --- */

static void finish(struct auth *a) {
	if (a->source) {
		wl_event_source_remove(a->source);
		a->source = NULL;
	}
	if (a->fd >= 0) {
		close(a->fd);
		a->fd = -1;
	}
	a->done = true;
}

static void dispatch(struct auth *a, char *line) {
	if (strncmp(line, "P0 ", 3) == 0 || strncmp(line, "P1 ", 3) == 0) {
		a->fn(a->data, AUTH_PROMPT, line + 3, line[1] == '1');
	} else if (strncmp(line, "I ", 2) == 0) {
		a->fn(a->data, AUTH_INFO, line + 2, false);
	} else if (strncmp(line, "E ", 2) == 0) {
		a->fn(a->data, AUTH_ERROR, line + 2, false);
	} else if (strncmp(line, "OK", 2) == 0) {
		finish(a);
		a->fn(a->data, AUTH_OK, "", false);
	} else if (strncmp(line, "NO", 2) == 0) {
		finish(a);
		a->fn(a->data, AUTH_FAIL, line[2] == ' ' ? line + 3 : "authentication failed", false);
	}
}

static int readable(int fd, uint32_t mask, void *data) {
	struct auth *a = data;
	if (mask & WL_EVENT_READABLE) {
		ssize_t n = read(fd, a->buf + a->len, sizeof(a->buf) - 1 - a->len);
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			return 0;
		}
		if (n > 0) {
			a->len += (size_t)n;
			a->buf[a->len] = '\0';
			char *nl;
			while (!a->done && (nl = strchr(a->buf, '\n'))) {
				*nl = '\0';
				char line[sizeof(a->buf)];
				snprintf(line, sizeof(line), "%s", a->buf);
				size_t used = (size_t)(nl - a->buf) + 1;
				memmove(a->buf, a->buf + used, a->len - used + 1);
				a->len -= used;
				/* after the last word the callback may free `a`: touch nothing */
				bool last = strncmp(line, "OK", 2) == 0 || strncmp(line, "NO", 2) == 0;
				dispatch(a, line);
				if (last) {
					return 0;
				}
			}
			return 0;
		}
	}
	if (!a->done) {
		finish(a);
		a->fn(a->data, AUTH_FAIL, "the authentication helper died", false);
	}
	return 0;
}

struct auth *auth_start(struct wl_event_loop *loop, const char *service, const char *user,
		const char *test_password, auth_fn fn, void *data) {
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		return NULL;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(sv[0]);
		close(sv[1]);
		return NULL;
	}
	if (pid == 0) {
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		signal(SIGPIPE, SIG_DFL);
		close(sv[0]);
		/* No exec here: let go of every descriptor twm holds (clients'
		 * sockets, DRM), or a client waiting for its socket to close never
		 * hears it. */
		long max = sysconf(_SC_OPEN_MAX);
		for (int fd = 3; fd < (max > 0 && max < 65536 ? max : 4096); fd++) {
			if (fd != sv[1]) {
				close(fd);
			}
		}
		child_fd = sv[1];
		child_main(service, user, test_password);
	}
	/* reaped with the others (server.c's SIGCHLD handler) */
	close(sv[1]);
	fcntl(sv[0], F_SETFD, FD_CLOEXEC);
	fcntl(sv[0], F_SETFL, O_NONBLOCK);
	struct auth *a = calloc(1, sizeof(*a));
	if (!a) {
		close(sv[0]);
		kill(pid, SIGKILL);
		return NULL;
	}
	a->fd = sv[0];
	a->pid = pid;
	a->fn = fn;
	a->data = data;
	a->source = wl_event_loop_add_fd(loop, a->fd, WL_EVENT_READABLE, readable, a);
	return a;
}

void auth_respond(struct auth *a, char *response) {
	size_t n = strlen(response);
	for (size_t i = 0; i < n; i++) {
		if (response[i] == '\n') {
			response[i] = ' ';
		}
	}
	if (a && !a->done && a->fd >= 0) {
		int flags = fcntl(a->fd, F_GETFL);
		fcntl(a->fd, F_SETFL, flags & ~O_NONBLOCK);
		if (write(a->fd, response, n) < 0 || write(a->fd, "\n", 1) < 0) {
			/* the child is gone; its hangup says so */
		}
		fcntl(a->fd, F_SETFL, flags);
	}
	wipe(response, n);
}

void auth_cancel(struct auth *a) {
	if (!a) {
		return;
	}
	if (!a->done) {
		kill(a->pid, SIGKILL);
	}
	finish(a);
	free(a);
}
