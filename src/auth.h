#ifndef TWM_AUTH_H
#define TWM_AUTH_H

#include <stdbool.h>
#include <wayland-server-core.h>

/*
 * The PAM conversation (GRT-6), in a child process so twm never waits on
 * it. The child asks; the lock or the greeter shows each question as it
 * comes and sends back the answer. One conversation, one try: a failure
 * ends it, and the next try is a new one.
 */

enum auth_event {
	AUTH_PROMPT, /* text: the question; echo: show what is typed */
	AUTH_INFO,
	AUTH_ERROR,
	AUTH_OK,
	AUTH_FAIL,   /* text: PAM's reason */
};

typedef void (*auth_fn)(void *data, enum auth_event ev, const char *text, bool echo);

struct auth;

/* `test_password`: --test only (GRT-9), checked instead of PAM; NULL for PAM. */
struct auth *auth_start(struct wl_event_loop *loop, const char *service, const char *user,
	const char *test_password, auth_fn fn, void *data);
/* The password check (GRT-11, GRT-12): PAM's twm-password service, or,
 * where PAM can't check a password unprivileged, twm-chkpwd (D-13). */
struct auth *auth_start_password(struct wl_event_loop *loop, const char *user,
	const char *test_password, auth_fn fn, void *data);
/* Answer the current question; the text is wiped from memory after. */
void auth_respond(struct auth *a, char *response);
/* End it now; no more events. */
void auth_cancel(struct auth *a);
/* The PAM service to use: "twm" if installed, else "login". */
const char *auth_service(void);
/* GRT-11: the password alone ("twm-password", else the above), and the
 * fingerprint reader alone ("twm-fingerprint" when pam_fprintd is
 * installed, else NULL). */
const char *auth_password_service(void);
const char *auth_fingerprint_service(void);
/* Whether PAM was built in. */
bool auth_available(void);
/* Whether a password can be checked from this process at all (GRT-12):
 * not on FreeBSD's OpenPAM unless root. */
bool auth_password_checkable(void);

#endif
