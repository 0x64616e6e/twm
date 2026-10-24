#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "levels.h"
#include "media.h"
#include "rootline.h"
#include "settings.h"

/*
 * Volume and brightness keys (LVL-*): the line levels.c writes runs in the
 * background (twm never waits on it), and what it prints becomes one root
 * line that changes in place while the key is pressed again, and goes 3 s
 * after the last press. No panel, no pop-up (P-4).
 */

#define LEVEL_TTL_MS 3000
#define KEY_VOLUME 0x766f6c756d65ull /* "volume": the root line's key for it */
#define KEY_MIC 0x6d6963ull
#define KEY_BRIGHT 0x627269676874ull

struct job {
	struct wl_list link; /* twm_server.media_jobs */
	struct twm_server *server;
	enum level_action action;
	int fd;
	struct wl_event_source *source;
	char out[2048];
	size_t len;
};

static void job_free(struct job *j) {
	wl_list_remove(&j->link);
	wl_event_source_remove(j->source);
	close(j->fd);
	free(j);
}

static void report(struct job *j) {
	struct twm_server *server = j->server;
	j->out[j->len] = '\0';
	bool brightness = j->action == LEVEL_BRIGHTER || j->action == LEVEL_DIMMER;
	const char *what = brightness ? "brightness" : j->action == LEVEL_MIC_MUTE ? "mic" : "volume";
	uint64_t key = brightness ? KEY_BRIGHT : j->action == LEVEL_MIC_MUTE ? KEY_MIC : KEY_VOLUME;
	int percent;
	bool muted;
	char text[128];
	if (!level_parse(j->out, &percent, &muted)) {
		/* LVL-3: say what's missing, once a press */
		snprintf(text, sizeof(text), "%s", brightness ?
			"no backlight to set (brightnessctl, or a writable /sys/class/backlight)" :
			"no mixer to set (wpctl, pactl, amixer or mixer)");
		rootline_set(server, key, ROOTLINE_ERROR, what, text, LEVEL_TTL_MS * 2);
		return;
	}
	if (j->action == LEVEL_MIC_MUTE) {
		percent = -1; /* the microphone: on or muted, its level isn't the news */
	}
	level_text(percent, muted, text, sizeof(text));
	rootline_set(server, key, ROOTLINE_INFO, what, text, LEVEL_TTL_MS);
}

static int handle_readable(int fd, uint32_t mask, void *data) {
	struct job *j = data;
	if (mask & WL_EVENT_READABLE) {
		ssize_t n = read(fd, j->out + j->len, sizeof(j->out) - 1 - j->len);
		if (n > 0) {
			j->len += (size_t)n;
			if (j->len < sizeof(j->out) - 1) {
				return 0;
			}
		} else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			return 0;
		}
	}
	/* end of output (or full): the line has had its say */
	report(j);
	job_free(j);
	return 0;
}

void media_level(struct twm_server *server, enum level_action action) {
	int step = action == LEVEL_BRIGHTER || action == LEVEL_DIMMER ?
		server->config->brightness_step : server->config->volume_step;
	char script[2048];
	level_script(action, step, script, sizeof(script));
	int fds[2];
	if (pipe(fds) < 0) {
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return;
	}
	if (pid == 0) {
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		dup2(fds[1], STDOUT_FILENO);
		int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDERR_FILENO);
		}
		close(fds[0]);
		close(fds[1]);
		execl("/bin/sh", "/bin/sh", "-c", script, (char *)NULL);
		_exit(127);
	}
	/* the child is reaped with the others (server.c's SIGCHLD handler) */
	close(fds[1]);
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	struct job *j = calloc(1, sizeof(*j));
	if (!j) {
		close(fds[0]);
		return;
	}
	j->server = server;
	j->action = action;
	j->fd = fds[0];
	j->source = wl_event_loop_add_fd(server->loop, fds[0], WL_EVENT_READABLE, handle_readable, j);
	wl_list_insert(&server->media_jobs, &j->link);
}

void media_init(struct twm_server *server) {
	wl_list_init(&server->media_jobs);
}

void media_finish(struct twm_server *server) {
	while (!wl_list_empty(&server->media_jobs)) {
		struct job *j = wl_container_of(server->media_jobs.next, j, link);
		job_free(j);
	}
}
