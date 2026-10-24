#ifndef TWM_GREETER_DATA_H
#define TWM_GREETER_DATA_H

#include <stdbool.h>
#include <stddef.h>

/*
 * The greeter's pure parts (GRT-3, GRT-6): greetd's JSON, the accounts, the
 * sessions, and what was chosen last. Unit-tested.
 */

/* --- greetd's protocol: native-endian u32 length, then JSON --- */

/* A request: `type` plus string fields (key, value pairs, NULL-ended). */
size_t greetd_request(char *out, size_t len, const char *type, const char *const *kv);
/* start_session: cmd and env as JSON string arrays. */
size_t greetd_start_session(char *out, size_t len, const char *const *cmd, const char *const *env);
/* A string field of a flat JSON object, unescaped; false if absent. */
bool json_string(const char *json, const char *key, char *out, size_t len);

/* --- who and what (GRT-3) --- */

#define GREETER_MAX_USERS 16
#define GREETER_MAX_SESSIONS 16

struct greeter_session {
	char id[64];   /* desktop file name without .desktop: "twm" */
	char name[96]; /* Name= */
	char exec[512];
	char desktops[96]; /* DesktopNames=, else the id */
};

/* Accounts a person logs into: uid 1000…59999 with a login shell. */
int greeter_users(char names[][64], int max);
/* wayland-sessions under each data dir (colon-separated), first one of an
 * id wins, hidden and missing-TryExec ones left out, sorted by name. */
int greeter_sessions(const char *dirs, struct greeter_session *out, int max);
/* $XDG_DATA_HOME and $XDG_DATA_DIRS (else /usr/local/share:/usr/share). */
void greeter_data_dirs(char *out, size_t len);

/* The greeter's memory: the last user, each user's last session. */
void greeter_state_load(const char *path, char *last_user, size_t ulen,
	const char *user, char *session, size_t slen);
void greeter_state_save(const char *path, const char *user, const char *session);
void greeter_state_path(char *out, size_t len);

#endif
