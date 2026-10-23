#include "histview.h"
#include "icons.h"
#include "imgr.h"
#include "logview.h"
#include "panner.h"
#include "server.h"
#include "ui.h"

static void handle_idle(void *data) {
	struct twm_server *server = data;
	server->ui_idle = NULL;
	icons_layout(server);
	imgr_refresh(server);
	panner_refresh(server);
	histview_changed(server);
	logview_refresh(server);
}

void ui_changed(struct twm_server *server) {
	if (!server->ui_idle && server->loop && !server->shutting_down) {
		server->ui_idle = wl_event_loop_add_idle(server->loop, handle_idle, server);
	}
}

void ui_init(struct twm_server *server) {
	(void)server;
}

void ui_finish(struct twm_server *server) {
	if (server->ui_idle) {
		wl_event_source_remove(server->ui_idle);
		server->ui_idle = NULL;
	}
}
