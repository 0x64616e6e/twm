#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>
#include "actions.h"
#include "desktop.h"
#include "grab.h"
#include "histview.h"
#include "logview.h"
#include "menu.h"
#include "rc.h"
#include "rootline.h"
#include "view.h"

/* Functions whose machinery arrives in a later milestone say so once on the
 * root line instead of failing silently. */
static void not_yet(struct twm_server *server, enum twm_func_kind kind, const char *milestone) {
	rootline_post(server, ROOTLINE_INFO, "twm", "%s arrives in %s",
		config_func_name(kind), milestone);
}

void action_run_in_terminal(struct twm_server *server, const char *cmd) {
	const char *term = server->config->terminal;
	char line[2048];
	const char *pct = strstr(term, "%s");
	if (pct) {
		snprintf(line, sizeof(line), "%.*s%s%s", (int)(pct - term), term, cmd, pct + 2);
	} else {
		snprintf(line, sizeof(line), "%s %s", term, cmd);
	}
	server_spawn(server, line);
}

void action_run(struct twm_server *server, const struct twm_func *func,
		const struct action_ctx *ctx) {
	struct twm_view *view = ctx->view;
	switch (func->kind) {
	case TWM_F_NOP:
	case TWM_F_SEPARATOR:
		return;
	case TWM_F_EXEC:
		server_spawn(server, func->arg);
		return;
	case TWM_F_TERMINAL:
		server_spawn(server, server->config->terminal);
		return;
	case TWM_F_RELOAD:
		rc_reload(server, true, NULL, 0);
		return;
	case TWM_F_QUIT:
		wl_display_terminate(server->display);
		return;
	case TWM_F_MOVE:
		if (view && !grab_active(server)) {
			if (ctx->context == TWM_CTX_ICON) {
				grab_begin_icon(server, view, ctx->button); /* ROOT-10 */
			} else {
				grab_begin_move(server, view, ctx->button);
			}
		}
		return;
	case TWM_F_RAISEMOVE:
		if (view && !grab_active(server)) {
			view_raise(view);
			view_focus(view);
			grab_begin_pending_move(server, view, ctx->button);
		}
		return;
	case TWM_F_RESIZE:
		if (view && !grab_active(server)) {
			uint32_t edges = WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
			if (ctx->context == TWM_CTX_WINDOW || ctx->context == TWM_CTX_BORDER) {
				struct wlr_box f = view_frame_box(view);
				edges = geo_edges_at((struct geo_box){f.x, f.y, f.width, f.height},
					(int)ctx->lx, (int)ctx->ly);
			}
			if (ctx->button) {
				view_focus(view);
			}
			grab_begin_resize(server, view, ctx->button, edges);
		}
		return;
	case TWM_F_RAISE:
		if (view) {
			view_raise(view);
		}
		return;
	case TWM_F_LOWER:
		if (view) {
			view_lower(view);
		}
		return;
	case TWM_F_RAISELOWER:
		if (view) {
			struct twm_view *top = wl_list_empty(&server->views) ? NULL :
				wl_container_of(server->views.next, top, link);
			if (top == view) {
				view_lower(view);
			} else {
				view_raise(view);
			}
		}
		return;
	case TWM_F_FOCUS:
		if (view) {
			view_focus(view);
		}
		return;
	case TWM_F_DELETE:
		if (view) {
			view->impl->close(view);
		}
		return;
	case TWM_F_ICONIFY: /* FRM-13; from an icon it deiconifies */
		if (view) {
			view_set_iconified(view, !view->iconified);
		}
		return;
	case TWM_F_SQUEEZE: /* FRM-14 */
		if (view) {
			view_set_squeezed(view, !view->squeezed);
		}
		return;
	case TWM_F_DESKTOP: /* DSK-1 */
		desktop_switch(server, func->num);
		return;
	case TWM_F_SEND:
		desktop_send(server, view, func->num);
		return;
	case TWM_F_MENU: /* MNU-1, MNU-6, MNU-7 */
		menu_open(server, func->arg, ctx->lx, ctx->ly, ctx->button != 0);
		return;
	case TWM_F_LOG: /* ROOT-8 */
		logview_toggle(server);
		return;
	case TWM_F_UNDO:
		histview_undo(server, view); /* HIS-2 */
		return;
	case TWM_F_REDO:
		histview_redo(server, view);
		return;
	case TWM_F_HISTORY:
		histview_toggle(server); /* HIS-3 */
		return;
	case TWM_F_HINTS:
	case TWM_F_NEXTTAB:
		not_yet(server, func->kind, "M5");
		return;
	case TWM_F_URGENT:
		not_yet(server, func->kind, "M6");
		return;
	}
}
