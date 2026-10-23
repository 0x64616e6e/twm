/* realpath(3) is XSI. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "actions.h"
#include "draw.h"
#include "internal.h"
#include "pixbuf.h"
#include "rc.h"
#include "rcedit.h"
#include "rootline.h"
#include "settings.h"
#include "setwin.h"
#include "text.h"
#include "view.h"

/*
 * B8 metrics at 26/30 (DESIGN measurement rule): the window body 1135 × 527,
 * nav 165, diff pane 364; rows 49, controls 29, buttons 38.
 */
#define SW_MIN_W 1135
#define SW_MIN_H 527
#define NAV_W 165
#define DIFF_W 364
#define NAV_ITEM 33
#define NAV_PAD 16
#define FILTER_H 29
#define PANE_PAD_X 24
#define PANE_PAD_Y 17
#define COL_LABEL 147
#define COL_KEY 113
#define COL_GAP 14
#define ROW_H 49
#define CTL_H 29
#define SEG_PAD 12
#define CHECK 14
#define CHECK_GAP 9
#define SWATCH 26
#define SWATCH_GAP 9
#define LIST_LINE 17
#define DIFF_HEAD 38
#define DIFF_LINE 19
#define DIFF_PAD 16
#define BTN_H 38
#define F_LABEL 12
#define F_CTL 11
#define F_SMALL 10

#define MAX_EDITS 64
#define MAX_UNDO 64
#define MAX_WRITES 16
#define MAX_HITS 256

enum section {
	SEC_LOOK, SEC_FRAMES, SEC_FOCUS, SEC_KEYS, SEC_DESKTOPS, SEC_OUTPUTS, SEC_PRIVACY,
	SEC_MENUS, SEC_COUNT,
};

static const char *const section_names[SEC_COUNT] = {
	"Look", "Frames", "Focus", "Keys & buttons", "Desktops", "Outputs", "Privacy", "Menus",
};

enum ctl_kind {
	CK_SEG,    /* segmented choice (SET-9) */
	CK_SLIDER, /* min…max */
	CK_CHECK,  /* on/off */
	CK_BITS,   /* several checkboxes in one value (Snap, Quiet) */
	CK_SWATCH, /* Accent presets and hex (SET-9) */
	CK_TEXT,   /* a string or word, typed */
	CK_INFO,   /* read-only words */
	CK_LIST,   /* read-only rows from the config (bindings, outputs, menus) */
	CK_BUTTON, /* "Edit ~/.twmrc by hand" */
};

enum list_kind { LIST_KEYS, LIST_BUTTONS, LIST_OUTPUTS, LIST_MENUS };

struct ctl;
typedef int (*get_fn)(const struct twm_config *c);
typedef void (*text_fn)(const struct twm_config *c, char *out, size_t len);
/* The value text to write for value `v` (or `text`), given the pending config. */
typedef void (*format_fn)(const struct twm_config *c, const struct ctl *ctl, int v,
	const char *text, char *out, size_t len);

struct ctl {
	enum section section;
	const char *label;
	const char *key;  /* the .twmrc key it writes (SET-3); NULL for none */
	const char *row;  /* a block row it writes (SqueezeTitle's "*"), or NULL */
	enum ctl_kind kind;
	const char *opts[9]; /* segment labels, checkbox labels, or a check's text */
	const char *words[9]; /* the words written for each option */
	int nopts;
	int min, max, step;
	const char *unit;
	get_fn get;
	text_fn get_text;
	format_fn format;
	enum list_kind list;
	int list_lines; /* rows shown before "+N more" */
};

struct edit {
	int ctl; /* the control that made it */
	char key[32];
	char row[16];
	char value[256];
};

struct edit_set {
	struct edit e[MAX_EDITS];
	int n;
};

enum hit_kind {
	HIT_NAV, HIT_FILTER, HIT_CTL, HIT_SLIDER, HIT_HEX, HIT_TEXT, HIT_DISCARD, HIT_WRITE,
	HIT_BUTTON,
};

struct hit {
	int x0, y0, x1, y1;
	enum hit_kind kind;
	int ctl, sub;
};

struct setwin {
	struct twm_server *server;
	struct twm_internal_view *iv;
	char *disk;   /* the file as on disk ("" when missing) */
	bool exists;
	struct edit_set edits;
	struct edit_set undo[MAX_UNDO], redo[MAX_UNDO];
	int nundo, nredo;
	struct wsave { char *text; bool existed; } wundo[MAX_WRITES], wredo[MAX_WRITES];
	int nwundo, nwredo;
	char *pending; /* disk with the edits applied */
	struct twm_config *cfg; /* from `pending`, or the last good one */
	bool check_ok;
	char check[160];
	struct rcdiff diff;
	char *diff_old; /* the texts diff lines point into */
	enum section section;
	char filter[64];
	int sel, sub; /* selected control (index into the table), sub-item */
	int editing;  /* control whose text is being typed, or -1 */
	char edit_buf[128];
	struct hit hits[MAX_HITS];
	int nhits;
};

/* --- the controls --- */

#define SENTINEL "\x01twm-settings\x01"

static int get_squeeze(const struct twm_config *c) {
	return (int)config_justify(c, SENTINEL, SENTINEL);
}
static int get_title_height(const struct twm_config *c) { return c->title_height; }
static int get_highlight(const struct twm_config *c) { return c->title_highlight; }
static int get_opaque(const struct twm_config *c) { return c->opaque_move; }
static int get_snap(const struct twm_config *c) { return (int)c->snap; }
static int get_accent(const struct twm_config *c) { return (int)c->accent; }
static int get_font_size(const struct twm_config *c) { return c->font_size; }
static int get_border(const struct twm_config *c) { return c->border_width; }
static int get_weave(const struct twm_config *c) { return c->root_weave; }
static int get_wordmark(const struct twm_config *c) { return c->root_wordmark; }
static int get_rl_timeout(const struct twm_config *c) { return c->root_line_timeout; }
static int get_focus_policy(const struct twm_config *c) { return c->focus_policy; }
static int get_focus_delay(const struct twm_config *c) { return c->focus_delay; }
static int get_raise(const struct twm_config *c) { return c->raise_on_click; }
static int get_placement(const struct twm_config *c) { return c->placement; }
static int get_remember(const struct twm_config *c) { return c->remember_geometry; }
static int get_snap_distance(const struct twm_config *c) { return c->snap_distance; }
static int get_modifier(const struct twm_config *c) { return c->modifier == TWM_MOD_ALT; }
static int get_desktops(const struct twm_config *c) { return c->desktops; }
static int get_panner(const struct twm_config *c) { return c->panner; }
/* Corners as two choices: top/bottom, left/right (enum twm_corner order). */
static int get_im_edge(const struct twm_config *c) {
	return c->iconmgr_corner == TWM_BOTTOM_LEFT || c->iconmgr_corner == TWM_BOTTOM_RIGHT;
}
static int get_im_side(const struct twm_config *c) {
	return c->iconmgr_corner == TWM_TOP_RIGHT || c->iconmgr_corner == TWM_BOTTOM_RIGHT;
}
static int get_im_sort(const struct twm_config *c) { return c->iconmgr_sort; }
static int get_icons_root(const struct twm_config *c) { return c->icons_on_root; }
static int get_quiet(const struct twm_config *c) { return (int)c->quiet; }
static int get_xwayland(const struct twm_config *c) { return c->xwayland; }
static int get_notifications(const struct twm_config *c) { return c->notifications; }

/* A string as .twmrc writes it: quoted, \" and \\ escaped. */
static void quote(const char *s, char *out, size_t len) {
	size_t o = 0;
	if (len < 3) {
		return;
	}
	out[o++] = '"';
	for (; *s && o + 3 < len; s++) {
		if (*s == '"' || *s == '\\') {
			out[o++] = '\\';
		}
		out[o++] = *s;
	}
	out[o++] = '"';
	out[o] = '\0';
}

static void text_font(const struct twm_config *c, char *out, size_t len) {
	snprintf(out, len, "%s", c->font);
}
static void text_terminal(const struct twm_config *c, char *out, size_t len) {
	snprintf(out, len, "%s", c->terminal ? c->terminal : "");
}
static void text_hint_keys(const struct twm_config *c, char *out, size_t len) {
	snprintf(out, len, "%s", c->hint_keys ? c->hint_keys : "");
}
static void text_scale(const struct twm_config *c, char *out, size_t len) {
	if (c->scale > 0) {
		snprintf(out, len, "%g", c->scale);
	} else {
		snprintf(out, len, "auto");
	}
}
static void text_effects(const struct twm_config *c, char *out, size_t len) {
	snprintf(out, len, "none \xc2\xb7 no animation over 80 ms");
}
static void text_capture(const struct twm_config *c, char *out, size_t len) {
	snprintf(out, len, "always shown; no setting hides them");
}
static void text_prompts(const struct twm_config *c, char *out, size_t len) {
	snprintf(out, len, "synthetic input never reaches them");
}

static void fmt_word(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	snprintf(out, len, "%s", v >= 0 && v < ctl->nopts ? ctl->words[v] : "");
}
static void fmt_number(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	snprintf(out, len, "%d", v);
}
static void fmt_onoff(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	snprintf(out, len, "%s", v ? "on" : "off");
}
static void fmt_bits(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	size_t o = (size_t)snprintf(out, len, "{");
	for (int i = 0; i < ctl->nopts && o < len; i++) {
		if (v & (1 << i)) {
			o += (size_t)snprintf(out + o, len - o, " %s", ctl->words[i]);
		}
	}
	snprintf(out + o, len - o, " }");
}
static void fmt_snap(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	if (v == 0) {
		snprintf(out, len, "off");
	} else {
		fmt_bits(c, ctl, v, text, out, len);
	}
}
static void fmt_quiet(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	size_t o = (size_t)snprintf(out, len, "{");
	for (int i = 0; i < 9 && o < len; i++) {
		if (v & (1 << i)) {
			o += (size_t)snprintf(out + o, len - o, " %d", i + 1);
		}
	}
	snprintf(out + o, len - o, " }");
}
static void fmt_accent(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	if (text) {
		snprintf(out, len, "%s", text);
	} else {
		snprintf(out, len, "#%06X", (unsigned)v & 0xFFFFFF);
	}
}
static void fmt_font_family(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	char q[128];
	quote(text ? text : c->font, q, sizeof(q));
	snprintf(out, len, "%s %d", q, c->font_size);
}
static void fmt_font_size(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	char q[128];
	quote(c->font, q, sizeof(q));
	snprintf(out, len, "%s %d", q, v);
}
static void fmt_quoted(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	quote(text ? text : "", out, len);
}
static void fmt_raw(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	snprintf(out, len, "%s", text && *text ? text : "auto");
}
static void fmt_panner(const struct twm_config *c, int desktops, bool shown,
		char *out, size_t len) {
	snprintf(out, len, "{ desktops %d%s }", desktops, shown ? "" : " off");
}
static void fmt_desktops(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	fmt_panner(c, v, c->panner, out, len);
}
static void fmt_panner_shown(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	fmt_panner(c, c->desktops, v != 0, out, len);
}
static const char *const corner_words[] = {"top-left", "top-right", "bottom-left", "bottom-right"};
static const char *const sort_words[] = {"name", "stack", "age"};
static void fmt_im(const struct twm_config *c, bool bottom, bool right, char *out, size_t len) {
	snprintf(out, len, "{ at %s-%s sort %s }", bottom ? "bottom" : "top",
		right ? "right" : "left", sort_words[c->iconmgr_sort % 3]);
}
static void fmt_im_edge(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	fmt_im(c, v != 0, get_im_side(c), out, len);
}
static void fmt_im_side(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	fmt_im(c, get_im_edge(c), v != 0, out, len);
}
static void fmt_im_sort(const struct twm_config *c, const struct ctl *ctl, int v,
		const char *text, char *out, size_t len) {
	snprintf(out, len, "{ at %s sort %s }", corner_words[c->iconmgr_corner & 3],
		sort_words[v % 3]);
}

static const struct ctl ctls[] = {
	/* Look */
	{SEC_LOOK, "Font", "Font", NULL, CK_TEXT, .get_text = text_font, .format = fmt_font_family},
	{SEC_LOOK, "Font size", "Font", NULL, CK_SLIDER, .min = 9, .max = 20, .step = 1, .unit = "px",
		.get = get_font_size, .format = fmt_font_size},
	{SEC_LOOK, "Border", "BorderWidth", NULL, CK_SLIDER, .min = 1, .max = 4, .step = 1,
		.unit = "px", .get = get_border, .format = fmt_number},
	{SEC_LOOK, "Root weave", "RootWeave", NULL, CK_CHECK, {"dots on the root"}, .nopts = 1,
		.get = get_weave, .format = fmt_onoff},
	{SEC_LOOK, "Wordmark", "RootWordmark", NULL, CK_CHECK, {"Tom's WM on the root"},
		.nopts = 1, .get = get_wordmark, .format = fmt_onoff},
	{SEC_LOOK, "Root line", "RootLineTimeout", NULL, CK_SLIDER, .min = 5, .max = 300,
		.step = 5, .unit = "s", .get = get_rl_timeout, .format = fmt_number},
	{SEC_LOOK, "Effects", "Effects", NULL, CK_INFO, .get_text = text_effects},
	/* Frames: B8's rows, in B8's order (SET-9) */
	{SEC_FRAMES, "Squeeze titles", "SqueezeTitle", "\"*\"", CK_SEG,
		{"left", "center", "right", "off"}, {"left", "center", "right", "off"}, 4,
		.get = get_squeeze, .format = fmt_word},
	{SEC_FRAMES, "Title height", "TitleHeight", NULL, CK_SLIDER, .min = 22, .max = 34,
		.step = 1, .unit = "px", .get = get_title_height, .format = fmt_number},
	{SEC_FRAMES, "Highlight hatch", "TitleHighlight", NULL, CK_CHECK, {"on the focused tab"},
		.nopts = 1, .get = get_highlight, .format = fmt_onoff},
	{SEC_FRAMES, "Move and resize", "OpaqueMove", NULL, CK_SEG, {"outline", "opaque"},
		{"off", "on"}, 2, .get = get_opaque, .format = fmt_word},
	{SEC_FRAMES, "Snap to", "Snap", NULL, CK_BITS,
		{"neighbouring edges", "screen thirds", "output centre"},
		{"edges", "thirds", "center"}, 3, .get = get_snap, .format = fmt_snap},
	{SEC_FRAMES, "Accent", "Accent", NULL, CK_SWATCH, .get = get_accent, .format = fmt_accent},
	/* Focus */
	{SEC_FOCUS, "Focus", "FocusPolicy", NULL, CK_SEG, {"sloppy", "click"}, {"sloppy", "click"},
		2, .get = get_focus_policy, .format = fmt_word},
	{SEC_FOCUS, "Focus delay", "FocusDelay", NULL, CK_SLIDER, .min = 0, .max = 500, .step = 10,
		.unit = "ms", .get = get_focus_delay, .format = fmt_number},
	{SEC_FOCUS, "Raise on click", "RaiseOnClick", NULL, CK_CHECK, {"a click raises the window"},
		.nopts = 1, .get = get_raise, .format = fmt_onoff},
	{SEC_FOCUS, "New windows", "Placement", NULL, CK_SEG, {"click to place", "cascade"},
		{"interactive", "cascade"}, 2, .get = get_placement, .format = fmt_word},
	{SEC_FOCUS, "Remember places", "RememberGeometry", NULL, CK_CHECK, {"per app id"},
		.nopts = 1, .get = get_remember, .format = fmt_onoff},
	{SEC_FOCUS, "Snap distance", "SnapDistance", NULL, CK_SLIDER, .min = 0, .max = 32,
		.step = 1, .unit = "px", .get = get_snap_distance, .format = fmt_number},
	{SEC_FOCUS, "Modifier", "Modifier", NULL, CK_SEG, {"logo", "alt"}, {"Mod4", "Alt"}, 2,
		.get = get_modifier, .format = fmt_word},
	/* Keys & buttons */
	{SEC_KEYS, "Terminal", "Terminal", NULL, CK_TEXT, .get_text = text_terminal,
		.format = fmt_quoted},
	{SEC_KEYS, "Hint keys", "HintKeys", NULL, CK_TEXT, .get_text = text_hint_keys,
		.format = fmt_quoted},
	{SEC_KEYS, "Keys", NULL, NULL, CK_LIST, .list = LIST_KEYS, .list_lines = 7},
	{SEC_KEYS, "Buttons", NULL, NULL, CK_LIST, .list = LIST_BUTTONS, .list_lines = 4},
	{SEC_KEYS, "Bindings", NULL, NULL, CK_BUTTON, {"Edit .twmrc by hand"}},
	/* Desktops */
	{SEC_DESKTOPS, "Desktops", "Panner", NULL, CK_SLIDER, .min = 1, .max = 9, .step = 1,
		.get = get_desktops, .format = fmt_desktops},
	{SEC_DESKTOPS, "Panner", "Panner", NULL, CK_CHECK, {"show the Panner"}, .nopts = 1,
		.get = get_panner, .format = fmt_panner_shown},
	{SEC_DESKTOPS, "Icon Manager", "IconManager", NULL, CK_SEG, {"top", "bottom"},
		{"top", "bottom"}, 2, .get = get_im_edge, .format = fmt_im_edge},
	{SEC_DESKTOPS, "", "IconManager", NULL, CK_SEG, {"left", "right"}, {"left", "right"}, 2,
		.get = get_im_side, .format = fmt_im_side},
	{SEC_DESKTOPS, "Sort by", "IconManager", NULL, CK_SEG, {"name", "stack", "age"},
		{"name", "stack", "age"}, 3, .get = get_im_sort, .format = fmt_im_sort},
	{SEC_DESKTOPS, "Icons", "IconsOnRoot", NULL, CK_CHECK, {"iconified windows on the root"},
		.nopts = 1, .get = get_icons_root, .format = fmt_onoff},
	{SEC_DESKTOPS, "Quiet desktops", "Quiet", NULL, CK_BITS,
		{"1", "2", "3", "4", "5", "6", "7", "8", "9"},
		{"1", "2", "3", "4", "5", "6", "7", "8", "9"}, 9,
		.get = get_quiet, .format = fmt_quiet},
	/* Outputs */
	{SEC_OUTPUTS, "Scale", "Scale", NULL, CK_TEXT, .get_text = text_scale, .format = fmt_raw},
	{SEC_OUTPUTS, "XWayland", "XWayland", NULL, CK_CHECK, {"X11 apps (at the next start)"},
		.nopts = 1, .get = get_xwayland, .format = fmt_onoff},
	{SEC_OUTPUTS, "Outputs", NULL, NULL, CK_LIST, .list = LIST_OUTPUTS, .list_lines = 6},
	{SEC_OUTPUTS, "Output rules", NULL, NULL, CK_BUTTON, {"Edit .twmrc by hand"}},
	/* Privacy */
	{SEC_PRIVACY, "Notifications", "Notifications", NULL, CK_CHECK,
		{"as root lines, if no other daemon runs"}, .nopts = 1, .get = get_notifications,
		.format = fmt_onoff},
	{SEC_PRIVACY, "Capture markers", NULL, NULL, CK_INFO, .get_text = text_capture},
	{SEC_PRIVACY, "Prompts", NULL, NULL, CK_INFO, .get_text = text_prompts},
	/* Menus */
	{SEC_MENUS, "Menus", NULL, NULL, CK_LIST, .list = LIST_MENUS, .list_lines = 16},
	{SEC_MENUS, "Menu entries", NULL, NULL, CK_BUTTON, {"Edit .twmrc by hand"}},
};

#define NCTLS ((int)(sizeof(ctls) / sizeof(ctls[0])))

static const uint32_t accent_presets[] = {0xFFB000, 0x7FD1AE, 0x8AB4FF, 0xFF8A65};

static struct setwin *sw_of(struct twm_server *server) {
	return server->setwin ? server->setwin->data : NULL;
}

/* How many options a control has right now (Quiet follows Desktops). */
static int nopts_of(struct setwin *sw, const struct ctl *c) {
	if (c->format == fmt_quiet) {
		return sw->cfg->desktops < 9 ? sw->cfg->desktops : 9;
	}
	return c->nopts;
}

/* --- the model: the file, the pending edits, the diff --- */

static char *apply_edits(const char *disk, const struct edit_set *set, int skip) {
	char *text = strdup(disk);
	for (int i = 0; i < set->n; i++) {
		if (i == skip) {
			continue;
		}
		const struct edit *e = &set->e[i];
		char *next = e->row[0] ? rcedit_set_row(text, e->key, e->row, e->value) :
			rcedit_set(text, e->key, e->value);
		free(text);
		text = next;
	}
	return text;
}

static bool facet_equal(const struct ctl *c, const struct twm_config *a,
		const struct twm_config *b) {
	if (c->get) {
		return c->get(a) == c->get(b);
	}
	if (c->get_text) {
		char x[256], y[256];
		c->get_text(a, x, sizeof(x));
		c->get_text(b, y, sizeof(y));
		return strcmp(x, y) == 0;
	}
	return true;
}

/* An edit that changes nothing goes: same text, or a key the file doesn't
 * have set to what it already is. */
static void drop_noops(struct setwin *sw) {
	for (int i = sw->edits.n - 1; i >= 0; i--) {
		char *with = apply_edits(sw->disk, &sw->edits, -1);
		char *without = apply_edits(sw->disk, &sw->edits, i);
		bool drop = strcmp(with, without) == 0;
		char *had = drop ? NULL : rcedit_get(without, sw->edits.e[i].key);
		if (!drop && !had) {
			struct twmrc_doc *d1, *d2;
			struct twm_config *c1 = config_build(with, strlen(with), &d1);
			struct twm_config *c2 = config_build(without, strlen(without), &d2);
			drop = !twmrc_count(d1, TWMRC_ERROR) && !twmrc_count(d2, TWMRC_ERROR) &&
				facet_equal(&ctls[sw->edits.e[i].ctl], c1, c2);
			config_free(c1);
			config_free(c2);
			twmrc_free(d1);
			twmrc_free(d2);
		}
		free(had);
		free(with);
		free(without);
		if (drop) {
			memmove(&sw->edits.e[i], &sw->edits.e[i + 1],
				sizeof(sw->edits.e[0]) * (size_t)(sw->edits.n - i - 1));
			sw->edits.n--;
		}
	}
}

static void recompute(struct setwin *sw) {
	drop_noops(sw);
	free(sw->pending);
	sw->pending = apply_edits(sw->disk, &sw->edits, -1);
	struct twmrc_doc *doc;
	struct twm_config *cfg = config_build(sw->pending, strlen(sw->pending), &doc);
	const struct twmrc_diag *err = twmrc_first_error(doc);
	if (err) {
		sw->check_ok = false;
		snprintf(sw->check, sizeof(sw->check), "line %d: %s", err->line, err->message);
		config_free(cfg);
	} else {
		size_t w = twmrc_count(doc, TWMRC_WARNING);
		sw->check_ok = true;
		snprintf(sw->check, sizeof(sw->check), "ok \xc2\xb7 %zu warning%s", w, w == 1 ? "" : "s");
		config_free(sw->cfg);
		sw->cfg = cfg;
	}
	twmrc_free(doc);
	rcdiff_free(&sw->diff);
	free(sw->diff_old);
	sw->diff_old = strdup(sw->disk);
	rcdiff_compute(&sw->diff, sw->diff_old, sw->pending, 2);
}

static void read_disk(struct setwin *sw) {
	char *src = NULL;
	size_t len;
	bool missing = false;
	if (rc_read(sw->server->config_path, &src, &len, &missing)) {
		free(sw->disk);
		sw->disk = src;
		sw->exists = true;
	} else if (missing) {
		free(sw->disk);
		sw->disk = strdup("");
		sw->exists = false;
	}
}

static void snapshot(struct setwin *sw) {
	if (sw->nundo == MAX_UNDO) {
		memmove(&sw->undo[0], &sw->undo[1], sizeof(sw->undo[0]) * (MAX_UNDO - 1));
		sw->nundo--;
	}
	sw->undo[sw->nundo++] = sw->edits;
	sw->nredo = 0;
}

static void put_edit(struct setwin *sw, int ctl, const char *value) {
	const struct ctl *c = &ctls[ctl];
	snapshot(sw);
	struct edit *e = NULL;
	for (int i = 0; i < sw->edits.n; i++) {
		if (strcmp(sw->edits.e[i].key, c->key) == 0 &&
				strcmp(sw->edits.e[i].row, c->row ? c->row : "") == 0) {
			e = &sw->edits.e[i];
		}
	}
	if (!e && sw->edits.n < MAX_EDITS) {
		e = &sw->edits.e[sw->edits.n++];
	}
	if (!e) {
		return;
	}
	e->ctl = ctl;
	snprintf(e->key, sizeof(e->key), "%s", c->key);
	snprintf(e->row, sizeof(e->row), "%s", c->row ? c->row : "");
	snprintf(e->value, sizeof(e->value), "%s", value);
	recompute(sw);
}

static void set_int(struct setwin *sw, int ctl, int v) {
	char value[256];
	ctls[ctl].format(sw->cfg, &ctls[ctl], v, NULL, value, sizeof(value));
	put_edit(sw, ctl, value);
}

static void set_text(struct setwin *sw, int ctl, const char *text) {
	char value[256];
	ctls[ctl].format(sw->cfg, &ctls[ctl], 0, text, value, sizeof(value));
	put_edit(sw, ctl, value);
}

static bool key_changed(struct setwin *sw, const struct ctl *c) {
	for (int i = 0; c->key && i < sw->edits.n; i++) {
		if (strcmp(sw->edits.e[i].key, c->key) == 0) {
			return true;
		}
	}
	return false;
}

/* --- writing (SET-6) --- */

/* Atomically: a temporary file beside the real one (through a symlink, so a
 * dotfile manager's link stays a link), then rename. */
static bool write_atomic(const char *path, const char *text, char *err, size_t errlen) {
	char target[PATH_MAX];
	if (!realpath(path, target)) {
		snprintf(target, sizeof(target), "%s", path);
	}
	char tmp[PATH_MAX + 16];
	snprintf(tmp, sizeof(tmp), "%s.XXXXXX", target);
	int fd = mkstemp(tmp);
	if (fd < 0) {
		snprintf(err, errlen, "%s", strerror(errno));
		return false;
	}
	struct stat st;
	fchmod(fd, stat(target, &st) == 0 ? (st.st_mode & 07777) : 0644);
	size_t len = strlen(text), off = 0;
	while (off < len) {
		ssize_t w = write(fd, text + off, len - off);
		if (w < 0 && errno == EINTR) {
			continue;
		}
		if (w <= 0) {
			snprintf(err, errlen, "%s", strerror(errno));
			close(fd);
			unlink(tmp);
			return false;
		}
		off += (size_t)w;
	}
	if (fsync(fd) < 0 || close(fd) < 0 || rename(tmp, target) < 0) {
		snprintf(err, errlen, "%s", strerror(errno));
		unlink(tmp);
		return false;
	}
	return true;
}

static void push_write(struct setwin *sw, bool redo, char *text, bool existed) {
	int *n = redo ? &sw->nwredo : &sw->nwundo;
	struct wsave *stack = redo ? sw->wredo : sw->wundo;
	if (*n == MAX_WRITES) {
		free(stack[0].text);
		memmove(&stack[0], &stack[1], sizeof(stack[0]) * (MAX_WRITES - 1));
		(*n)--;
	}
	stack[*n].text = text;
	stack[*n].existed = existed;
	(*n)++;
}

/* Put `text` on disk (or remove the file when !exists) and apply it. */
static bool replace_file(struct setwin *sw, const char *text, bool exists) {
	char err[160] = "";
	bool ok;
	if (exists) {
		ok = write_atomic(sw->server->config_path, text, err, sizeof(err));
	} else {
		ok = unlink(sw->server->config_path) == 0 || errno == ENOENT;
		if (!ok) {
			snprintf(err, sizeof(err), "%s", strerror(errno));
		}
	}
	if (!ok) {
		rootline_post(sw->server, ROOTLINE_ERROR, "settings", "can't write %s: %s",
			sw->server->config_path, err);
		return false;
	}
	free(sw->disk);
	sw->disk = strdup(text);
	sw->exists = exists;
	/* SET-6: saving is applying; don't wait for the watcher. */
	rc_reload(sw->server, false, NULL, 0);
	return true;
}

static void do_write(struct setwin *sw) {
	if (sw->edits.n == 0 || !sw->check_ok) {
		return;
	}
	char *old = strdup(sw->disk);
	bool existed = sw->exists;
	char *text = strdup(sw->pending);
	if (replace_file(sw, text, true)) {
		push_write(sw, false, old, existed);
		for (int i = 0; i < sw->nwredo; i++) {
			free(sw->wredo[i].text);
		}
		sw->nwredo = 0;
		sw->edits.n = 0;
		sw->nundo = sw->nredo = 0;
	} else {
		free(old);
	}
	free(text);
	recompute(sw);
}

static void do_discard(struct setwin *sw) {
	if (sw->edits.n) {
		snapshot(sw);
		sw->edits.n = 0;
		recompute(sw);
	}
}

/* HIS-5: M-z undoes the last pending change, then the last write. */
static void do_undo(struct setwin *sw, bool redo) {
	if (!redo && sw->nundo > 0) {
		sw->redo[sw->nredo++] = sw->edits;
		sw->edits = sw->undo[--sw->nundo];
	} else if (redo && sw->nredo > 0) {
		sw->undo[sw->nundo++] = sw->edits;
		sw->edits = sw->redo[--sw->nredo];
	} else if (sw->edits.n == 0 && (redo ? sw->nwredo : sw->nwundo) > 0) {
		struct wsave *stack = redo ? sw->wredo : sw->wundo;
		int *n = redo ? &sw->nwredo : &sw->nwundo;
		char *text = stack[*n - 1].text;
		bool existed = stack[*n - 1].existed;
		char *cur = strdup(sw->disk);
		bool cur_existed = sw->exists;
		if (replace_file(sw, text, existed)) {
			(*n)--;
			free(text);
			push_write(sw, !redo, cur, cur_existed);
			rootline_post(sw->server, ROOTLINE_INFO, "settings", "%s the last write",
				redo ? "redid" : "undid");
		} else {
			free(cur);
		}
	} else {
		return;
	}
	recompute(sw);
}

/* --- drawing --- */

struct cv {
	int w, h; /* the body, logical px */
	pixman_image_t *img;
	float scale;
	const struct twm_theme *t;
	const char *family;
};

static int S(const struct cv *cv, double v) {
	return (int)lround(v * cv->scale);
}

static uint32_t C(uint32_t rgb) {
	return rgba_argb32(rgba_hex(rgb, 1));
}

static uint32_t wash(uint32_t rgb, float a, uint32_t under) {
	return rgba_argb32(rgba_over(rgba_hex(rgb, a), rgba_hex(under, 1)));
}

static void fill(const struct cv *cv, uint32_t argb, double x, double y, double w, double h) {
	draw_fill(cv->img, argb, S(cv, x), S(cv, y), S(cv, x + w) - S(cv, x), S(cv, y + h) - S(cv, y));
}

static void frame_box(const struct cv *cv, uint32_t argb, double x, double y, double w, double h) {
	draw_box(cv->img, argb, S(cv, x), S(cv, y), S(cv, x + w) - S(cv, x),
		S(cv, y + h) - S(cv, y), draw_stroke(cv->scale));
}

static struct twm_font *font(const struct cv *cv, int size, int weight) {
	return font_get(cv->family, size, weight, cv->scale);
}

/* Text vertically centred in the band [y, y+h); returns its logical width. */
static double text(const struct cv *cv, int size, int weight, uint32_t argb, double x,
		double y, double h, const char *s, double maxw, float tracking) {
	struct twm_font *f = font(cv, size, weight);
	int w = text_draw(cv->img, f, s, S(cv, x), text_baseline_centered(f, S(cv, y), S(cv, h)),
		argb, tracking * cv->scale, maxw > 0 ? S(cv, maxw) : 0);
	return w / cv->scale;
}

static double text_w(const struct cv *cv, int size, int weight, const char *s, float tracking) {
	return text_width(font(cv, size, weight), s, tracking * cv->scale) / cv->scale;
}

static void add_hit(struct setwin *sw, double x, double y, double w, double h,
		enum hit_kind kind, int ctl, int sub) {
	if (sw->nhits < MAX_HITS) {
		sw->hits[sw->nhits++] = (struct hit){(int)x, (int)y, (int)(x + w), (int)(y + h),
			kind, ctl, sub};
	}
}

static void checkbox(const struct cv *cv, double x, double y, bool on, bool focus) {
	const struct twm_theme *t = cv->t;
	if (on) {
		fill(cv, C(t->accent), x, y, CHECK, CHECK);
		fill(cv, C(t->on_accent), x + 5, y + 5, CHECK - 10, CHECK - 10);
	} else {
		frame_box(cv, C(focus ? t->accent : t->hairline_strong), x, y, CHECK, CHECK);
	}
}

/* --- the nav (SET-2) --- */

static void draw_nav(struct setwin *sw, const struct cv *cv) {
	const struct twm_theme *t = cv->t;
	fill(cv, C(t->hairline), NAV_W - 1, 0, 1, cv->h);
	double fx = 10, fy = 12, fw = NAV_W - 20;
	fill(cv, C(t->ground), fx, fy, fw, FILTER_H);
	frame_box(cv, C(sw->filter[0] ? t->accent : t->hairline_strong), fx, fy, fw, FILTER_H);
	if (sw->filter[0]) {
		double w = text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->ink), fx + 9, fy, FILTER_H,
			sw->filter, fw - 20, 0);
		fill(cv, C(t->accent), fx + 9 + w + 1, fy + 7, 1, FILTER_H - 14);
	} else {
		text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->dim), fx + 9, fy, FILTER_H,
			"filter keys\xe2\x80\xa6", fw - 20, 0);
	}
	add_hit(sw, fx, fy, fw, FILTER_H, HIT_FILTER, -1, 0);
	double y = fy + FILTER_H + 10;
	for (int s = 0; s < SEC_COUNT; s++) {
		bool cur = !sw->filter[0] && (int)sw->section == s;
		if (cur) {
			fill(cv, wash(t->accent, 0.10f, t->surface), 0, y, NAV_W - 1, NAV_ITEM);
			fill(cv, C(t->accent), 0, y, 3, NAV_ITEM);
		}
		text(cv, F_LABEL, cur ? TWM_WEIGHT_SEMIBOLD : TWM_WEIGHT_REGULAR,
			C(cur ? t->ink : t->ink_2), NAV_PAD, y, NAV_ITEM, section_names[s], 0, 0);
		add_hit(sw, 0, y, NAV_W - 1, NAV_ITEM, HIT_NAV, -1, s);
		y += NAV_ITEM;
	}
	text(cv, F_SMALL, TWM_WEIGHT_REGULAR, C(t->dim), NAV_PAD, cv->h - 12 - 30, 15,
		"Everything here is also", 0, 0);
	text(cv, F_SMALL, TWM_WEIGHT_REGULAR, C(t->dim), NAV_PAD, cv->h - 12 - 15, 15,
		"man 5 twmrc.", 0, 0);
}

/* --- the controls (SET-3, SET-9) --- */

static bool matches(const char *hay, const char *needle) {
	if (!hay) {
		return false;
	}
	size_t n = strlen(needle);
	for (; *hay; hay++) {
		if (strncasecmp(hay, needle, n) == 0) {
			return true;
		}
	}
	return n == 0;
}

static int visible(struct setwin *sw, int *out) {
	int n = 0;
	for (int i = 0; i < NCTLS; i++) {
		const struct ctl *c = &ctls[i];
		bool show = sw->filter[0] ? matches(c->label, sw->filter) ||
			matches(c->key, sw->filter) || matches(section_names[c->section], sw->filter) :
			c->section == sw->section;
		if (show) {
			out[n++] = i;
		}
	}
	return n;
}

static double row_height(struct setwin *sw, const struct ctl *c) {
	switch (c->kind) {
	case CK_BITS:
		if (c->format == fmt_quiet) {
			return ROW_H;
		}
		return 2 * 14 + c->nopts * CHECK + (c->nopts - 1) * CHECK_GAP;
	case CK_LIST:
		return 2 * 12 + (c->list_lines + 1) * LIST_LINE;
	default:
		return ROW_H;
	}
}

static void func_text(const struct twm_func *f, char *out, size_t len) {
	if (f->kind == TWM_F_EXEC) {
		snprintf(out, len, "!\"%s\"", f->arg ? f->arg : "");
	} else if (f->arg) {
		snprintf(out, len, "%s \"%s\"", config_func_name(f->kind), f->arg);
	} else if (f->kind == TWM_F_DESKTOP || f->kind == TWM_F_SEND) {
		snprintf(out, len, "%s %d", config_func_name(f->kind), f->num);
	} else {
		snprintf(out, len, "%s", config_func_name(f->kind));
	}
}

static const char *const context_names[] = {
	"root", "title", "iconify", "resize", "window", "border", "icon", "iconmgr",
};

/* Row `i` of a list control: left and right text; false past the end. */
static bool list_row(struct setwin *sw, const struct ctl *c, size_t i, char *l, size_t ll,
		char *r, size_t rl, bool *builtin) {
	const struct twm_config *cfg = sw->cfg;
	*builtin = false;
	switch (c->list) {
	case LIST_KEYS:
		if (i >= cfg->nkeys) {
			return false;
		}
		snprintf(l, ll, "\"%s\"", cfg->keys[i].spec ? cfg->keys[i].spec : "?");
		func_text(&cfg->keys[i].func, r, rl);
		*builtin = cfg->keys[i].builtin;
		return true;
	case LIST_BUTTONS:
		if (i >= cfg->nbuttons) {
			return false;
		}
		snprintf(l, ll, "Button%u = %s%s", cfg->buttons[i].button,
			cfg->buttons[i].mods ? "M : " : "", context_names[cfg->buttons[i].context & 7]);
		func_text(&cfg->buttons[i].func, r, rl);
		*builtin = cfg->buttons[i].builtin;
		return true;
	case LIST_OUTPUTS:
		if (i >= cfg->noutputs) {
			if (i == 0) {
				snprintf(l, ll, "no Output rules: every output at Scale");
				r[0] = '\0';
				*builtin = true;
				return true;
			}
			return false;
		} else {
			const struct twm_output_rule *o = &cfg->outputs[i];
			snprintf(l, ll, "\"%s\"", o->name);
			size_t n = 0;
			r[0] = '\0';
			if (o->scale > 0) {
				n += (size_t)snprintf(r + n, rl - n, "scale %g ", o->scale);
			}
			if (o->width && n < rl) {
				n += (size_t)snprintf(r + n, rl - n, "mode %dx%d ", o->width, o->height);
			}
			if (o->has_position && n < rl) {
				snprintf(r + n, rl - n, "position %d,%d", o->x, o->y);
			}
			return true;
		}
	case LIST_MENUS: {
		size_t k = 0;
		for (size_t m = 0; m < cfg->nmenus; m++) {
			if (k++ == i) {
				snprintf(l, ll, "Menu \"%s\"", cfg->menus[m].name);
				snprintf(r, rl, "%zu entries", cfg->menus[m].nitems);
				return true;
			}
			for (size_t j = 0; j < cfg->menus[m].nitems; j++) {
				if (k++ == i) {
					const struct twm_menu_item *it = &cfg->menus[m].items[j];
					if (it->func.kind == TWM_F_SEPARATOR) {
						snprintf(l, ll, "  \xe2\x80\x94");
						r[0] = '\0';
					} else {
						snprintf(l, ll, "  \"%s\"", it->label);
						func_text(&it->func, r, rl);
					}
					*builtin = true;
					return true;
				}
			}
		}
		return false;
	}
	}
	return false;
}

static void draw_control(struct setwin *sw, const struct cv *cv, int idx, double x, double y,
		double w, double h) {
	const struct ctl *c = &ctls[idx];
	const struct twm_theme *t = cv->t;
	bool focus = sw->sel == idx;
	double cy = y + (h - CTL_H) / 2;
	switch (c->kind) {
	case CK_SEG: {
		int v = c->get(sw->cfg);
		double bx = x;
		for (int i = 0; i < c->nopts; i++) {
			bool on = v == i;
			double bw = text_w(cv, F_CTL, on ? TWM_WEIGHT_SEMIBOLD : TWM_WEIGHT_REGULAR,
				c->opts[i], 0) + 2 * SEG_PAD;
			if (on) {
				fill(cv, C(t->accent), bx, cy, bw, CTL_H);
			} else {
				frame_box(cv, C(focus && sw->sub == i ? t->accent : t->hairline_strong),
					bx, cy, bw, CTL_H);
			}
			text(cv, F_CTL, on ? TWM_WEIGHT_SEMIBOLD : TWM_WEIGHT_REGULAR,
				C(on ? t->on_accent : t->ink_2), bx + SEG_PAD, cy, CTL_H, c->opts[i], 0, 0);
			add_hit(sw, bx, cy, bw, CTL_H, HIT_CTL, idx, i);
			bx += bw - 1; /* shared borders, as B8's border-left: 0 */
		}
		break;
	}
	case CK_SLIDER: {
		int v = c->get(sw->cfg);
		double tw = w - 60, ty = y + h / 2;
		int clamped = v < c->min ? c->min : v > c->max ? c->max : v;
		double pos = (double)(clamped - c->min) / (c->max - c->min) * tw;
		fill(cv, C(t->hairline_strong), x, ty - 1, tw, 2);
		fill(cv, C(t->accent), x, ty - 1, pos, 2);
		/* A square thumb: the capture dot is the only round shape (CAP-4). */
		fill(cv, C(focus ? t->ink : t->accent), x + pos - 6, ty - 6, 12, 12);
		add_hit(sw, x - 6, y, tw + 12, h, HIT_SLIDER, idx, 0);
		char val[32];
		snprintf(val, sizeof(val), "%d%s%s", v, c->unit ? " " : "", c->unit ? c->unit : "");
		bool changed = key_changed(sw, c);
		text(cv, F_CTL, changed ? TWM_WEIGHT_SEMIBOLD : TWM_WEIGHT_REGULAR,
			C(changed ? t->accent : t->ink_2), x + tw + 14, y, h, val, 0, 0);
		break;
	}
	case CK_CHECK: {
		bool on = c->get(sw->cfg) != 0;
		checkbox(cv, x, y + (h - CHECK) / 2, on, focus);
		text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->ink_2), x + CHECK + 10, y, h, c->opts[0],
			w - CHECK - 10, 0);
		add_hit(sw, x, y, w, h, HIT_CTL, idx, 0);
		break;
	}
	case CK_BITS: {
		int v = c->get(sw->cfg);
		int n = nopts_of(sw, c);
		bool row = c->format == fmt_quiet;
		double bx = x, by = row ? y + (h - CHECK) / 2 : y + 14;
		for (int i = 0; i < n; i++) {
			bool on = (v >> i) & 1;
			checkbox(cv, bx, by, on, focus && sw->sub == i);
			double lw = text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->ink_2), bx + CHECK + 10,
				by - 4, CHECK + 8, c->opts[i], 0, 0);
			add_hit(sw, bx, by - 4, CHECK + 10 + lw, CHECK + 8, HIT_CTL, idx, i);
			if (row) {
				bx += CHECK + 10 + lw + 18;
			} else {
				by += CHECK + CHECK_GAP;
			}
		}
		break;
	}
	case CK_SWATCH: {
		uint32_t v = (uint32_t)c->get(sw->cfg) & 0xFFFFFF;
		double bx = x, by = y + (h - SWATCH) / 2;
		for (int i = 0; i < 4; i++) {
			if (v == accent_presets[i]) {
				frame_box(cv, C(t->ink), bx - 3, by - 3, SWATCH + 6, SWATCH + 6);
			} else if (focus && sw->sub == i) {
				frame_box(cv, C(t->hairline_strong), bx - 3, by - 3, SWATCH + 6, SWATCH + 6);
			}
			fill(cv, C(accent_presets[i]), bx, by, SWATCH, SWATCH);
			add_hit(sw, bx, by, SWATCH, SWATCH, HIT_CTL, idx, i);
			bx += SWATCH + SWATCH_GAP;
		}
		char hex[16];
		if (sw->editing == idx) {
			snprintf(hex, sizeof(hex), "%s", sw->edit_buf);
		} else {
			snprintf(hex, sizeof(hex), "#%06X", v);
		}
		double hx = bx - SWATCH_GAP + 6;
		double hw = text(cv, F_CTL, TWM_WEIGHT_REGULAR,
			C(sw->editing == idx ? t->ink : t->muted), hx, y, h, hex, 0, 0);
		if (sw->editing == idx) {
			fill(cv, C(t->accent), hx + hw + 1, y + h / 2 - 7, 1, 14);
		}
		add_hit(sw, hx - 4, y + 8, 80, h - 16, HIT_HEX, idx, 0);
		break;
	}
	case CK_TEXT: {
		char s[256];
		if (sw->editing == idx) {
			snprintf(s, sizeof(s), "%s", sw->edit_buf);
		} else {
			c->get_text(sw->cfg, s, sizeof(s));
		}
		double fw = w - 20;
		fill(cv, C(t->ground), x, cy, fw, CTL_H);
		frame_box(cv, C(sw->editing == idx || focus ? t->accent : t->hairline_strong),
			x, cy, fw, CTL_H);
		double tw2 = text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->ink), x + 9, cy, CTL_H, s,
			fw - 20, 0);
		if (sw->editing == idx) {
			fill(cv, C(t->accent), x + 9 + tw2 + 1, cy + 7, 1, CTL_H - 14);
		}
		add_hit(sw, x, cy, fw, CTL_H, HIT_TEXT, idx, 0);
		break;
	}
	case CK_INFO: {
		char s[256];
		c->get_text(sw->cfg, s, sizeof(s));
		text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->dim), x, y, h, s, w, 0);
		break;
	}
	case CK_LIST: {
		double ly = y + 12;
		size_t i = 0;
		char l[128], r[160];
		bool builtin;
		for (; (int)i < c->list_lines && list_row(sw, c, i, l, sizeof(l), r, sizeof(r),
				&builtin); i++) {
			uint32_t col = C(builtin ? t->dim : t->ink_2);
			text(cv, F_CTL, TWM_WEIGHT_REGULAR, col, x, ly, LIST_LINE, l, 150, 0);
			text(cv, F_CTL, TWM_WEIGHT_REGULAR, col, x + 160, ly, LIST_LINE, r, w - 160, 0);
			ly += LIST_LINE;
		}
		size_t more = 0;
		while (list_row(sw, c, i + more, l, sizeof(l), r, sizeof(r), &builtin)) {
			more++;
		}
		if (more) {
			snprintf(l, sizeof(l), "+%zu more in the file", more);
			text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->dim), x, ly, LIST_LINE, l, 0, 0);
		}
		break;
	}
	case CK_BUTTON: {
		double bw = text_w(cv, F_CTL, TWM_WEIGHT_REGULAR, c->opts[0], 0) + 2 * SEG_PAD;
		frame_box(cv, C(focus ? t->accent : t->hairline_strong), x, cy, bw, CTL_H);
		text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->ink), x + SEG_PAD, cy, CTL_H, c->opts[0], 0, 0);
		add_hit(sw, x, cy, bw, CTL_H, HIT_BUTTON, idx, 0);
		break;
	}
	}
}

/* SET-4: a focused and an unfocused tab with the pending values. */
static void draw_preview(struct setwin *sw, const struct cv *cv, double x, double y) {
	const struct twm_theme *t = cv->t;
	const struct twm_config *c = sw->cfg;
	int th = c->title_height;
	text(cv, F_SMALL, TWM_WEIGHT_REGULAR, C(t->dim), x, y, th, "PREVIEW", 0, 1.5f);
	x += text_w(cv, F_SMALL, TWM_WEIGHT_REGULAR, "PREVIEW", 1.5f) + 19;
	const char *ftitle = "notes";
	struct twm_font *fb = font_get(c->font, c->font_size, TWM_WEIGHT_SEMIBOLD, cv->scale);
	double tw = text_width(fb, ftitle, 0) / cv->scale;
	double hatch = c->title_highlight ? 35 : 0;
	double w = th + 12 + tw + 12 + (hatch ? hatch + 9 : 0);
	uint32_t acc = C(c->accent), on = C(t->on_accent);
	fill(cv, acc, x, y, w, th);
	int g = (th - 12) / 2;
	draw_glyph(cv->img, TWM_GLYPH_ICONIFY, S(cv, x + g), S(cv, y + g), cv->scale, on);
	fill(cv, rgba_argb32(rgba_over(rgba_hex(t->on_accent, 0.30f), rgba_hex(c->accent, 1))),
		x + th, y, 1, th);
	text_draw(cv->img, fb, ftitle, S(cv, x + th + 12),
		text_baseline_centered(fb, S(cv, y), S(cv, th)), on, 0, 0);
	if (hatch) {
		draw_hatch(cv->img, S(cv, x + th + 24 + tw), S(cv, y + 6), S(cv, hatch),
			S(cv, y + th - 6) - S(cv, y + 6), cv->scale,
			rgba_argb32(rgba_over(rgba_hex(t->on_accent, 0.55f), rgba_hex(c->accent, 1))));
	}
	x += w + 19;
	struct twm_font *fm = font_get(c->font, c->font_size, TWM_WEIGHT_MEDIUM, cv->scale);
	const char *utitle = "zpool status";
	double uw = text_width(fm, utitle, 0) / cv->scale + 24;
	fill(cv, C(t->graphite), x, y, uw, th);
	text_draw(cv->img, fm, utitle, S(cv, x + 12), text_baseline_centered(fm, S(cv, y), S(cv, th)),
		C(t->muted), 0, 0);
}

static void draw_controls(struct setwin *sw, const struct cv *cv) {
	const struct twm_theme *t = cv->t;
	double x0 = NAV_W + PANE_PAD_X, x1 = cv->w - DIFF_W - PANE_PAD_X;
	double y = PANE_PAD_Y;
	int list[NCTLS];
	int n = visible(sw, list);
	bool preview = !sw->filter[0] && (sw->section == SEC_FRAMES || sw->section == SEC_LOOK);
	double bottom = cv->h - PANE_PAD_Y - (preview ? sw->cfg->title_height + 14 : 0);
	int shown = 0;
	for (int k = 0; k < n; k++) {
		const struct ctl *c = &ctls[list[k]];
		double h = row_height(sw, c);
		if (y + h > bottom) {
			break;
		}
		if (sw->sel == list[k]) {
			fill(cv, wash(t->accent, 0.05f, t->surface), x0 - 8, y, x1 - x0 + 16, h - 1);
		}
		double ly = c->kind == CK_BITS && c->format != fmt_quiet ? y + 10 : y;
		double lh = c->kind == CK_BITS && c->format != fmt_quiet ? 22 :
			c->kind == CK_LIST ? 12 + LIST_LINE : h;
		text(cv, F_LABEL, TWM_WEIGHT_REGULAR, C(t->ink), x0, c->kind == CK_LIST ? y + 12 : ly,
			c->kind == CK_LIST ? LIST_LINE : lh, c->label, COL_LABEL, 0);
		double cx = x0 + COL_LABEL + COL_GAP, cw = x1 - COL_KEY - COL_GAP - cx;
		draw_control(sw, cv, list[k], cx, y, cw, h);
		if (c->key || c->kind == CK_INFO) {
			char key[48];
			bool changed = key_changed(sw, c);
			snprintf(key, sizeof(key), "%s%s", c->key ? c->key : "\xe2\x80\x94",
				changed ? " \xe2\x97\x8f" : "");
			double kw = text_w(cv, F_CTL, TWM_WEIGHT_REGULAR, key, 0);
			text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(changed ? t->accent : t->dim), x1 - kw,
				ly, lh, key, 0, 0);
		}
		fill(cv, C(t->rule_faint), x0, y + h - 1, x1 - x0, 1);
		y += h;
		shown++;
	}
	if (shown < n) {
		char more[48];
		snprintf(more, sizeof(more), "+%d more \xc2\xb7 narrow the filter", n - shown);
		text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->dim), x0, y + 6, 20, more, 0, 0);
	} else if (n == 0) {
		text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->dim), x0, y + 6, 20,
			"no key matches; the file may still have it", 0, 0);
	}
	if (preview) {
		draw_preview(sw, cv, x0, cv->h - PANE_PAD_Y - sw->cfg->title_height);
	}
}

/* --- the diff pane (SET-5) --- */

static void draw_diff(struct setwin *sw, const struct cv *cv) {
	const struct twm_theme *t = cv->t;
	double x = cv->w - DIFF_W, w = DIFF_W;
	fill(cv, C(t->surface_sunk), x, 0, w, cv->h);
	fill(cv, C(t->hairline), x, 0, 1, cv->h);
	/* Header: the file, and how many changes. */
	char name[96];
	const char *home = getenv("HOME");
	const char *path = sw->server->config_path;
	if (home && *home && strncmp(path, home, strlen(home)) == 0 && path[strlen(home)] == '/') {
		snprintf(name, sizeof(name), "~%s", path + strlen(home));
	} else {
		snprintf(name, sizeof(name), "%s", path);
	}
	text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->muted), x + DIFF_PAD, 0, DIFF_HEAD, name,
		w - 140, 1.5f);
	char count[32];
	if (sw->edits.n) {
		snprintf(count, sizeof(count), "%d change%s", sw->edits.n, sw->edits.n == 1 ? "" : "s");
	} else {
		snprintf(count, sizeof(count), "no changes");
	}
	double cw = text_w(cv, F_CTL, TWM_WEIGHT_REGULAR, count, 0);
	text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(sw->edits.n ? t->accent : t->dim),
		x + w - DIFF_PAD - cw, 0, DIFF_HEAD, count, 0, 0);
	fill(cv, C(t->hairline), x, DIFF_HEAD - 1, w, 1);

	/* Footer: the check, Discard, Write. */
	double foot_h = 12 + 15 + 12 + BTN_H + 12 + 30 + 12;
	double fy = cv->h - foot_h;
	fill(cv, C(t->hairline), x, fy, w, 1);
	double y = fy + 12;
	text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->muted), x + DIFF_PAD, y, 15, "twm --check", 0, 0);
	double chw = text_w(cv, F_CTL, TWM_WEIGHT_REGULAR, sw->check, 0);
	double maxw = w - 2 * DIFF_PAD - 100;
	double cx = x + w - DIFF_PAD - (chw < maxw ? chw : maxw);
	text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(sw->check_ok ? t->ok : t->alert), cx, y, 15,
		sw->check, maxw, 0);
	y += 15 + 12;
	double bw = (w - 2 * DIFF_PAD - 9) / 3;
	frame_box(cv, C(t->hairline_strong), x + DIFF_PAD, y, bw, BTN_H);
	double tw = text_w(cv, F_CTL, TWM_WEIGHT_REGULAR, "Discard", 0);
	text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(sw->edits.n ? t->ink : t->dim),
		x + DIFF_PAD + (bw - tw) / 2, y, BTN_H, "Discard", 0, 0);
	add_hit(sw, x + DIFF_PAD, y, bw, BTN_H, HIT_DISCARD, -1, 0);
	double wx = x + DIFF_PAD + bw + 9, ww = 2 * bw;
	bool can = sw->edits.n && sw->check_ok;
	if (can) {
		fill(cv, C(t->accent), wx, y, ww, BTN_H);
	} else {
		frame_box(cv, C(t->hairline), wx, y, ww, BTN_H);
	}
	tw = text_w(cv, F_CTL, TWM_WEIGHT_SEMIBOLD, "Write .twmrc", 0);
	text(cv, F_CTL, TWM_WEIGHT_SEMIBOLD, C(can ? t->on_accent : t->dim), wx + (ww - tw) / 2, y,
		BTN_H, "Write .twmrc", 0, 0);
	add_hit(sw, wx, y, ww, BTN_H, HIT_WRITE, -1, 0);
	y += BTN_H + 12;
	text(cv, F_SMALL, TWM_WEIGHT_REGULAR, C(t->dim), x + DIFF_PAD, y, 15,
		"Comments and your own ordering are kept.", w - 2 * DIFF_PAD, 0);
	text(cv, F_SMALL, TWM_WEIGHT_REGULAR, C(t->dim), x + DIFF_PAD, y + 15, 15,
		"M-z undoes a write.", 0, 0);

	/* Body: the unified diff with line numbers. */
	y = DIFF_HEAD + 14;
	double body_end = fy - 8;
	int room = (int)((body_end - y) / DIFF_LINE);
	if (sw->diff.n == 0) {
		text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->dim), x + DIFF_PAD, y, DIFF_LINE,
			sw->exists ? "the file as it is" : "no file yet: the built-in defaults", 0, 0);
		return;
	}
	for (int i = 0; i < sw->diff.n && i < room; i++) {
		const struct rcdiff_line *l = &sw->diff.lines[i];
		char buf[300];
		if (i == room - 1 && i + 1 < sw->diff.n) {
			snprintf(buf, sizeof(buf), "  \xe2\x80\xa6 %d more", sw->diff.n - i);
			text(cv, F_CTL, TWM_WEIGHT_REGULAR, C(t->dim), x + DIFF_PAD, y, DIFF_LINE, buf, 0, 0);
			break;
		}
		uint32_t col = C(t->dim);
		if (l->kind == RCDIFF_DEL) {
			fill(cv, wash(t->alert, 0.08f, t->surface_sunk), x + 1, y, w - 1, DIFF_LINE);
			col = C(t->alert);
		} else if (l->kind == RCDIFF_ADD) {
			fill(cv, wash(t->ok, 0.08f, t->surface_sunk), x + 1, y, w - 1, DIFF_LINE);
			col = C(t->ok);
		}
		if (l->kind == RCDIFF_GAP) {
			snprintf(buf, sizeof(buf), "  \xe2\x80\xa6");
		} else {
			/* Tabs would break the columns; one space each. */
			char line[256];
			int len = l->len < (int)sizeof(line) - 1 ? l->len : (int)sizeof(line) - 1;
			for (int k = 0; k < len; k++) {
				line[k] = l->text[k] == '\t' ? ' ' : l->text[k];
			}
			line[len] = '\0';
			snprintf(buf, sizeof(buf), "%c%3d  %s", (char)l->kind, l->number, line);
		}
		text(cv, F_CTL, TWM_WEIGHT_REGULAR, col, x + DIFF_PAD, y, DIFF_LINE, buf,
			w - 2 * DIFF_PAD, 0);
		y += DIFF_LINE;
	}
}

static void render(struct twm_internal_view *iv, float scale, struct twm_pixbuf **out,
		int *lw, int *lh) {
	struct setwin *sw = iv->data;
	int w = iv->req_w > SW_MIN_W ? iv->req_w : SW_MIN_W;
	int h = iv->req_h > SW_MIN_H ? iv->req_h : SW_MIN_H;
	*lw = w;
	*lh = h;
	struct twm_pixbuf *buf = pixbuf_create((int)lroundf(w * scale), (int)lroundf(h * scale));
	if (!buf) {
		return;
	}
	struct cv cv = {w, h, buf->image, scale, &sw->server->theme, sw->server->theme.ui_font};
	draw_fill(cv.img, C(cv.t->surface), 0, 0, S(&cv, w), S(&cv, h));
	sw->nhits = 0;
	draw_nav(sw, &cv);
	draw_controls(sw, &cv);
	draw_diff(sw, &cv);
	*out = buf;
}

/* --- input --- */

static void redraw(struct setwin *sw) {
	internal_view_redraw(sw->iv);
}

static void start_edit(struct setwin *sw, int idx) {
	sw->editing = idx;
	sw->sel = idx;
	if (ctls[idx].kind == CK_SWATCH) {
		snprintf(sw->edit_buf, sizeof(sw->edit_buf), "#%06X",
			(unsigned)ctls[idx].get(sw->cfg) & 0xFFFFFF);
	} else {
		ctls[idx].get_text(sw->cfg, sw->edit_buf, sizeof(sw->edit_buf));
	}
}

static void commit_edit(struct setwin *sw) {
	int idx = sw->editing;
	sw->editing = -1;
	if (idx >= 0) {
		set_text(sw, idx, sw->edit_buf);
	}
}

static void edit_by_hand(struct setwin *sw) {
	char cmd[PATH_MAX + 64];
	char q[PATH_MAX + 16];
	size_t o = 0;
	q[o++] = '\'';
	for (const char *p = sw->server->config_path; *p && o + 5 < sizeof(q); p++) {
		if (*p == '\'') {
			memcpy(q + o, "'\\''", 4);
			o += 4;
		} else {
			q[o++] = *p;
		}
	}
	q[o++] = '\'';
	q[o] = '\0';
	snprintf(cmd, sizeof(cmd), "${EDITOR:-vi} %s", q);
	action_run_in_terminal(sw->server, cmd);
}

/* B1 on a control part, or the keyboard's equivalent. */
static void activate(struct setwin *sw, int idx, int sub) {
	const struct ctl *c = &ctls[idx];
	sw->sel = idx;
	sw->sub = sub;
	switch (c->kind) {
	case CK_SEG:
		set_int(sw, idx, sub);
		break;
	case CK_CHECK:
		set_int(sw, idx, !c->get(sw->cfg));
		break;
	case CK_BITS:
		set_int(sw, idx, c->get(sw->cfg) ^ (1 << sub));
		break;
	case CK_SWATCH:
		set_int(sw, idx, (int)accent_presets[sub & 3]);
		break;
	case CK_TEXT:
		start_edit(sw, idx);
		break;
	case CK_BUTTON:
		edit_by_hand(sw);
		break;
	default:
		break;
	}
}

static void slider_at(struct setwin *sw, const struct hit *h, double x) {
	const struct ctl *c = &ctls[h->ctl];
	double tw = (h->x1 - h->x0) - 12;
	double f = (x - (h->x0 + 6)) / tw;
	f = f < 0 ? 0 : f > 1 ? 1 : f;
	int v = c->min + (int)lround(f * (c->max - c->min) / c->step) * c->step;
	sw->sel = h->ctl;
	if (v != c->get(sw->cfg)) {
		set_int(sw, h->ctl, v);
	}
}

static bool on_button(struct twm_internal_view *iv, uint32_t button, bool pressed,
		double x, double y) {
	struct setwin *sw = iv->data;
	if (button != TWM_BTN_LEFT) {
		return true;
	}
	for (int i = sw->nhits - 1; i >= 0; i--) {
		const struct hit *h = &sw->hits[i];
		if (x < h->x0 || x >= h->x1 || y < h->y0 || y >= h->y1) {
			continue;
		}
		if (h->kind == HIT_SLIDER) {
			/* Press sets it; the release sets it again (a drag, in two steps:
			 * twm's own windows see presses, not motion). */
			slider_at(sw, h, x);
			redraw(sw);
			return true;
		}
		if (!pressed) {
			return true;
		}
		if (sw->editing >= 0 && !(h->kind == HIT_TEXT && h->ctl == sw->editing)) {
			commit_edit(sw);
		}
		switch (h->kind) {
		case HIT_NAV:
			sw->section = h->sub;
			sw->filter[0] = '\0';
			sw->sel = -1;
			break;
		case HIT_FILTER:
			break;
		case HIT_CTL:
			activate(sw, h->ctl, h->sub);
			break;
		case HIT_HEX:
		case HIT_TEXT:
			if (sw->editing != h->ctl) {
				start_edit(sw, h->ctl);
			}
			break;
		case HIT_DISCARD:
			do_discard(sw);
			break;
		case HIT_WRITE:
			do_write(sw);
			break;
		case HIT_BUTTON:
			edit_by_hand(sw);
			break;
		default:
			break;
		}
		redraw(sw);
		return true;
	}
	if (pressed && sw->editing >= 0) {
		commit_edit(sw);
		redraw(sw);
	}
	return true;
}

static void move_sel(struct setwin *sw, int dir) {
	int list[NCTLS];
	int n = visible(sw, list);
	if (n == 0) {
		return;
	}
	int at = -1;
	for (int k = 0; k < n; k++) {
		if (list[k] == sw->sel) {
			at = k;
		}
	}
	at = at < 0 ? (dir > 0 ? 0 : n - 1) : (at + dir + n) % n;
	sw->sel = list[at];
	sw->sub = 0;
}

static void adjust(struct setwin *sw, int dir, bool big) {
	if (sw->sel < 0) {
		return;
	}
	const struct ctl *c = &ctls[sw->sel];
	switch (c->kind) {
	case CK_SEG: {
		int v = c->get(sw->cfg) + dir;
		if (v >= 0 && v < c->nopts) {
			set_int(sw, sw->sel, v);
		}
		break;
	}
	case CK_SLIDER: {
		int v = c->get(sw->cfg) + dir * c->step * (big ? 5 : 1);
		v = v < c->min ? c->min : v > c->max ? c->max : v;
		set_int(sw, sw->sel, v);
		break;
	}
	case CK_BITS:
	case CK_SWATCH: {
		int n = c->kind == CK_SWATCH ? 4 : nopts_of(sw, c);
		sw->sub = (sw->sub + dir + n) % n;
		if (c->kind == CK_SWATCH) {
			set_int(sw, sw->sel, (int)accent_presets[sw->sub]);
		}
		break;
	}
	default:
		break;
	}
}

static void append_utf8(char *buf, size_t len, const char *utf8) {
	size_t n = strlen(buf);
	if (n + strlen(utf8) < len) {
		strcat(buf, utf8);
	}
}

static void backspace(char *buf) {
	size_t len = strlen(buf);
	while (len > 0 && ((unsigned char)buf[len - 1] & 0xC0) == 0x80) {
		len--;
	}
	if (len > 0) {
		len--;
	}
	buf[len] = '\0';
}

static bool on_key(struct twm_internal_view *iv, xkb_keysym_t sym, const char *utf8,
		uint32_t mods, bool first) {
	struct setwin *sw = iv->data;
	bool ctrl = mods & TWM_MOD_CTRL, shift = mods & TWM_MOD_SHIFT;
	if (!first) {
		return false;
	}
	/* HIS-5: M-z / M-S-z here undo Settings' own changes and writes. */
	if (mods & sw->server->config->modifier) {
		if (sym == XKB_KEY_z || sym == XKB_KEY_Z) {
			if (sw->editing >= 0) {
				sw->editing = -1;
			}
			do_undo(sw, shift);
			redraw(sw);
			return true;
		}
		return false; /* the rest of M-… stays with the bindings */
	}
	if (mods & TWM_MOD_ALT) {
		return false;
	}
	if (sw->editing >= 0) {
		switch (sym) {
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
		case XKB_KEY_Tab:
			commit_edit(sw);
			break;
		case XKB_KEY_Escape:
			sw->editing = -1;
			break;
		case XKB_KEY_BackSpace:
			backspace(sw->edit_buf);
			break;
		default:
			if (!ctrl && utf8[0] && (unsigned char)utf8[0] >= 0x20) {
				append_utf8(sw->edit_buf, sizeof(sw->edit_buf), utf8);
			}
			break;
		}
		redraw(sw);
		return true;
	}
	if (ctrl) {
		if (sym == XKB_KEY_s) {
			do_write(sw);
		} else if (sym == XKB_KEY_d) {
			do_discard(sw);
		} else {
			return false;
		}
		redraw(sw);
		return true;
	}
	switch (sym) {
	case XKB_KEY_Up:
		move_sel(sw, -1);
		break;
	case XKB_KEY_Down:
		move_sel(sw, 1);
		break;
	case XKB_KEY_Left:
		adjust(sw, -1, shift);
		break;
	case XKB_KEY_Right:
		adjust(sw, 1, shift);
		break;
	case XKB_KEY_space:
		if (sw->sel >= 0 && (ctls[sw->sel].kind == CK_CHECK || ctls[sw->sel].kind == CK_BITS)) {
			activate(sw, sw->sel, sw->sub);
		}
		break;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if (sw->sel >= 0) {
			const struct ctl *c = &ctls[sw->sel];
			if (c->kind == CK_TEXT || c->kind == CK_SWATCH) {
				start_edit(sw, sw->sel);
			} else if (c->kind == CK_BUTTON) {
				edit_by_hand(sw);
			} else if (c->kind == CK_CHECK) {
				activate(sw, sw->sel, 0);
			}
		}
		break;
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab:
		sw->filter[0] = '\0';
		sw->section = (sw->section + (shift || sym == XKB_KEY_ISO_Left_Tab ? SEC_COUNT - 1 : 1)) %
			SEC_COUNT;
		sw->sel = -1;
		break;
	case XKB_KEY_BackSpace:
		backspace(sw->filter);
		sw->sel = -1;
		break;
	case XKB_KEY_Escape:
		if (!sw->filter[0]) {
			return false;
		}
		sw->filter[0] = '\0';
		sw->sel = -1;
		break;
	default:
		/* Typing narrows the controls (SET-2). */
		if (utf8[0] && (unsigned char)utf8[0] > 0x20) {
			append_utf8(sw->filter, sizeof(sw->filter), utf8);
			sw->sel = -1;
			break;
		}
		return false;
	}
	redraw(sw);
	return true;
}

static bool wants_keys_first(struct twm_internal_view *iv) {
	return true;
}

static void on_blur(struct twm_internal_view *iv) {
	struct setwin *sw = iv->data;
	if (sw->editing >= 0) {
		commit_edit(sw);
		redraw(sw);
	}
}

/* First time: centred on the output. */
static void place(struct twm_internal_view *iv, struct wlr_box out, int *x, int *y) {
	struct wlr_box f = view_frame_box(&iv->view);
	*x = out.x + (out.width - f.width) / 2;
	*y = out.y + (out.height - f.height) / 2;
}

/* --- lifecycle --- */

void setwin_file_changed(struct twm_server *server) {
	struct setwin *sw = sw_of(server);
	if (!sw) {
		return;
	}
	char *old = strdup(sw->disk);
	read_disk(sw);
	if (strcmp(old, sw->disk) != 0 || !sw->cfg) {
		recompute(sw); /* SET-8: the edits go on top of the new file */
		if (sw->iv->view.shown) {
			redraw(sw);
		}
	}
	free(old);
}

void setwin_toggle(struct twm_server *server) {
	struct setwin *sw = sw_of(server);
	if (!sw) {
		return;
	}
	if (sw->iv->view.shown && server->focused == &sw->iv->view) {
		internal_view_hide(sw->iv);
		return;
	}
	setwin_file_changed(server);
	if (!sw->iv->view.shown) {
		internal_view_show(sw->iv);
	}
	view_raise(&sw->iv->view);
	view_focus(&sw->iv->view);
}

void setwin_init(struct twm_server *server) {
	struct setwin *sw = calloc(1, sizeof(*sw));
	if (!sw) {
		return;
	}
	sw->server = server;
	sw->disk = strdup("");
	sw->section = SEC_FRAMES; /* as B8 opens */
	sw->sel = -1;
	sw->editing = -1;
	char title[128];
	const char *home = getenv("HOME");
	const char *path = server->config_path ? server->config_path : "~/.twmrc";
	if (home && *home && strncmp(path, home, strlen(home)) == 0 && path[strlen(home)] == '/') {
		snprintf(title, sizeof(title), "settings \xe2\x80\x94 ~%s", path + strlen(home));
	} else {
		snprintf(title, sizeof(title), "settings \xe2\x80\x94 %s", path);
	}
	sw->iv = internal_view_create(server, title, "twm-settings", render, sw);
	if (!sw->iv) {
		free(sw);
		return;
	}
	sw->iv->view.utility = false; /* B8: a full frame, iconify and resize boxes */
	sw->iv->resizable = true;     /* a bigger window shows more of the diff */
	sw->iv->min_w = sw->iv->req_w = SW_MIN_W;
	sw->iv->min_h = sw->iv->req_h = SW_MIN_H;
	sw->iv->button = on_button;
	sw->iv->key = on_key;
	sw->iv->wants_keys_first = wants_keys_first;
	sw->iv->blur = on_blur;
	sw->iv->place = place;
	server->setwin = sw->iv;
	read_disk(sw);
	recompute(sw);
}

void setwin_finish(struct twm_server *server) {
	struct setwin *sw = sw_of(server);
	if (!sw) {
		return;
	}
	internal_view_destroy(sw->iv);
	server->setwin = NULL;
	for (int i = 0; i < sw->nwundo; i++) {
		free(sw->wundo[i].text);
	}
	for (int i = 0; i < sw->nwredo; i++) {
		free(sw->wredo[i].text);
	}
	rcdiff_free(&sw->diff);
	free(sw->diff_old);
	free(sw->pending);
	free(sw->disk);
	config_free(sw->cfg);
	free(sw);
}
