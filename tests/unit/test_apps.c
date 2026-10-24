/* Unit tests for the installed applications (IMG-13). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "apps.h"
#include "check.h"

static char root[256];

static void put(const char *rel, const char *text) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", root, rel);
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s", path);
	for (char *p = dir + strlen(root) + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(dir, 0755);
			*p = '/';
		}
	}
	FILE *f = fopen(path, "w");
	fputs(text, f);
	fclose(f);
}

static const struct app *find(const struct app_list *l, const char *id) {
	for (size_t i = 0; i < l->n; i++) {
		if (strcmp(l->apps[i].id, id) == 0) {
			return &l->apps[i];
		}
	}
	return NULL;
}

static int str_is(const char *got, const char *want) {
	return got && want && strcmp(got, want) == 0;
}

int main(void) {
	/* Exec, as the shell will run it: field codes gone, every argument quoted. */
	char *c = apps_exec_to_shell("firefox-esr %u");
	CHECK("IMG-13", str_is(c, "'firefox-esr'"));
	free(c);
	c = apps_exec_to_shell("sh -c \"echo \\\"a b\\\"; x\" %F");
	CHECK("IMG-13", str_is(c, "'sh' '-c' 'echo \"a b\"; x'"));
	free(c);
	c = apps_exec_to_shell("prog --rate=50%% --icon=%i it's");
	CHECK("IMG-13", str_is(c, "'prog' '--rate=50%' '--icon=' 'it'\\''s'"));
	free(c);
	CHECK("IMG-13", apps_exec_to_shell("") == NULL);

	snprintf(root, sizeof(root), "/tmp/twm-apps-XXXXXX");
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}
	put("home/applications/firefox.desktop",
		"[Desktop Entry]\nType=Application\nName=Firefox\nExec=firefox\nNoDisplay=true\n");
	put("sys/applications/firefox.desktop",
		"[Desktop Entry]\nType=Application\nName=Firefox\nExec=firefox %u\n");
	put("sys/applications/foot.desktop",
		"[Desktop Entry]\nType=Application\nName=Foot\nGenericName=Terminal\nExec=foot\n"
		"[Desktop Action server]\nName=Foot Server\nExec=foot --server\n");
	put("sys/applications/htop.desktop",
		"[Desktop Entry]\nType=Application\nName=Htop\nExec=htop\nTerminal=true\n");
	put("sys/applications/gnome-only.desktop",
		"[Desktop Entry]\nType=Application\nName=Gnome Thing\nExec=g\nOnlyShowIn=GNOME;\n");
	put("sys/applications/everywhere.desktop",
		"[Desktop Entry]\nType=Application\nName=Everywhere\nExec=e\nOnlyShowIn=GNOME;twm;\n");
	put("sys/applications/not-twm.desktop",
		"[Desktop Entry]\nType=Application\nName=Not Here\nExec=n\nNotShowIn=twm;\n");
	put("sys/applications/gone.desktop",
		"[Desktop Entry]\nType=Application\nName=Gone\nExec=gone\nTryExec=no-such-program-anywhere\n");
	put("sys/applications/link.desktop", "[Desktop Entry]\nType=Link\nName=Link\nURL=x\n");
	put("sys/applications/kde/app.desktop",
		"[Desktop Entry]\nType=Application\nName=Sub App\nExec=\"/opt/x y/run\" --flag\n");

	char home[600], dirs[1200];
	snprintf(home, sizeof(home), "%s/home", root);
	snprintf(dirs, sizeof(dirs), "%s/sys:%s/none", root, root);
	struct app_list l;
	apps_scan(&l, home, dirs, "twm");

	const struct app *foot = find(&l, "foot");
	CHECK("IMG-13", foot && str_is(foot->name, "Foot") && str_is(foot->generic, "Terminal") &&
		str_is(foot->command, "'foot'") && !foot->terminal);
	const struct app *htop = find(&l, "htop");
	CHECK("IMG-13", htop && htop->terminal);
	CHECK("IMG-13", find(&l, "everywhere") != NULL);
	const struct app *sub = find(&l, "kde-app");
	CHECK("IMG-13", sub && str_is(sub->command, "'/opt/x y/run' '--flag'"));
	/* left out: a user's NoDisplay copy hides the system's, and the rest */
	CHECK("IMG-13", find(&l, "firefox") == NULL);
	CHECK("IMG-13", find(&l, "gnome-only") == NULL && find(&l, "not-twm") == NULL);
	CHECK("IMG-13", find(&l, "gone") == NULL && find(&l, "link") == NULL);
	CHECK_EQ("IMG-13", l.n, 4);
	/* sorted by name */
	CHECK("IMG-13", l.n == 4 && str_is(l.apps[0].name, "Everywhere") &&
		str_is(l.apps[3].name, "Sub App"));
	apps_free(&l);

	char cmd[600];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
	if (system(cmd) != 0) {
		fprintf(stderr, "could not remove %s\n", root);
	}
	return UNIT_RESULT();
}
