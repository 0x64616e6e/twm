#ifndef TWM_ACTIONS_H
#define TWM_ACTIONS_H

#include <stdint.h>
#include "server.h"
#include "settings.h"

/* Where a function was invoked from, so it knows its target. */
struct action_ctx {
	struct twm_view *view;   /* the window under the pointer, or focused */
	enum twm_context context;
	uint32_t button;         /* 0 for keys */
	double lx, ly;           /* pointer, layout coordinates */
};

/*
 * IMG-9: run `cmd` in the configured Terminal. A `%s` in Terminal stands for
 * the command ("xterm -e %s"); otherwise the command is appended ("foot").
 */
void action_run_in_terminal(struct twm_server *server, const char *cmd);

/* Run a bound function (KEY, FRM-12, OUT-8, OUT-9). */
void action_run(struct twm_server *server, const struct twm_func *func,
	const struct action_ctx *ctx);

#endif
