#ifndef TWM_SLEEP_H
#define TWM_SLEEP_H

struct twm_server;

/*
 * GRT-7: LockOnSleep, through logind (Linux): a delay inhibitor held while
 * awake; when logind says the machine is going to sleep, twm locks, then
 * lets go so it may. Without logind (FreeBSD, no system bus) it does
 * nothing.
 */
void sleep_init(struct twm_server *server);
void sleep_finish(struct twm_server *server);
/* The config changed: take or drop the inhibitor. */
void sleep_configure(struct twm_server *server);

#endif
