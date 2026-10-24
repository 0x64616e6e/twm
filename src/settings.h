#ifndef TWM_SETTINGS_H
#define TWM_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include "squeeze.h"
#include "twmrc.h"

/*
 * Typed configuration (CFG-3, DESIGN §5). Built from the built-in defaults,
 * then the user's .twmrc on top. Pure: no wlroots.
 */

/* Modifier bits; the same values as wlroots' enum wlr_keyboard_modifier. */
#define TWM_MOD_SHIFT 1u
#define TWM_MOD_CAPS 2u
#define TWM_MOD_CTRL 4u
#define TWM_MOD_ALT 8u
#define TWM_MOD_MOD2 16u
#define TWM_MOD_LOGO 64u
/* "M-" in a binding: whatever `Modifier` says, resolved when matching. */
#define TWM_MOD_M (1u << 16)

enum twm_func_kind {
	TWM_F_NOP,
	TWM_F_EXEC,      /* arg: shell command */
	TWM_F_TERMINAL,  /* runs `Terminal` */
	TWM_F_MENU,      /* arg: menu name */
	TWM_F_MOVE,
	TWM_F_RESIZE,
	TWM_F_RAISEMOVE, /* raise and focus; move once dragged */
	TWM_F_SQUEEZE,
	TWM_F_ICONIFY,
	TWM_F_RAISE,
	TWM_F_LOWER,
	TWM_F_RAISELOWER,
	TWM_F_FOCUS,
	TWM_F_DELETE,
	TWM_F_RELOAD,
	TWM_F_QUIT,
	TWM_F_HINTS,
	TWM_F_UNDO,
	TWM_F_REDO,
	TWM_F_URGENT,
	TWM_F_LOG,
	TWM_F_NEXTTAB,
	TWM_F_HISTORY,
	TWM_F_SETTINGS,
	TWM_F_DESTROY,   /* kill the client (FRM-19, twm's f.destroy) */
	TWM_F_RUN,       /* the Icon Manager, filtering: run an app (IMG-13) */
	TWM_F_FOLD,      /* a group's tabs folded into one (GRP-6) */
	TWM_F_VOLUME,    /* num: up, down, mute, mic (LVL-1) */
	TWM_F_BRIGHTNESS, /* num: up, down (LVL-1) */
	TWM_F_DESKTOP, /* num */
	TWM_F_SEND,    /* num */
	TWM_F_SEPARATOR,
};

struct twm_func {
	enum twm_func_kind kind;
	char *arg;
	int num;
};

enum twm_context {
	TWM_CTX_ROOT,
	TWM_CTX_TITLE,
	TWM_CTX_ICONIFY,
	TWM_CTX_RESIZE,
	TWM_CTX_CLOSE,  /* FRM-19: the close box */
	TWM_CTX_WINDOW,
	TWM_CTX_BORDER,
	TWM_CTX_ICON,
	TWM_CTX_ICONMGR,
};

struct twm_button_binding {
	uint32_t button; /* 1..5 */
	uint32_t mods;
	enum twm_context context;
	struct twm_func func;
	bool builtin;
};

struct twm_key_binding {
	uint32_t mods;
	xkb_keysym_t sym; /* lower case, unshifted key */
	char *spec;
	struct twm_func func;
	bool builtin;
};

struct twm_menu_item {
	char *label;
	struct twm_func func;
};

struct twm_menu {
	char *name;
	struct twm_menu_item *items;
	size_t nitems;
};

struct twm_squeeze_rule {
	char *pattern; /* fnmatch(3) against app id, then title */
	enum twm_justify justify;
};

struct twm_output_rule {
	char *name;
	double scale; /* 0 = unset */
	int width, height, refresh_mhz; /* 0 = unset */
	bool has_position;
	int x, y;
};

enum twm_focus_policy { TWM_FOCUS_SLOPPY, TWM_FOCUS_CLICK };
enum twm_placement { TWM_PLACE_INTERACTIVE, TWM_PLACE_CASCADE };
/* INP-*: the touchpad, through libinput */
enum twm_tp_scroll { TWM_TP_SCROLL_TWO_FINGER, TWM_TP_SCROLL_EDGE, TWM_TP_SCROLL_NONE };
enum twm_tp_click { TWM_TP_CLICK_DEFAULT, TWM_TP_CLICK_AREAS, TWM_TP_CLICK_FINGERS };
enum twm_tp_accel { TWM_TP_ACCEL_ADAPTIVE, TWM_TP_ACCEL_FLAT };
enum twm_corner { TWM_TOP_LEFT, TWM_TOP_RIGHT, TWM_BOTTOM_LEFT, TWM_BOTTOM_RIGHT };
enum twm_sort { TWM_SORT_NAME, TWM_SORT_STACK, TWM_SORT_AGE };

#define TWM_SNAP_EDGES 1u
#define TWM_SNAP_THIRDS 2u
#define TWM_SNAP_CENTER 4u

struct twm_config {
	/* Look */
	uint32_t accent;
	char font[96];
	int font_size;
	int title_height, border_width;
	bool title_highlight, root_weave, root_wordmark;
	double scale; /* 0 = auto */
	struct twm_output_rule *outputs;
	size_t noutputs;

	/* Frames */
	struct twm_squeeze_rule *squeeze;
	size_t nsqueeze;
	char **notitle;
	size_t nnotitle;

	/* Focus, moving, placing */
	enum twm_focus_policy focus_policy;
	int focus_delay;
	bool raise_on_click;
	bool opaque_move;
	enum twm_placement placement;
	bool remember_geometry;
	uint32_t snap;
	int snap_distance;

	/* Keys and buttons */
	uint32_t modifier; /* TWM_MOD_LOGO or TWM_MOD_ALT */
	char *terminal;
	char *hint_keys;
	struct twm_button_binding *buttons;
	size_t nbuttons;
	struct twm_key_binding *keys;
	size_t nkeys;
	bool no_default_bindings;
	struct twm_menu *menus;
	size_t nmenus;

	/* Desktops and the rest */
	int root_line_timeout;
	enum twm_corner iconmgr_corner;
	enum twm_sort iconmgr_sort;
	bool icons_on_root;
	int desktops;
	bool panner;
	uint32_t quiet; /* bit n-1 = desktop n */
	bool xwayland;
	bool notifications; /* M6: serve org.freedesktop.Notifications if free */
	bool close_box;     /* FRM-19 */
	int volume_step, brightness_step; /* LVL-1: percent a key press moves */

	/* INP-*: touchpad and touchscreen */
	bool tp_tap, tp_tap_drag, tp_natural_scroll, tp_dwt;
	enum twm_tp_scroll tp_scroll;
	enum twm_tp_click tp_click;
	double tp_speed; /* -1 … 1, libinput's pointer acceleration speed */
	enum twm_tp_accel tp_accel;
	bool touchscreen;
};

/*
 * Build a config: defaults, then `src` (may be NULL for defaults only).
 * Diagnostics go to *doc_out (caller frees with twmrc_free); the config is
 * returned even when there are errors, and must not be used then.
 */
struct twm_config *config_build(const char *src, size_t len, struct twmrc_doc **doc_out);
void config_free(struct twm_config *cfg);

/* The built-in defaults as .twmrc text. */
extern const char config_default_twmrc[];

/* Match the configured modifier into binding mods. */
uint32_t config_resolve_mods(const struct twm_config *cfg, uint32_t mods);
const struct twm_key_binding *config_match_key(const struct twm_config *cfg,
	uint32_t mods, xkb_keysym_t sym);
const struct twm_button_binding *config_match_button(const struct twm_config *cfg,
	uint32_t button, uint32_t mods, enum twm_context ctx);
const struct twm_menu *config_menu(const struct twm_config *cfg, const char *name);
/* The key bound to the same function, for menu shortcut columns (MNU-3). */
const struct twm_key_binding *config_key_for(const struct twm_config *cfg,
	const struct twm_func *func);
/* A button binding in `ctx` for the same function, or NULL (MNU-3). */
const struct twm_button_binding *config_button_for(const struct twm_config *cfg,
	const struct twm_func *func, enum twm_context ctx);
enum twm_justify config_justify(const struct twm_config *cfg, const char *app_id,
	const char *title);
bool config_notitle(const struct twm_config *cfg, const char *app_id, const char *title);

const char *config_func_name(enum twm_func_kind kind);

#endif
