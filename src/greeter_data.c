#ifdef __FreeBSD__
#undef _POSIX_C_SOURCE /* getpwent */
#else
#define _DEFAULT_SOURCE /* getpwent */
#endif
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "greeter_data.h"

/* --- JSON, as much as greetd needs --- */

static size_t jstr(char *out, size_t len, size_t o, const char *s) {
	if (o < len) {
		out[o] = '"';
	}
	o++;
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		char esc[8] = "";
		if (c == '"' || c == '\\') {
			snprintf(esc, sizeof(esc), "\\%c", c);
		} else if (c == '\n') {
			snprintf(esc, sizeof(esc), "\\n");
		} else if (c < 0x20) {
			snprintf(esc, sizeof(esc), "\\u%04x", c);
		}
		if (esc[0]) {
			for (char *e = esc; *e; e++, o++) {
				if (o < len) {
					out[o] = *e;
				}
			}
		} else {
			if (o < len) {
				out[o] = (char)c;
			}
			o++;
		}
	}
	if (o < len) {
		out[o] = '"';
	}
	return o + 1;
}

static size_t lit(char *out, size_t len, size_t o, const char *s) {
	for (; *s; s++, o++) {
		if (o < len) {
			out[o] = *s;
		}
	}
	return o;
}

size_t greetd_request(char *out, size_t len, const char *type, const char *const *kv) {
	size_t o = lit(out, len, 0, "{\"type\":");
	o = jstr(out, len, o, type);
	for (int i = 0; kv && kv[i] && kv[i + 1]; i += 2) {
		o = lit(out, len, o, ",");
		o = jstr(out, len, o, kv[i]);
		o = lit(out, len, o, ":");
		o = jstr(out, len, o, kv[i + 1]);
	}
	o = lit(out, len, o, "}");
	if (len) {
		out[o < len ? o : len - 1] = '\0';
	}
	return o;
}

static size_t jarray(char *out, size_t len, size_t o, const char *const *a) {
	o = lit(out, len, o, "[");
	for (int i = 0; a && a[i]; i++) {
		if (i) {
			o = lit(out, len, o, ",");
		}
		o = jstr(out, len, o, a[i]);
	}
	return lit(out, len, o, "]");
}

size_t greetd_start_session(char *out, size_t len, const char *const *cmd, const char *const *env) {
	size_t o = lit(out, len, 0, "{\"type\":\"start_session\",\"cmd\":");
	o = jarray(out, len, o, cmd);
	o = lit(out, len, o, ",\"env\":");
	o = jarray(out, len, o, env);
	o = lit(out, len, o, "}");
	if (len) {
		out[o < len ? o : len - 1] = '\0';
	}
	return o;
}

static int hexv(char c) {
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
		c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Copy the string starting after its opening quote; returns its end. */
static const char *read_string(const char *p, char *out, size_t len) {
	size_t o = 0;
	while (*p && *p != '"') {
		unsigned cp = (unsigned char)*p;
		if (*p == '\\' && p[1]) {
			p++;
			switch (*p) {
			case 'n': cp = '\n'; break;
			case 't': cp = '\t'; break;
			case 'r': cp = '\r'; break;
			case 'b': cp = '\b'; break;
			case 'f': cp = '\f'; break;
			case 'u': {
				cp = 0;
				for (int k = 1; k <= 4; k++) {
					int h = hexv(p[k]);
					if (h < 0) {
						return NULL;
					}
					cp = cp << 4 | (unsigned)h;
				}
				p += 4;
				break;
			}
			default: cp = (unsigned char)*p; break;
			}
		}
		/* UTF-8 out (BMP is enough for greetd's messages) */
		char u[4];
		int n = 0;
		if (cp < 0x80) {
			u[n++] = (char)cp;
		} else if (cp < 0x800) {
			u[n++] = (char)(0xC0 | cp >> 6);
			u[n++] = (char)(0x80 | (cp & 0x3F));
		} else {
			u[n++] = (char)(0xE0 | cp >> 12);
			u[n++] = (char)(0x80 | (cp >> 6 & 0x3F));
			u[n++] = (char)(0x80 | (cp & 0x3F));
		}
		for (int k = 0; k < n; k++) {
			if (o + 1 < len) {
				out[o++] = u[k];
			}
		}
		p++;
	}
	if (len) {
		out[o] = '\0';
	}
	return *p == '"' ? p + 1 : NULL;
}

bool json_string(const char *json, const char *key, char *out, size_t len) {
	size_t kl = strlen(key);
	/* walk the top level, skipping strings, so a key inside a value
	 * doesn't count */
	int depth = 0;
	for (const char *p = json; p && *p; p++) {
		if (*p == '{' || *p == '[') {
			depth++;
		} else if (*p == '}' || *p == ']') {
			depth--;
		} else if (*p == '"') {
			const char *start = p + 1;
			char tmp[256];
			const char *end = read_string(start, tmp, sizeof(tmp));
			if (!end) {
				return false;
			}
			const char *q = end;
			while (*q == ' ' || *q == '\t' || *q == '\n') {
				q++;
			}
			bool is_key = *q == ':';
			if (is_key && depth == 1 && (size_t)(end - 1 - start) == kl &&
					strncmp(start, key, kl) == 0) {
				q++;
				while (*q == ' ' || *q == '\t' || *q == '\n') {
					q++;
				}
				if (*q != '"') {
					return false; /* not a string */
				}
				return read_string(q + 1, out, len) != NULL;
			}
			p = end - 1;
		}
	}
	return false;
}

/* --- accounts --- */

static bool login_shell(const char *shell) {
	if (!shell || !*shell) {
		return false;
	}
	const char *base = strrchr(shell, '/');
	base = base ? base + 1 : shell;
	return strcmp(base, "nologin") != 0 && strcmp(base, "false") != 0;
}

int greeter_users(char names[][64], int max) {
	int n = 0;
	uid_t uids[GREETER_MAX_USERS];
	setpwent();
	struct passwd *pw;
	while ((pw = getpwent()) && n < max && n < GREETER_MAX_USERS) {
		if (pw->pw_uid < 1000 || pw->pw_uid >= 60000 || !login_shell(pw->pw_shell)) {
			continue;
		}
		/* in uid order */
		int i = n++;
		while (i > 0 && uids[i - 1] > pw->pw_uid) {
			uids[i] = uids[i - 1];
			memcpy(names[i], names[i - 1], 64);
			i--;
		}
		uids[i] = pw->pw_uid;
		snprintf(names[i], 64, "%s", pw->pw_name);
	}
	endpwent();
	return n;
}

/* --- sessions --- */

static bool in_path(const char *prog) {
	if (!*prog) {
		return false;
	}
	if (prog[0] == '/') {
		return access(prog, X_OK) == 0;
	}
	const char *path = getenv("PATH");
	char buf[4096];
	snprintf(buf, sizeof(buf), "%s", path ? path : "/usr/local/bin:/usr/bin:/bin");
	for (char *dir = strtok(buf, ":"); dir; dir = strtok(NULL, ":")) {
		char p[4096];
		snprintf(p, sizeof(p), "%s/%s", dir, prog);
		if (access(p, X_OK) == 0) {
			return true;
		}
	}
	return false;
}

static bool read_session(const char *path, const char *id, struct greeter_session *s) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	memset(s, 0, sizeof(*s));
	snprintf(s->id, sizeof(s->id), "%s", id);
	char line[1024];
	bool in_entry = false, hidden = false;
	char tryexec[512] = "";
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '[') {
			in_entry = strcmp(line, "[Desktop Entry]") == 0;
			continue;
		}
		if (!in_entry) {
			continue;
		}
		if (strncmp(line, "Name=", 5) == 0) {
			snprintf(s->name, sizeof(s->name), "%.*s", (int)sizeof(s->name) - 1, line + 5);
		} else if (strncmp(line, "Exec=", 5) == 0) {
			snprintf(s->exec, sizeof(s->exec), "%.*s", (int)sizeof(s->exec) - 1, line + 5);
		} else if (strncmp(line, "TryExec=", 8) == 0) {
			snprintf(tryexec, sizeof(tryexec), "%.*s", (int)sizeof(tryexec) - 1, line + 8);
		} else if (strncmp(line, "DesktopNames=", 13) == 0) {
			snprintf(s->desktops, sizeof(s->desktops), "%.*s", (int)sizeof(s->desktops) - 1, line + 13);
			size_t n = strlen(s->desktops);
			if (n && s->desktops[n - 1] == ';') {
				s->desktops[n - 1] = '\0';
			}
			for (char *p = s->desktops; *p; p++) {
				if (*p == ';') {
					*p = ':';
				}
			}
		} else if (strcmp(line, "Hidden=true") == 0 || strcmp(line, "NoDisplay=true") == 0) {
			hidden = true;
		}
	}
	fclose(f);
	if (!s->desktops[0]) {
		snprintf(s->desktops, sizeof(s->desktops), "%s", id);
	}
	return !hidden && s->name[0] && s->exec[0] && (!tryexec[0] || in_path(tryexec));
}

int greeter_sessions(const char *dirs, struct greeter_session *out, int max) {
	int n = 0;
	char buf[4096];
	snprintf(buf, sizeof(buf), "%s", dirs);
	char *save = NULL;
	for (char *dir = strtok_r(buf, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
		char sd[4200];
		snprintf(sd, sizeof(sd), "%s/wayland-sessions", dir);
		DIR *d = opendir(sd);
		struct dirent *e;
		while (d && (e = readdir(d)) && n < max) {
			size_t l = strlen(e->d_name);
			if (l < 9 || strcmp(e->d_name + l - 8, ".desktop") != 0) {
				continue;
			}
			char id[64];
			snprintf(id, sizeof(id), "%.*s", (int)(l - 8), e->d_name);
			bool seen = false;
			for (int i = 0; i < n; i++) {
				seen |= strcmp(out[i].id, id) == 0;
			}
			char path[4400];
			snprintf(path, sizeof(path), "%s/%s", sd, e->d_name);
			struct greeter_session s;
			if (!seen && read_session(path, id, &s)) {
				out[n++] = s;
			}
		}
		if (d) {
			closedir(d);
		}
	}
	/* by name */
	for (int i = 1; i < n; i++) {
		struct greeter_session t = out[i];
		int j = i;
		while (j > 0 && strcmp(out[j - 1].name, t.name) > 0) {
			out[j] = out[j - 1];
			j--;
		}
		out[j] = t;
	}
	return n;
}

void greeter_data_dirs(char *out, size_t len) {
	const char *dh = getenv("XDG_DATA_HOME"), *dd = getenv("XDG_DATA_DIRS"), *home = getenv("HOME");
	char first[1100] = "";
	if (dh && *dh) {
		snprintf(first, sizeof(first), "%s:", dh);
	} else if (home && *home) {
		snprintf(first, sizeof(first), "%s/.local/share:", home);
	}
	snprintf(out, len, "%s%s", first, dd && *dd ? dd : "/usr/local/share:/usr/share");
}

/* --- memory: "last\tuser", then "user\tsession" lines --- */

void greeter_state_path(char *out, size_t len) {
	const char *st = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
	if (st && *st) {
		snprintf(out, len, "%s/twm/greeter", st);
	} else {
		snprintf(out, len, "%s/.local/state/twm/greeter", home && *home ? home : "/tmp");
	}
}

void greeter_state_load(const char *path, char *last_user, size_t ulen,
		const char *user, char *session, size_t slen) {
	if (last_user && ulen) {
		last_user[0] = '\0';
	}
	if (session && slen) {
		session[0] = '\0';
	}
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		char *tab = strchr(line, '\t');
		if (!tab) {
			continue;
		}
		*tab = '\0';
		if (strcmp(line, "last") == 0 && last_user) {
			snprintf(last_user, ulen, "%s", tab + 1);
		} else if (user && session && strcmp(line, user) == 0) {
			snprintf(session, slen, "%s", tab + 1);
		}
	}
	fclose(f);
}

static void mkdirs_for(const char *path) {
	char p[4096];
	snprintf(p, sizeof(p), "%s", path);
	char *slash = strrchr(p, '/');
	if (!slash) {
		return;
	}
	*slash = '\0';
	for (char *s = p + 1; *s; s++) {
		if (*s == '/') {
			*s = '\0';
			mkdir(p, 0700);
			*s = '/';
		}
	}
	mkdir(p, 0700);
}

void greeter_state_save(const char *path, const char *user, const char *session) {
	char old[8192] = "";
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(old, 1, sizeof(old) - 1, f) : 0;
	if (f) {
		fclose(f);
	}
	old[n] = '\0';
	mkdirs_for(path);
	char tmp[4200];
	snprintf(tmp, sizeof(tmp), "%s.part", path);
	f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	fprintf(f, "last\t%s\n", user);
	if (session && *session) {
		fprintf(f, "%s\t%s\n", user, session);
	}
	/* everyone else's choices stay */
	for (char *l = strtok(old, "\n"); l; l = strtok(NULL, "\n")) {
		char *tab = strchr(l, '\t');
		if (!tab || strncmp(l, "last\t", 5) == 0 ||
				((size_t)(tab - l) == strlen(user) && strncmp(l, user, strlen(user)) == 0)) {
			continue;
		}
		fprintf(f, "%s\n", l);
	}
	if (fclose(f) == 0) {
		rename(tmp, path);
	}
}
