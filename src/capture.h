#ifndef TWM_CAPTURE_H
#define TWM_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct twm_server;
struct twm_output;
struct wl_client;

/*
 * Capture is always visible (CAP-1 … CAP-4). A client capturing the screen
 * (wlr-screencopy frames, ext-image-copy-capture sessions) marks its windows'
 * tabs with SCREEN; one without windows gets a root line that stays while it
 * captures. Markers can't be turned off.
 */
void capture_init(struct twm_server *server);
void capture_finish(struct twm_server *server);
/* Called on every output frame: look at pending screencopy frames. */
void capture_output_frame(struct twm_output *output);

/* A process's short name ("grim"), for messages. */
void capture_process_name(pid_t pid, char *buf, size_t len);
/* `--test`: the startup command (the test program) is trusted (D-7). */
bool capture_client_exempt(struct twm_server *server, struct wl_client *client);

#endif
