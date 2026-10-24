#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>
#include "actions.h"
#include "setwin.h"
#include "desktop.h"
#include "grab.h"
#include "group.h"
#include "hints.h"
#include "histview.h"
#include "imgr.h"
#include "media.h"
#include "logview.h"
#include "menu.h"
#include "rc.h"
#include "rootline.h"
#include "view.h"

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
			} else if (ctx->tab && ctx->tab->group && ctx->button) {
				grab_begin_tab(server, ctx->tab, ctx->button); /* GRP-5 */
			} else {
				grab_begin_move(server, view, ctx->button);
			}
		}
		return;
	case TWM_F_RAISEMOVE:
		if (ctx->tab && ctx->tab->group && ctx->tab != view) {
			group_activate(ctx->tab); /* GRP-4 */
			view = ctx->tab;
		}
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
	case TWM_F_DESTROY: /* FRM-19: twm's f.destroy */
		if (view && view->impl->kill) {
			rootline_post(server, ROOTLINE_INFO, "twm", "killed %s", view_title(view));
			view->impl->kill(view);
		} else if (view) {
			view->impl->close(view);
		}
		return;
	case TWM_F_VOLUME: { /* LVL-1 */
		static const enum level_action a[] = {
			LEVEL_VOLUME_UP, LEVEL_VOLUME_DOWN, LEVEL_VOLUME_MUTE, LEVEL_MIC_MUTE,
		};
		media_level(server, a[func->num & 3]);
		return;
	}
	case TWM_F_BRIGHTNESS: /* LVL-1 */
		media_level(server, func->num ? LEVEL_DIMMER : LEVEL_BRIGHTER);
		return;
	case TWM_F_RUN: /* IMG-13 */
		imgr_run(server);
		return;
	case TWM_F_ICONIFY: /* FRM-13; from an icon it deiconifies */
		if (view) {
			view_set_iconified(view, !view->iconified);
		}
		return;
	case TWM_F_SQUEEZE: /* FRM-14; a group squeezes whole (GRP-9) */
		if (view && view->group && view->group->active) {
			/* the tab in front is the group's frame; the others take its
			 * state when they come to the front (group.c hand_over) */
			view_set_squeezed(view->group->active, !view->group->active->squeezed);
		} else if (view) {
			view_set_squeezed(view, !view->squeezed);
		}
		return;
	case TWM_F_FOLD: /* GRP-6: the tabs into the one in front, and out */
		if (view && view->group) {
			group_set_folded(view->group, !view->group->folded);
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
	case TWM_F_SETTINGS:
		setwin_toggle(server); /* SET-1 */
		return;
	case TWM_F_NEXTTAB: /* GRP-4 */
		group_cycle(view, 1);
		return;
	case TWM_F_HINTS: /* HNT-1 */
		hints_begin(server);
		return;
	case TWM_F_URGENT: /* URG-4 */
		urgent_jump(server);
		return;
	}
}
