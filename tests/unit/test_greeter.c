/* Unit tests for the greeter's pure parts (GRT-3, GRT-6). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "greeter_data.h"

static void write_file(const char *path, const char *text) {
	FILE *f = fopen(path, "w");
	fputs(text, f);
	fclose(f);
}

int main(void) {
	char out[1024], v[256];
	/* greetd's JSON, both ways */
	const char *kv[] = {"username", "tom", NULL};
	greetd_request(out, sizeof(out), "create_session", kv);
	CHECK("GRT-6", strcmp(out, "{\"type\":\"create_session\",\"username\":\"tom\"}") == 0);
	const char *kv2[] = {"response", "pa\"ss\\w\nd", NULL};
	greetd_request(out, sizeof(out), "post_auth_message_response", kv2);
	CHECK("GRT-6", strstr(out, "\"response\":\"pa\\\"ss\\\\w\\nd\"") != NULL);
	const char *cmd[] = {"/bin/sh", "-c", "exec twm-session", NULL};
	const char *env[] = {"XDG_SESSION_TYPE=wayland", NULL};
	greetd_start_session(out, sizeof(out), cmd, env);
	CHECK("GRT-6", strcmp(out, "{\"type\":\"start_session\",\"cmd\":[\"/bin/sh\",\"-c\","
		"\"exec twm-session\"],\"env\":[\"XDG_SESSION_TYPE=wayland\"]}") == 0);
	const char *msg = "{\"type\": \"auth_message\", \"auth_message_type\":\"secret\","
		" \"auth_message\": \"Password:\\u00a0\"}";
	CHECK("GRT-6", json_string(msg, "type", v, sizeof(v)) && strcmp(v, "auth_message") == 0);
	CHECK("GRT-6", json_string(msg, "auth_message_type", v, sizeof(v)) && strcmp(v, "secret") == 0);
	CHECK("GRT-6", json_string(msg, "auth_message", v, sizeof(v)) && strcmp(v, "Password:\xc2\xa0") == 0);
	CHECK("GRT-6", !json_string(msg, "description", v, sizeof(v)));
	/* a key named inside a value doesn't count */
	CHECK("GRT-6", !json_string("{\"a\":\"\\\"type\\\":\\\"x\\\"\"}", "type", v, sizeof(v)));

	/* sessions from data dirs: first id wins, hidden and missing TryExec out */
	char dir[] = "/tmp/twm-greeter-XXXXXX";
	if (!mkdtemp(dir)) {
		return 1;
	}
	char a[600], b[600], p[1400];
	snprintf(a, sizeof(a), "%s/a/wayland-sessions", dir);
	snprintf(b, sizeof(b), "%s/b/wayland-sessions", dir);
	snprintf(p, sizeof(p), "mkdir -p '%s' '%s'", a, b);
	if (system(p) != 0) {
		return 1;
	}
	snprintf(p, sizeof(p), "%s/twm.desktop", a);
	write_file(p, "[Desktop Entry]\nName=Tom's WM\nExec=twm-session\nDesktopNames=twm\n");
	snprintf(p, sizeof(p), "%s/twm.desktop", b);
	write_file(p, "[Desktop Entry]\nName=Shadowed\nExec=nope\n");
	snprintf(p, sizeof(p), "%s/hidden.desktop", b);
	write_file(p, "[Desktop Entry]\nName=Hidden\nExec=x\nHidden=true\n");
	snprintf(p, sizeof(p), "%s/missing.desktop", b);
	write_file(p, "[Desktop Entry]\nName=Missing\nExec=x\nTryExec=no-such-program-here\n");
	snprintf(p, sizeof(p), "%s/sway.desktop", b);
	write_file(p, "[Desktop Entry]\nName=Sway\nExec=sway\n[Desktop Action x]\nName=Not this\n");
	char dirs[1300];
	snprintf(dirs, sizeof(dirs), "%s/a:%s/b", dir, dir);
	struct greeter_session s[GREETER_MAX_SESSIONS];
	int n = greeter_sessions(dirs, s, GREETER_MAX_SESSIONS);
	CHECK("GRT-3", n == 2);
	CHECK("GRT-3", n == 2 && strcmp(s[0].name, "Sway") == 0 && strcmp(s[1].name, "Tom's WM") == 0);
	CHECK("GRT-3", n == 2 && strcmp(s[1].exec, "twm-session") == 0 && strcmp(s[1].desktops, "twm") == 0 &&
		strcmp(s[0].desktops, "sway") == 0);

	/* the greeter's memory */
	char st[700];
	snprintf(st, sizeof(st), "%s/state/twm/greeter", dir);
	greeter_state_save(st, "tom", "twm");
	greeter_state_save(st, "ana", "sway");
	char last[64], sess[64];
	greeter_state_load(st, last, sizeof(last), "tom", sess, sizeof(sess));
	CHECK("GRT-3", strcmp(last, "ana") == 0 && strcmp(sess, "twm") == 0);
	greeter_state_save(st, "tom", "sway");
	greeter_state_load(st, last, sizeof(last), "tom", sess, sizeof(sess));
	CHECK("GRT-3", strcmp(last, "tom") == 0 && strcmp(sess, "sway") == 0);
	greeter_state_load(st, NULL, 0, "ana", sess, sizeof(sess));
	CHECK("GRT-3", strcmp(sess, "sway") == 0);

	char users[GREETER_MAX_USERS][64];
	int nu = greeter_users(users, GREETER_MAX_USERS);
	CHECK("GRT-3", nu >= 0 && nu <= GREETER_MAX_USERS); /* whatever this machine has */
	snprintf(p, sizeof(p), "rm -rf '%s'", dir);
	if (system(p) != 0) {
		return 1;
	}
	return UNIT_RESULT();
}
