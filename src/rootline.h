#ifndef TWM_ROOTLINE_H
#define TWM_ROOTLINE_H

#include <stdbool.h>
#include <time.h>
#include "server.h"

/*
 * The root line (ROOT-5 … ROOT-7): compositor messages written as one line
 * each on the root, above the status line. No toasts.
 */

enum rootline_kind {
	ROOTLINE_INFO,
	ROOTLINE_ERROR,
};

#define ROOTLINE_KEEP 200    /* messages kept for the log (ROOT-8) */
#define ROOTLINE_VISIBLE 3

struct rootline_msg {
	enum rootline_kind kind;
	time_t when;
	uint64_t expires_ms; /* monotonic; 0 = until replaced (CAP-3) */
	char source[16];
	char text[200];
};

struct twm_rootline {
	struct rootline_msg msgs[ROOTLINE_KEEP];
	size_t count, head; /* ring buffer: head = next slot */
	struct wlr_scene_buffer *node;
	struct wl_event_source *timer;
};

void rootline_init(struct twm_server *server);
void rootline_finish(struct twm_server *server);
void rootline_post(struct twm_server *server, enum rootline_kind kind,
	const char *source, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
/* Redraw after an output, theme or timeout change. */
void rootline_update(struct twm_server *server);

/* The i-th newest message (0 = latest), expired ones too; NULL past the end. */
const struct rootline_msg *rootline_get(struct twm_server *server, size_t i);

/* Format one line as drawn: "HH:MM  source   text" (pure, for tests). */
void rootline_format(const struct rootline_msg *msg, char *buf, size_t len);

#endif
