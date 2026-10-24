#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "ipcclient.h"

bool ipc_socket_path(const char *display, char *buf, size_t len) {
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (!dir || !*dir || !display || !*display) {
		return false;
	}
	int n = snprintf(buf, len, "%s/twm.%s.sock", dir, display);
	return n > 0 && (size_t)n < len && (size_t)n < sizeof(((struct sockaddr_un *)0)->sun_path);
}

int ipcc_connect(bool quiet) {
	char path[108];
	const char *sock = getenv("TWM_SOCK");
	if (sock && *sock) {
		snprintf(path, sizeof(path), "%s", sock);
	} else if (!ipc_socket_path(getenv("WAYLAND_DISPLAY"), path, sizeof(path))) {
		if (!quiet) {
			fprintf(stderr, "twm: can't find a running twm (set WAYLAND_DISPLAY or TWM_SOCK)\n");
		}
		return -1;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if (!quiet) {
			fprintf(stderr, "twm: can't reach %s: %s\n", path, strerror(errno));
		}
		if (fd >= 0) {
			close(fd);
		}
		return -1;
	}
	return fd;
}

void ipcc_write_all(int fd, const char *s, size_t n) {
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

void ipcc_read_reply(int fd, char *buf, size_t len) {
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

bool ipcc_request(const char *request, char *reply, size_t len, bool quiet) {
	int fd = ipcc_connect(quiet);
	if (fd < 0) {
		reply[0] = '\0';
		return false;
	}
	ipcc_write_all(fd, request, strlen(request));
	ipcc_write_all(fd, "\n", 1);
	ipcc_read_reply(fd, reply, len);
	close(fd);
	return true;
}
