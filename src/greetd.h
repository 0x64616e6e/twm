#ifndef TWM_GREETD_H
#define TWM_GREETD_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include "auth.h"
#include "greeter_data.h"

/*
 * The greeter's side of greetd (GRT-1, GRT-6): greetd does the PAM part
 * and the greeter relays its conversation — the same events as auth.c's.
 * And what the greeter shows: the accounts, the sessions, what was chosen
 * last (GRT-3). The JSON and the discovery are pure; unit-tested.
 */

struct greetd;
/* Connect ($GREETD_SOCK) and start `user`'s session conversation. */
struct greetd *greetd_start(struct wl_event_loop *loop, const char *user, auth_fn fn, void *data);
void greetd_respond(struct greetd *g, char *response);
/* Start the chosen session once authenticated; `fn` gets AUTH_OK when greetd
 * agreed (the greeter may exit), AUTH_FAIL otherwise. */
void greetd_launch(struct greetd *g, const char *const *cmd, const char *const *env);
/* Cancel the conversation and free it (never inside its own callback). */
void greetd_cancel(struct greetd *g);

#endif
