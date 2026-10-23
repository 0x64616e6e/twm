#ifndef TWM_XDG_H
#define TWM_XDG_H

struct wlr_xdg_toplevel;
struct wlr_xdg_toplevel_decoration_v1;

/* Remember (or forget, with NULL) a toplevel's decoration object, so the
 * initial commit can answer it (FRM-15). */
void xdg_set_decoration(struct wlr_xdg_toplevel *toplevel,
	struct wlr_xdg_toplevel_decoration_v1 *deco);

#endif
