#ifndef TWM_APPS_H
#define TWM_APPS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * The installed applications (IMG-13): the .desktop entries in
 * <dir>/applications for XDG_DATA_HOME, then each of XDG_DATA_DIRS, the first
 * of an id winning, as the Icon Manager's RUN results offer them. Pure C, no
 * wlroots; unit-tested.
 */

struct app {
	char *id;      /* the desktop file id: "firefox-esr" (a subdir's slash as '-') */
	char *name;    /* Name= */
	char *generic; /* GenericName=, or NULL: "Web Browser" */
	char *command; /* Exec= without field codes, quoted for /bin/sh -c */
	bool terminal; /* Terminal=true: it runs in the configured Terminal */
};

struct app_list {
	struct app *apps;
	size_t n;
};

/*
 * Scan `data_home` (may be NULL) and the colon-separated `data_dirs`,
 * leaving out what isn't an application to launch: NoDisplay, Hidden,
 * Type other than Application, a TryExec not on PATH, and entries whose
 * OnlyShowIn / NotShowIn shut out `desktop` ("twm"). Sorted by name.
 */
void apps_scan(struct app_list *list, const char *data_home, const char *data_dirs,
	const char *desktop);
void apps_free(struct app_list *list);

/* The command a desktop entry's Exec runs, quoted for the shell, field codes
 * (%f %U %i …) left out; NULL when Exec is empty. Exposed for the tests. */
char *apps_exec_to_shell(const char *exec);

#endif
