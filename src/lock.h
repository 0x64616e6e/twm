#ifndef TWM_LOCK_H
#define TWM_LOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

struct twm_server;

/*
 * The lock (GRT-1 … GRT-7) and idle: twm's own, in the trusted frame, PAM
 * underneath. While locked every window is hidden, input goes to the lock
 * alone, and urgency waits. Idle locks after IdleLock seconds and turns the
 * outputs off after IdleScreen; a client's idle inhibitor holds both.
 */
void lock_init(struct twm_server *server);
void lock_finish(struct twm_server *server);

/* f.lock, twm --lock, idle, sleep. `why` for the root line: "on idle". */
void lock_engage(struct twm_server *server, const char *why);
bool lock_active(struct twm_server *server);

/* A key pressed while locked (real devices only). */
void lock_key(struct twm_server *server, xkb_keysym_t sym, const char *utf8, uint32_t mods,
	bool caps);
/* GRT-1: twm --greeter: the sign-in screen, greetd underneath. */
void lock_greet(struct twm_server *server);
/* GRT-3: a press on the greeter (layout coordinates). */
void lock_button(struct twm_server *server, double lx, double ly);
/* Any input: the idle timers start over, dark outputs light up. */
void lock_activity(struct twm_server *server);
/* The config or the outputs changed. */
void lock_configure(struct twm_server *server);

#endif
