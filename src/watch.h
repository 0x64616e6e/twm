#ifndef TWM_WATCH_H
#define TWM_WATCH_H

struct twm_server;

/*
 * Hot reload trigger (CFG-5, D-9): watch the config file's directory, so
 * editors that save by writing a new file and renaming it are seen too.
 * inotify on Linux, kqueue on the BSDs. Changes are debounced and handed to
 * rc_reload, which ignores saves that didn't change the text.
 */
void watch_init(struct twm_server *server, const char *path);
void watch_finish(struct twm_server *server);

#endif
