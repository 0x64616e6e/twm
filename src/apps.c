#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "apps.h"

static char *dup_str(const char *s) {
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d) {
		memcpy(d, s, n);
	}
	return d;
}

/* Exec's own quoting: double quotes, with \", \`, \$ and \\ inside them. */
static size_t split_exec(const char *exec, char args[][256], size_t max) {
	size_t n = 0, len = 0;
	bool quoted = false, started = false;
	char cur[256];
	for (const char *p = exec;; p++) {
		char c = *p;
		if (c == '\0' || ((c == ' ' || c == '\t') && !quoted)) {
			if ((started || len) && n < max) {
				cur[len] = '\0';
				memcpy(args[n++], cur, len + 1);
			}
			len = 0;
			started = false;
			if (c == '\0') {
				break;
			}
			continue;
		}
		if (c == '"') {
			quoted = !quoted;
			started = true;
			continue;
		}
		if (c == '\\' && quoted && p[1]) {
			c = *++p;
		}
		if (len + 1 < sizeof(cur)) {
			cur[len++] = c;
		}
	}
	return n;
}

char *apps_exec_to_shell(const char *exec) {
	char args[32][256];
	size_t n = split_exec(exec, args, 32);
	char out[2048];
	size_t o = 0;
	out[0] = '\0';
	for (size_t i = 0; i < n; i++) {
		const char *a = args[i];
		/* A field code alone stands for files or URLs: none are given. */
		if (a[0] == '%' && a[1] && !a[2] && a[1] != '%') {
			continue;
		}
		char arg[256];
		size_t al = 0;
		for (const char *p = a; *p && al + 1 < sizeof(arg); p++) {
			if (*p == '%' && p[1]) {
				p++;
				if (*p == '%') {
					arg[al++] = '%';
				}
				continue; /* %i, %c, %k inside an argument go */
			}
			arg[al++] = *p;
		}
		arg[al] = '\0';
		/* single quotes for the shell, ' as '\'' */
		if (o + 4 >= sizeof(out)) {
			break;
		}
		if (o) {
			out[o++] = ' ';
		}
		out[o++] = '\'';
		for (const char *p = arg; *p && o + 6 < sizeof(out); p++) {
			if (*p == '\'') {
				memcpy(out + o, "'\\''", 4);
				o += 4;
			} else {
				out[o++] = *p;
			}
		}
		out[o++] = '\'';
		out[o] = '\0';
	}
	return o ? dup_str(out) : NULL;
}

static bool on_path(const char *program) {
	if (strchr(program, '/')) {
		return access(program, X_OK) == 0;
	}
	const char *path = getenv("PATH");
	if (!path) {
		return false;
	}
	char dir[4096];
	for (const char *p = path; *p;) {
		size_t len = strcspn(p, ":");
		if (len && len < sizeof(dir) - strlen(program) - 2) {
			snprintf(dir, sizeof(dir), "%.*s/%s", (int)len, p, program);
			if (access(dir, X_OK) == 0) {
				return true;
			}
		}
		p += len + (p[len] == ':');
	}
	return false;
}

/* Does a ;-separated list name `desktop`? */
static bool lists(const char *list, const char *desktop) {
	char item[64];
	for (const char *p = list; *p;) {
		size_t len = strcspn(p, ";");
		snprintf(item, sizeof(item), "%.*s", (int)len, p);
		if (*item && strcasecmp(item, desktop) == 0) {
			return true;
		}
		p += len + (p[len] == ';');
	}
	return false;
}

static void trim(char *s) {
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
		s[--n] = '\0';
	}
}

/* One entry's [Desktop Entry] group; false if it isn't offered. */
static bool parse_entry(const char *path, const char *desktop, struct app *out) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	char line[2048], name[256] = "", generic[256] = "", exec[1024] = "", type[64] = "",
		try_exec[512] = "", only[512] = "", not_in[512] = "";
	bool inside = false, no_display = false, hidden = false, terminal = false;
	while (fgets(line, sizeof(line), f)) {
		trim(line);
		if (line[0] == '[') {
			inside = strcmp(line, "[Desktop Entry]") == 0;
			continue;
		}
		char *eq = strchr(line, '=');
		if (!inside || !eq || line[0] == '#') {
			continue;
		}
		*eq = '\0';
		const char *k = line, *v = eq + 1;
		while (*v == ' ') {
			v++;
		}
		size_t kl = strlen(k);
		while (kl && k[kl - 1] == ' ') {
			line[--kl] = '\0';
		}
		if (strcmp(k, "Name") == 0) {
			snprintf(name, sizeof(name), "%s", v);
		} else if (strcmp(k, "GenericName") == 0) {
			snprintf(generic, sizeof(generic), "%s", v);
		} else if (strcmp(k, "Exec") == 0) {
			snprintf(exec, sizeof(exec), "%s", v);
		} else if (strcmp(k, "Type") == 0) {
			snprintf(type, sizeof(type), "%s", v);
		} else if (strcmp(k, "TryExec") == 0) {
			snprintf(try_exec, sizeof(try_exec), "%s", v);
		} else if (strcmp(k, "OnlyShowIn") == 0) {
			snprintf(only, sizeof(only), "%s", v);
		} else if (strcmp(k, "NotShowIn") == 0) {
			snprintf(not_in, sizeof(not_in), "%s", v);
		} else if (strcmp(k, "NoDisplay") == 0) {
			no_display = strcmp(v, "true") == 0;
		} else if (strcmp(k, "Hidden") == 0) {
			hidden = strcmp(v, "true") == 0;
		} else if (strcmp(k, "Terminal") == 0) {
			terminal = strcmp(v, "true") == 0;
		}
	}
	fclose(f);
	if (strcmp(type, "Application") != 0 || no_display || hidden || !*name || !*exec ||
			(*try_exec && !on_path(try_exec)) || (*only && !lists(only, desktop)) ||
			(*not_in && lists(not_in, desktop))) {
		return false;
	}
	char *command = apps_exec_to_shell(exec);
	if (!command) {
		return false;
	}
	out->name = dup_str(name);
	out->generic = *generic ? dup_str(generic) : NULL;
	out->command = command;
	out->terminal = terminal;
	return true;
}

static bool known(const struct app_list *l, const char *id) {
	for (size_t i = 0; i < l->n; i++) {
		if (strcmp(l->apps[i].id, id) == 0) {
			return true;
		}
	}
	return false;
}

/* Every .desktop under `dir` (and one level of subdirectories, whose
 * names prefix the id with '-', as the spec says). */
static void scan_dir(struct app_list *l, size_t *cap, const char *dir, const char *prefix,
		const char *desktop, int depth) {
	DIR *d = opendir(dir);
	if (!d) {
		return;
	}
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') {
			continue;
		}
		char path[4096], id[512];
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(path, &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			if (depth == 0) {
				char sub[512];
				snprintf(sub, sizeof(sub), "%s%s-", prefix, e->d_name);
				scan_dir(l, cap, path, sub, desktop, depth + 1);
			}
			continue;
		}
		size_t nl = strlen(e->d_name);
		if (nl < 9 || strcmp(e->d_name + nl - 8, ".desktop") != 0) {
			continue;
		}
		snprintf(id, sizeof(id), "%s%.*s", prefix, (int)(nl - 8), e->d_name);
		if (known(l, id)) {
			continue; /* an earlier directory's entry of that id wins (or hides it) */
		}
		struct app a = {0};
		bool offered = parse_entry(path, desktop, &a);
		if (l->n == *cap) {
			*cap = *cap ? *cap * 2 : 64;
			struct app *grown = realloc(l->apps, *cap * sizeof(*grown));
			if (!grown) {
				break;
			}
			l->apps = grown;
		}
		/* Not offered still claims its id, so a user's NoDisplay copy in
		 * ~/.local/share hides the system's entry. */
		a.id = dup_str(id);
		if (!offered) {
			a.name = NULL;
		}
		l->apps[l->n++] = a;
	}
	closedir(d);
}

static int by_name(const void *a, const void *b) {
	const struct app *x = a, *y = b;
	return strcasecmp(x->name, y->name);
}

void apps_scan(struct app_list *list, const char *data_home, const char *data_dirs,
		const char *desktop) {
	*list = (struct app_list){0};
	size_t cap = 0;
	char dir[4096];
	if (data_home && *data_home) {
		snprintf(dir, sizeof(dir), "%s/applications", data_home);
		scan_dir(list, &cap, dir, "", desktop, 0);
	}
	const char *dirs = data_dirs && *data_dirs ? data_dirs : "/usr/local/share:/usr/share";
	for (const char *p = dirs; *p;) {
		size_t len = strcspn(p, ":");
		if (len) {
			snprintf(dir, sizeof(dir), "%.*s/applications", (int)len, p);
			scan_dir(list, &cap, dir, "", desktop, 0);
		}
		p += len + (p[len] == ':');
	}
	/* Drop the placeholders that only claimed an id. */
	size_t k = 0;
	for (size_t i = 0; i < list->n; i++) {
		if (list->apps[i].name) {
			list->apps[k++] = list->apps[i];
		} else {
			free(list->apps[i].id);
		}
	}
	list->n = k;
	if (list->n) {
		qsort(list->apps, list->n, sizeof(list->apps[0]), by_name);
	}
}

void apps_free(struct app_list *list) {
	for (size_t i = 0; i < list->n; i++) {
		free(list->apps[i].id);
		free(list->apps[i].name);
		free(list->apps[i].generic);
		free(list->apps[i].command);
	}
	free(list->apps);
	*list = (struct app_list){0};
}
