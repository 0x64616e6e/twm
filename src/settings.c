#include <ctype.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "settings.h"

/*
 * Built-in defaults: DESIGN §5 values, the KEY table, MNU-4's menu. A user
 * .twmrc is read on top; its bindings replace these for the same trigger,
 * and `NoDefaultBindings` drops the rest.
 */
const char config_default_twmrc[] =
	"Theme            \"phosphor\"\n"
	"Accent           #FFB000\n"
	"Font             \"IBM Plex Mono\" 12\n"
	"TitleHeight      26\n"
	"BorderWidth      1\n"
	"TitleHighlight   on\n"
	"SqueezeTitle     { \"*\" left }\n"
	"RootWeave        on\n"
	"RootWordmark     on\n"
	"Effects          none\n"
	"Scale            auto\n"
	"FocusPolicy      sloppy\n"
	"FocusDelay       80\n"
	"RaiseOnClick     off\n"
	"OpaqueMove       off\n"
	"Placement        interactive\n"
	"RememberGeometry on\n"
	"Snap             { edges thirds }\n"
	"SnapDistance     12\n"
	"Modifier         Mod4\n"
	"Terminal         \"foot\"\n"
	"HintKeys         \"asdfjklgh\"\n"
	"RootLineTimeout  60\n"
	"IconManager      { at top-right sort name }\n"
	"IconsOnRoot      on\n"
	"Panner           { desktops 4 }\n"
	"XWayland         on\n"
	"Notifications    on\n"
	"\n"
	"TouchpadTap                 on\n"
	"TouchpadTapDrag             on\n"
	"TouchpadNaturalScroll       off\n"
	"TouchpadDisableWhileTyping  on\n"
	"TouchpadScroll              two-finger\n"
	"TouchpadClick               default\n"
	"TouchpadSpeed               0\n"
	"TouchpadAccel               adaptive\n"
	"Touchscreen                 on\n"
	"\n"
	"Button1 = root    : f.menu \"main\"\n"
	"Button1 = title   : f.raisemove\n"
	"Button2 = title   : f.move\n"
	"Button3 = title   : f.squeeze\n"
	"Button1 = iconify : f.iconify\n"
	"Button1 = resize  : f.resize\n"
	"Button2 = resize  : f.resize\n"
	"Button1 = M : window : f.move\n"
	"Button3 = M : window : f.resize\n"
	"Button1 = icon    : f.iconify\n"
	"Button2 = icon    : f.move\n"
	"\n"
	"\"M-Return\" : f.terminal\n"
	"\"M-b\"      : !\"${BROWSER:-firefox}\"\n"
	"\"M-f\"      : !\"xdg-open \\\"$HOME\\\"\"\n"
	"\"M-r\"      : f.resize\n"
	"\"M-i\"      : f.iconify\n"
	"\"M-space\"  : f.raiselower\n"
	"\"M-S-r\"    : f.reload\n"
	"\"M-S-q\"    : f.quit\n"
	"\"M-;\"      : f.hints\n"
	"\"M-z\"      : f.undo\n"
	"\"M-S-z\"    : f.redo\n"
	"\"M-u\"      : f.urgent\n"
	"\"M-n\"      : f.log\n"
	"\"M-Tab\"    : f.nexttab\n"
	"\"M-m\"      : f.menu \"main\"\n"
	"\"M-S-m\"    : f.move\n"
	"\"M-S-h\"    : f.history\n"
	"\"M-comma\"  : f.settings\n"
	"\"M-1\" : f.desktop 1\n\"M-2\" : f.desktop 2\n\"M-3\" : f.desktop 3\n"
	"\"M-4\" : f.desktop 4\n\"M-5\" : f.desktop 5\n\"M-6\" : f.desktop 6\n"
	"\"M-7\" : f.desktop 7\n\"M-8\" : f.desktop 8\n\"M-9\" : f.desktop 9\n"
	"\"M-S-1\" : f.send 1\n\"M-S-2\" : f.send 2\n\"M-S-3\" : f.send 3\n"
	"\"M-S-4\" : f.send 4\n\"M-S-5\" : f.send 5\n\"M-S-6\" : f.send 6\n"
	"\"M-S-7\" : f.send 7\n\"M-S-8\" : f.send 8\n\"M-S-9\" : f.send 9\n"
	"\n"
	"Menu \"main\" {\n"
	"  \"xterm\"          f.terminal\n"
	"  \"browser\"        !\"${BROWSER:-firefox}\"\n"
	"  \"files\"          !\"xdg-open \\\"$HOME\\\"\"\n"
	"  \"\"               f.separator\n"
	"  \"move\"           f.move\n"
	"  \"resize\"         f.resize\n"
	"  \"squeeze\"        f.squeeze\n"
	"  \"iconify\"        f.iconify\n"
	"  \"raise / lower\"  f.raiselower\n"
	"  \"\"               f.separator\n"
	"  \"desktops\"       f.menu \"desktops\"\n"
	"  \"settings\"       f.settings\n"
	"  \"reload .twmrc\"  f.reload\n"
	"  \"exit\"           f.quit\n"
	"}\n"
	"Menu \"desktops\" {\n"
	"  \"1\" f.desktop 1\n  \"2\" f.desktop 2\n  \"3\" f.desktop 3\n  \"4\" f.desktop 4\n"
	"}\n";

/* --- small helpers --- */

static void *xgrow(void *ptr, size_t n, size_t size) {
	void *p = realloc(ptr, (n + 1) * size);
	if (!p) {
		abort();
	}
	return p;
}

static char *xstrdup(const char *s) {
	char *d = strdup(s ? s : "");
	if (!d) {
		abort();
	}
	return d;
}

static void set_str(char **dst, const char *s) {
	free(*dst);
	*dst = xstrdup(s);
}

struct ctx {
	struct twm_config *cfg;
	struct twmrc_doc *doc;
	bool builtin;
	const struct twmrc_stmt *st;
};

#define ERR(c, ...) twmrc_diag((c)->doc, TWMRC_ERROR, (c)->st->line, __VA_ARGS__)
#define WARN(c, ...) twmrc_diag((c)->doc, TWMRC_WARNING, (c)->st->line, __VA_ARGS__)

static const struct twmrc_row *values(const struct ctx *c) {
	static const struct twmrc_row empty = {0};
	return c->st->nsegments ? &c->st->segments[0] : &empty;
}

static bool want_n(struct ctx *c, size_t n, const char *what) {
	if (values(c)->n != n) {
		ERR(c, "%s wants %s", c->st->key, what);
		return false;
	}
	return true;
}

static bool node_word(const struct twmrc_node *n, const char *w) {
	return (n->kind == TWMRC_WORD || n->kind == TWMRC_STRING) && strcasecmp(n->text, w) == 0;
}

static bool parse_bool(const struct twmrc_node *n, bool *out) {
	if (n->kind != TWMRC_WORD) {
		return false;
	}
	static const char *yes[] = {"on", "yes", "true"}, *no[] = {"off", "no", "false"};
	for (int i = 0; i < 3; i++) {
		if (strcasecmp(n->text, yes[i]) == 0) {
			*out = true;
			return true;
		}
		if (strcasecmp(n->text, no[i]) == 0) {
			*out = false;
			return true;
		}
	}
	return false;
}

static void set_bool(struct ctx *c, bool *dst) {
	if (want_n(c, 1, "on or off") && !parse_bool(&values(c)->nodes[0], dst)) {
		ERR(c, "%s wants on or off", c->st->key);
	}
}

static void set_int(struct ctx *c, int *dst, int min, int max) {
	if (!want_n(c, 1, "a number")) {
		return;
	}
	const struct twmrc_node *n = &values(c)->nodes[0];
	if (n->kind != TWMRC_NUMBER || n->number != (int)n->number ||
			n->number < min || n->number > max) {
		ERR(c, "%s wants a whole number from %d to %d", c->st->key, min, max);
		return;
	}
	*dst = (int)n->number;
}

static void set_string(struct ctx *c, char **dst) {
	if (want_n(c, 1, "a quoted string")) {
		const struct twmrc_node *n = &values(c)->nodes[0];
		if (n->kind != TWMRC_STRING) {
			ERR(c, "%s wants a quoted string", c->st->key);
			return;
		}
		set_str(dst, n->text);
	}
}

/* One of `choices` (NULL-terminated); stores the index. */
static void set_choice(struct ctx *c, int *dst, const char *const *choices) {
	char list[160] = "";
	for (int i = 0; choices[i]; i++) {
		if (i) {
			strncat(list, choices[i + 1] ? ", " : " or ", sizeof(list) - strlen(list) - 1);
		}
		strncat(list, choices[i], sizeof(list) - strlen(list) - 1);
	}
	if (!want_n(c, 1, list)) {
		return;
	}
	const struct twmrc_node *n = &values(c)->nodes[0];
	for (int i = 0; choices[i]; i++) {
		if (node_word(n, choices[i])) {
			*dst = i;
			return;
		}
	}
	ERR(c, "%s wants %s", c->st->key, list);
}

/* Flatten a block's rows into one list of nodes. */
static size_t block_nodes(const struct twmrc_node *blk, const struct twmrc_node ***out) {
	size_t n = 0;
	for (size_t r = 0; r < blk->nrows; r++) {
		n += blk->rows[r].n;
	}
	const struct twmrc_node **list = calloc(n ? n : 1, sizeof(*list));
	size_t k = 0;
	for (size_t r = 0; r < blk->nrows; r++) {
		for (size_t i = 0; i < blk->rows[r].n; i++) {
			list[k++] = &blk->rows[r].nodes[i];
		}
	}
	*out = list;
	return n;
}

static const struct twmrc_node *the_block(struct ctx *c, const char *what) {
	if (values(c)->n != 1 || values(c)->nodes[0].kind != TWMRC_BLOCK) {
		ERR(c, "%s wants { %s }", c->st->key, what);
		return NULL;
	}
	return &values(c)->nodes[0];
}

/* --- functions --- */

static const struct {
	const char *name;
	enum twm_func_kind kind;
	char arg; /* 0 none, 's' string, 'n' number */
} functions[] = {
	{"f.nop", TWM_F_NOP, 0},
	{"f.exec", TWM_F_EXEC, 's'},
	{"f.terminal", TWM_F_TERMINAL, 0},
	{"f.menu", TWM_F_MENU, 's'},
	{"f.move", TWM_F_MOVE, 0},
	{"f.resize", TWM_F_RESIZE, 0},
	{"f.raisemove", TWM_F_RAISEMOVE, 0},
	{"f.squeeze", TWM_F_SQUEEZE, 0},
	{"f.iconify", TWM_F_ICONIFY, 0},
	{"f.raise", TWM_F_RAISE, 0},
	{"f.lower", TWM_F_LOWER, 0},
	{"f.raiselower", TWM_F_RAISELOWER, 0},
	{"f.focus", TWM_F_FOCUS, 0},
	{"f.delete", TWM_F_DELETE, 0},
	{"f.reload", TWM_F_RELOAD, 0},
	{"f.quit", TWM_F_QUIT, 0},
	{"f.hints", TWM_F_HINTS, 0},
	{"f.undo", TWM_F_UNDO, 0},
	{"f.redo", TWM_F_REDO, 0},
	{"f.urgent", TWM_F_URGENT, 0},
	{"f.log", TWM_F_LOG, 0},
	{"f.nexttab", TWM_F_NEXTTAB, 0},
	{"f.history", TWM_F_HISTORY, 0},
	{"f.settings", TWM_F_SETTINGS, 0},
	{"f.desktop", TWM_F_DESKTOP, 'n'},
	{"f.send", TWM_F_SEND, 'n'},
	{"f.separator", TWM_F_SEPARATOR, 0},
};

const char *config_func_name(enum twm_func_kind kind) {
	for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
		if (functions[i].kind == kind) {
			return functions[i].name;
		}
	}
	return "f.?";
}

static void func_free(struct twm_func *f) {
	free(f->arg);
	f->arg = NULL;
}

static bool parse_func(struct ctx *c, const struct twmrc_node *nodes, size_t n,
		struct twm_func *out) {
	*out = (struct twm_func){0};
	if (n == 0) {
		ERR(c, "a function is missing");
		return false;
	}
	if (nodes[0].kind == TWMRC_EXEC) {
		if (n > 1) {
			ERR(c, "unexpected words after !\"%s\"", nodes[0].text);
			return false;
		}
		out->kind = TWM_F_EXEC;
		out->arg = xstrdup(nodes[0].text);
		return true;
	}
	if (nodes[0].kind != TWMRC_WORD) {
		ERR(c, "a function like f.move or !\"command\" is missing");
		return false;
	}
	for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
		if (strcmp(functions[i].name, nodes[0].text) != 0) {
			continue;
		}
		out->kind = functions[i].kind;
		size_t want = functions[i].arg ? 2 : 1;
		if (n != want) {
			ERR(c, "%s %s", nodes[0].text, functions[i].arg == 's' ? "wants a quoted name" :
				functions[i].arg == 'n' ? "wants a number" : "takes no arguments");
			return false;
		}
		if (functions[i].arg == 's') {
			if (nodes[1].kind != TWMRC_STRING) {
				ERR(c, "%s wants a quoted name", nodes[0].text);
				return false;
			}
			out->arg = xstrdup(nodes[1].text);
		} else if (functions[i].arg == 'n') {
			if (nodes[1].kind != TWMRC_NUMBER || nodes[1].number < 1 || nodes[1].number > 9) {
				ERR(c, "%s wants a desktop number from 1 to 9", nodes[0].text);
				return false;
			}
			out->num = (int)nodes[1].number;
		}
		return true;
	}
	ERR(c, "unknown function %s", nodes[0].text);
	return false;
}

/* --- bindings --- */

static bool parse_mods(const char *s, uint32_t *mods) {
	*mods = 0;
	if (strcasecmp(s, "none") == 0) {
		return true;
	}
	for (const char *p = s; *p; p++) {
		switch (toupper((unsigned char)*p)) {
		case 'M': *mods |= TWM_MOD_M; break;
		case 'S': *mods |= TWM_MOD_SHIFT; break;
		case 'C': *mods |= TWM_MOD_CTRL; break;
		case 'A': *mods |= TWM_MOD_ALT; break;
		case 'L': *mods |= TWM_MOD_LOGO; break;
		case '-': break;
		default: return false;
		}
	}
	return true;
}

/* "M-S-Return", "M-;", "C-M-x": modifier letters, then a key name. */
static bool parse_keyspec(const char *spec, uint32_t *mods, xkb_keysym_t *sym) {
	*mods = 0;
	const char *p = spec;
	while (p[0] && p[1] == '-' && p[2]) {
		uint32_t m;
		char letter[2] = {p[0], '\0'};
		if (!parse_mods(letter, &m)) {
			return false;
		}
		*mods |= m;
		p += 2;
	}
	if (!*p) {
		return false;
	}
	if (p[1] == '\0') {
		*sym = xkb_utf32_to_keysym((uint32_t)(unsigned char)p[0]);
	} else {
		*sym = xkb_keysym_from_name(p, XKB_KEYSYM_CASE_INSENSITIVE);
	}
	if (*sym == XKB_KEY_NoSymbol) {
		return false;
	}
	*sym = xkb_keysym_to_lower(*sym);
	return true;
}

static void add_key(struct ctx *c) {
	uint32_t mods;
	xkb_keysym_t sym;
	if (!parse_keyspec(c->st->key, &mods, &sym)) {
		ERR(c, "unknown key name \"%s\"", c->st->key);
		return;
	}
	if (c->st->nsegments != 1) {
		ERR(c, "\"%s\" : wants one function", c->st->key);
		return;
	}
	struct twm_func func;
	if (!parse_func(c, c->st->segments[0].nodes, c->st->segments[0].n, &func)) {
		return;
	}
	struct twm_config *cfg = c->cfg;
	for (size_t i = 0; i < cfg->nkeys; i++) {
		struct twm_key_binding *k = &cfg->keys[i];
		if (k->mods == mods && k->sym == sym) {
			if (!k->builtin && !c->builtin) {
				WARN(c, "\"%s\" is bound twice; this one wins", c->st->key);
			}
			func_free(&k->func);
			k->func = func;
			k->builtin = c->builtin;
			set_str(&k->spec, c->st->key);
			return;
		}
	}
	cfg->keys = xgrow(cfg->keys, cfg->nkeys, sizeof(*cfg->keys));
	cfg->keys[cfg->nkeys++] = (struct twm_key_binding){
		.mods = mods, .sym = sym, .spec = xstrdup(c->st->key),
		.func = func, .builtin = c->builtin,
	};
}

static const char *const context_names[] = {
	"root", "title", "iconify", "resize", "window", "border", "icon", "iconmgr", NULL,
};

static void add_button(struct ctx *c) {
	const char *key = c->st->key;
	if (strncasecmp(key, "Button", 6) != 0 || key[6] < '1' || key[6] > '5' || key[7]) {
		ERR(c, "unknown key %s", key);
		return;
	}
	uint32_t button = (uint32_t)(key[6] - '0');
	size_t n = c->st->nsegments;
	if (n != 2 && n != 3) {
		ERR(c, "%s wants = [modifiers :] context : function", key);
		return;
	}
	uint32_t mods = 0;
	if (n == 3) {
		const struct twmrc_row *m = &c->st->segments[0];
		if (m->n != 1 || m->nodes[0].kind != TWMRC_WORD || !parse_mods(m->nodes[0].text, &mods)) {
			ERR(c, "%s: modifiers are letters M, S, C, A, like M or M-S", key);
			return;
		}
	}
	const struct twmrc_row *ctxrow = &c->st->segments[n - 2];
	int context = -1;
	if (ctxrow->n == 1) {
		for (int i = 0; context_names[i]; i++) {
			if (node_word(&ctxrow->nodes[0], context_names[i])) {
				context = i;
			}
		}
	}
	if (context < 0) {
		ERR(c, "%s: context is one of root, title, iconify, resize, window, border, "
			"icon, iconmgr", key);
		return;
	}
	struct twm_func func;
	const struct twmrc_row *f = &c->st->segments[n - 1];
	if (!parse_func(c, f->nodes, f->n, &func)) {
		return;
	}
	struct twm_config *cfg = c->cfg;
	for (size_t i = 0; i < cfg->nbuttons; i++) {
		struct twm_button_binding *b = &cfg->buttons[i];
		if (b->button == button && b->mods == mods && b->context == (enum twm_context)context) {
			if (!b->builtin && !c->builtin) {
				WARN(c, "%s on %s is bound twice; this one wins", key, context_names[context]);
			}
			func_free(&b->func);
			b->func = func;
			b->builtin = c->builtin;
			return;
		}
	}
	cfg->buttons = xgrow(cfg->buttons, cfg->nbuttons, sizeof(*cfg->buttons));
	cfg->buttons[cfg->nbuttons++] = (struct twm_button_binding){
		.button = button, .mods = mods, .context = (enum twm_context)context,
		.func = func, .builtin = c->builtin,
	};
}

/* --- settings --- */

static void menu_free(struct twm_menu *m) {
	free(m->name);
	for (size_t i = 0; i < m->nitems; i++) {
		free(m->items[i].label);
		func_free(&m->items[i].func);
	}
	free(m->items);
}

static void k_menu(struct ctx *c) {
	const struct twmrc_row *v = values(c);
	if (v->n != 2 || v->nodes[0].kind != TWMRC_STRING || v->nodes[1].kind != TWMRC_BLOCK) {
		ERR(c, "Menu wants \"name\" { \"label\" function … }");
		return;
	}
	struct twm_menu menu = {.name = xstrdup(v->nodes[0].text)};
	const struct twmrc_node *blk = &v->nodes[1];
	bool ok = true;
	for (size_t r = 0; r < blk->nrows && ok; r++) {
		const struct twmrc_row *row = &blk->rows[r];
		if (row->n < 2 || row->nodes[0].kind != TWMRC_STRING) {
			twmrc_diag(c->doc, TWMRC_ERROR, row->nodes[0].line,
				"menu \"%s\": each line is \"label\" function", menu.name);
			ok = false;
			break;
		}
		struct twm_func func;
		struct ctx rc = *c;
		struct twmrc_stmt fake = *c->st;
		fake.line = row->nodes[0].line;
		rc.st = &fake;
		if (!parse_func(&rc, row->nodes + 1, row->n - 1, &func)) {
			ok = false;
			break;
		}
		menu.items = xgrow(menu.items, menu.nitems, sizeof(*menu.items));
		menu.items[menu.nitems++] = (struct twm_menu_item){
			.label = xstrdup(row->nodes[0].text), .func = func,
		};
	}
	if (!ok) {
		menu_free(&menu);
		return;
	}
	struct twm_config *cfg = c->cfg;
	for (size_t i = 0; i < cfg->nmenus; i++) {
		if (strcmp(cfg->menus[i].name, menu.name) == 0) {
			menu_free(&cfg->menus[i]);
			cfg->menus[i] = menu;
			return;
		}
	}
	cfg->menus = xgrow(cfg->menus, cfg->nmenus, sizeof(*cfg->menus));
	cfg->menus[cfg->nmenus++] = menu;
}

static void k_theme(struct ctx *c) {
	if (!want_n(c, 1, "\"phosphor\"") || !node_word(&values(c)->nodes[0], "phosphor")) {
		ERR(c, "Theme: only \"phosphor\" exists");
	}
}

static void k_accent(struct ctx *c) {
	if (!want_n(c, 1, "a color like #FFB000")) {
		return;
	}
	const struct twmrc_node *n = &values(c)->nodes[0];
	if (n->kind != TWMRC_COLOR) {
		ERR(c, "Accent wants a color like #FFB000");
		return;
	}
	c->cfg->accent = n->color;
}

static void k_font(struct ctx *c) {
	const struct twmrc_row *v = values(c);
	if (v->n != 2 || v->nodes[0].kind != TWMRC_STRING || v->nodes[1].kind != TWMRC_NUMBER ||
			v->nodes[1].number < 6 || v->nodes[1].number > 72) {
		ERR(c, "Font wants \"family\" and a pixel size from 6 to 72");
		return;
	}
	snprintf(c->cfg->font, sizeof(c->cfg->font), "%s", v->nodes[0].text);
	c->cfg->font_size = (int)v->nodes[1].number;
}

static void k_title_height(struct ctx *c) { set_int(c, &c->cfg->title_height, 22, 34); }
static void k_border_width(struct ctx *c) { set_int(c, &c->cfg->border_width, 1, 4); }
static void k_title_highlight(struct ctx *c) { set_bool(c, &c->cfg->title_highlight); }
static void k_root_weave(struct ctx *c) { set_bool(c, &c->cfg->root_weave); }
static void k_root_wordmark(struct ctx *c) { set_bool(c, &c->cfg->root_wordmark); }
static void k_raise_on_click(struct ctx *c) { set_bool(c, &c->cfg->raise_on_click); }
static void k_opaque_move(struct ctx *c) { set_bool(c, &c->cfg->opaque_move); }
static void k_remember(struct ctx *c) { set_bool(c, &c->cfg->remember_geometry); }
static void k_icons_on_root(struct ctx *c) { set_bool(c, &c->cfg->icons_on_root); }
static void k_xwayland(struct ctx *c) { set_bool(c, &c->cfg->xwayland); }
static void k_notifications(struct ctx *c) { set_bool(c, &c->cfg->notifications); }
static void k_tp_tap(struct ctx *c) { set_bool(c, &c->cfg->tp_tap); }
static void k_tp_tap_drag(struct ctx *c) { set_bool(c, &c->cfg->tp_tap_drag); }
static void k_tp_natural(struct ctx *c) { set_bool(c, &c->cfg->tp_natural_scroll); }
static void k_tp_dwt(struct ctx *c) { set_bool(c, &c->cfg->tp_dwt); }
static void k_touchscreen(struct ctx *c) { set_bool(c, &c->cfg->touchscreen); }

static void k_tp_scroll(struct ctx *c) {
	static const char *const ch[] = {"two-finger", "edge", "none", NULL};
	int v = (int)c->cfg->tp_scroll;
	set_choice(c, &v, ch);
	c->cfg->tp_scroll = (enum twm_tp_scroll)v;
}

static void k_tp_click(struct ctx *c) {
	static const char *const ch[] = {"default", "areas", "clickfinger", NULL};
	int v = (int)c->cfg->tp_click;
	set_choice(c, &v, ch);
	c->cfg->tp_click = (enum twm_tp_click)v;
}

static void k_tp_accel(struct ctx *c) {
	static const char *const ch[] = {"adaptive", "flat", NULL};
	int v = (int)c->cfg->tp_accel;
	set_choice(c, &v, ch);
	c->cfg->tp_accel = (enum twm_tp_accel)v;
}

static void k_tp_speed(struct ctx *c) {
	if (!want_n(c, 1, "a number from -1 to 1")) {
		return;
	}
	const struct twmrc_node *n = &values(c)->nodes[0];
	if (n->kind != TWMRC_NUMBER || n->number < -1 || n->number > 1) {
		ERR(c, "TouchpadSpeed wants a number from -1 (slow) to 1 (fast)");
		return;
	}
	c->cfg->tp_speed = n->number;
}
static void k_focus_delay(struct ctx *c) { set_int(c, &c->cfg->focus_delay, 0, 1000); }
static void k_snap_distance(struct ctx *c) { set_int(c, &c->cfg->snap_distance, 0, 64); }
static void k_root_line_timeout(struct ctx *c) { set_int(c, &c->cfg->root_line_timeout, 1, 3600); }
static void k_terminal(struct ctx *c) { set_string(c, &c->cfg->terminal); }

static void k_hint_keys(struct ctx *c) {
	char *keys = NULL;
	set_string(c, &keys);
	if (!keys) {
		return;
	}
	if (strlen(keys) < 2) {
		ERR(c, "HintKeys wants at least two letters");
	} else {
		set_str(&c->cfg->hint_keys, keys);
	}
	free(keys);
}

static void k_effects(struct ctx *c) {
	if (!want_n(c, 1, "none") || !node_word(&values(c)->nodes[0], "none")) {
		ERR(c, "Effects: only none (no blur, no shadow, no animation)");
	}
}

static void k_scale(struct ctx *c) {
	if (!want_n(c, 1, "auto or a number from 0.5 to 4")) {
		return;
	}
	const struct twmrc_node *n = &values(c)->nodes[0];
	if (node_word(n, "auto")) {
		c->cfg->scale = 0;
	} else if (n->kind == TWMRC_NUMBER && n->number >= 0.5 && n->number <= 4) {
		c->cfg->scale = n->number;
	} else {
		ERR(c, "Scale wants auto or a number from 0.5 to 4");
	}
}

static void k_focus_policy(struct ctx *c) {
	static const char *const ch[] = {"sloppy", "click", NULL};
	int v = (int)c->cfg->focus_policy;
	set_choice(c, &v, ch);
	c->cfg->focus_policy = (enum twm_focus_policy)v;
}

static void k_placement(struct ctx *c) {
	static const char *const ch[] = {"interactive", "cascade", NULL};
	int v = (int)c->cfg->placement;
	set_choice(c, &v, ch);
	c->cfg->placement = (enum twm_placement)v;
}

static void k_modifier(struct ctx *c) {
	static const char *const ch[] = {"Mod4", "Alt", "Super", "Logo", "Mod1", NULL};
	int v = -1;
	set_choice(c, &v, ch);
	if (v >= 0) {
		c->cfg->modifier = (v == 1 || v == 4) ? TWM_MOD_ALT : TWM_MOD_LOGO;
	}
}

static enum twm_justify justify_of(const struct twmrc_node *n, bool *ok) {
	static const char *const names[] = {"left", "center", "right", "off"};
	for (int i = 0; i < 4; i++) {
		if (node_word(n, names[i])) {
			*ok = true;
			return (enum twm_justify)i;
		}
	}
	*ok = false;
	return TWM_JUSTIFY_LEFT;
}

static void k_squeeze(struct ctx *c) {
	const struct twmrc_node *blk = the_block(c, "\"pattern\" left|center|right|off …");
	if (!blk) {
		return;
	}
	const struct twmrc_node **list;
	size_t n = block_nodes(blk, &list);
	if (n % 2) {
		ERR(c, "SqueezeTitle wants pairs: \"pattern\" left|center|right|off");
		free(list);
		return;
	}
	struct twm_squeeze_rule *rules = NULL;
	size_t nrules = 0;
	for (size_t i = 0; i < n; i += 2) {
		bool ok;
		enum twm_justify j = justify_of(list[i + 1], &ok);
		if (list[i]->kind != TWMRC_STRING || !ok) {
			ERR(c, "SqueezeTitle wants pairs: \"pattern\" left|center|right|off");
			for (size_t k = 0; k < nrules; k++) {
				free(rules[k].pattern);
			}
			free(rules);
			free(list);
			return;
		}
		rules = xgrow(rules, nrules, sizeof(*rules));
		rules[nrules++] = (struct twm_squeeze_rule){xstrdup(list[i]->text), j};
	}
	free(list);
	for (size_t k = 0; k < c->cfg->nsqueeze; k++) {
		free(c->cfg->squeeze[k].pattern);
	}
	free(c->cfg->squeeze);
	c->cfg->squeeze = rules;
	c->cfg->nsqueeze = nrules;
}

static void k_notitle(struct ctx *c) {
	const struct twmrc_node *blk = the_block(c, "\"pattern\" …");
	if (!blk) {
		return;
	}
	const struct twmrc_node **list;
	size_t n = block_nodes(blk, &list);
	for (size_t i = 0; i < n; i++) {
		if (list[i]->kind != TWMRC_STRING) {
			ERR(c, "NoTitle wants quoted patterns");
			free(list);
			return;
		}
	}
	for (size_t k = 0; k < c->cfg->nnotitle; k++) {
		free(c->cfg->notitle[k]);
	}
	free(c->cfg->notitle);
	c->cfg->notitle = calloc(n ? n : 1, sizeof(char *));
	c->cfg->nnotitle = n;
	for (size_t i = 0; i < n; i++) {
		c->cfg->notitle[i] = xstrdup(list[i]->text);
	}
	free(list);
}

static void k_snap(struct ctx *c) {
	if (values(c)->n == 1 && node_word(&values(c)->nodes[0], "off")) {
		c->cfg->snap = 0;
		return;
	}
	const struct twmrc_node *blk = the_block(c, "edges thirds center");
	if (!blk) {
		return;
	}
	const struct twmrc_node **list;
	size_t n = block_nodes(blk, &list);
	uint32_t snap = 0;
	for (size_t i = 0; i < n; i++) {
		if (node_word(list[i], "edges")) {
			snap |= TWM_SNAP_EDGES;
		} else if (node_word(list[i], "thirds")) {
			snap |= TWM_SNAP_THIRDS;
		} else if (node_word(list[i], "center") || node_word(list[i], "centre")) {
			snap |= TWM_SNAP_CENTER;
		} else {
			ERR(c, "Snap knows edges, thirds and center");
			free(list);
			return;
		}
	}
	free(list);
	c->cfg->snap = snap;
}

static bool corner_of(const struct twmrc_node *n, enum twm_corner *out) {
	static const char *const names[] = {"top-left", "top-right", "bottom-left", "bottom-right"};
	for (int i = 0; i < 4; i++) {
		if (node_word(n, names[i])) {
			*out = (enum twm_corner)i;
			return true;
		}
	}
	return false;
}

static void k_iconmgr(struct ctx *c) {
	const struct twmrc_node *blk = the_block(c, "at <corner> sort name|stack|age");
	if (!blk) {
		return;
	}
	const struct twmrc_node **list;
	size_t n = block_nodes(blk, &list);
	for (size_t i = 0; i + 1 < n + 1; i += 2) {
		if (i + 1 >= n) {
			ERR(c, "IconManager wants pairs: at <corner>, sort <order>");
			break;
		}
		if (node_word(list[i], "at")) {
			if (!corner_of(list[i + 1], &c->cfg->iconmgr_corner)) {
				ERR(c, "IconManager at wants top-left, top-right, bottom-left or bottom-right");
				break;
			}
		} else if (node_word(list[i], "sort")) {
			static const char *const s[] = {"name", "stack", "age"};
			bool ok = false;
			for (int k = 0; k < 3; k++) {
				if (node_word(list[i + 1], s[k])) {
					c->cfg->iconmgr_sort = (enum twm_sort)k;
					ok = true;
				}
			}
			if (!ok) {
				ERR(c, "IconManager sort wants name, stack or age");
				break;
			}
		} else {
			ERR(c, "IconManager knows at and sort");
			break;
		}
	}
	free(list);
}

static void k_panner(struct ctx *c) {
	if (values(c)->n == 1 && values(c)->nodes[0].kind == TWMRC_WORD) {
		bool on;
		if (parse_bool(&values(c)->nodes[0], &on)) {
			c->cfg->panner = on;
			return;
		}
	}
	const struct twmrc_node *blk = the_block(c, "desktops N");
	if (!blk) {
		return;
	}
	const struct twmrc_node **list;
	size_t n = block_nodes(blk, &list);
	/* `{ desktops N }`, or `{ desktops N off }` to keep N with no Panner. */
	bool on = true;
	if (n == 3 && !parse_bool(list[2], &on)) {
		n = 0;
	}
	if ((n != 2 && n != 3) || !node_word(list[0], "desktops") ||
			list[1]->kind != TWMRC_NUMBER || list[1]->number < 1 || list[1]->number > 9) {
		ERR(c, "Panner wants { desktops N } with N from 1 to 9 (and on or off)");
	} else {
		c->cfg->desktops = (int)list[1]->number;
		c->cfg->panner = on;
	}
	free(list);
}

static void k_quiet(struct ctx *c) {
	const struct twmrc_node *blk = the_block(c, "desktop numbers");
	if (!blk) {
		return;
	}
	const struct twmrc_node **list;
	size_t n = block_nodes(blk, &list);
	uint32_t quiet = 0;
	for (size_t i = 0; i < n; i++) {
		if (list[i]->kind != TWMRC_NUMBER || list[i]->number < 1 || list[i]->number > 9) {
			ERR(c, "Quiet wants desktop numbers from 1 to 9");
			free(list);
			return;
		}
		quiet |= 1u << ((int)list[i]->number - 1);
	}
	free(list);
	c->cfg->quiet = quiet;
}

static void k_output(struct ctx *c) {
	const struct twmrc_row *v = values(c);
	if (v->n != 2 || v->nodes[0].kind != TWMRC_STRING || v->nodes[1].kind != TWMRC_BLOCK) {
		ERR(c, "Output wants \"name\" { scale N mode WxH[@Hz] position X,Y }");
		return;
	}
	struct twm_output_rule rule = {.name = xstrdup(v->nodes[0].text)};
	const struct twmrc_node **list;
	size_t n = block_nodes(&v->nodes[1], &list);
	bool ok = n % 2 == 0;
	for (size_t i = 0; ok && i < n; i += 2) {
		const struct twmrc_node *val = list[i + 1];
		if (node_word(list[i], "scale")) {
			ok = val->kind == TWMRC_NUMBER && val->number >= 0.5 && val->number <= 4;
			rule.scale = val->number;
		} else if (node_word(list[i], "mode")) {
			double hz = 0;
			ok = val->text && (sscanf(val->text, "%dx%d@%lf", &rule.width, &rule.height, &hz) >= 2) &&
				rule.width > 0 && rule.height > 0;
			rule.refresh_mhz = (int)(hz * 1000);
		} else if (node_word(list[i], "position")) {
			char tail;
			ok = val->text && sscanf(val->text, "%d,%d%c", &rule.x, &rule.y, &tail) == 2;
			rule.has_position = ok;
		} else {
			ok = false;
		}
	}
	free(list);
	if (!ok) {
		ERR(c, "Output wants \"name\" { scale N mode WxH[@Hz] position X,Y }");
		free(rule.name);
		return;
	}
	struct twm_config *cfg = c->cfg;
	for (size_t i = 0; i < cfg->noutputs; i++) {
		if (strcmp(cfg->outputs[i].name, rule.name) == 0) {
			free(cfg->outputs[i].name);
			cfg->outputs[i] = rule;
			return;
		}
	}
	cfg->outputs = xgrow(cfg->outputs, cfg->noutputs, sizeof(*cfg->outputs));
	cfg->outputs[cfg->noutputs++] = rule;
}

static void k_no_default_bindings(struct ctx *c) {
	if (values(c)->n != 0) {
		ERR(c, "NoDefaultBindings takes no value");
		return;
	}
	c->cfg->no_default_bindings = true;
}

static const struct {
	const char *name;
	void (*fn)(struct ctx *c);
} keys[] = {
	{"Accent", k_accent},
	{"BorderWidth", k_border_width},
	{"Effects", k_effects},
	{"FocusDelay", k_focus_delay},
	{"FocusPolicy", k_focus_policy},
	{"Font", k_font},
	{"HintKeys", k_hint_keys},
	{"IconManager", k_iconmgr},
	{"IconsOnRoot", k_icons_on_root},
	{"Menu", k_menu},
	{"Modifier", k_modifier},
	{"NoDefaultBindings", k_no_default_bindings},
	{"NoTitle", k_notitle},
	{"OpaqueMove", k_opaque_move},
	{"Output", k_output},
	{"Panner", k_panner},
	{"Placement", k_placement},
	{"Quiet", k_quiet},
	{"RaiseOnClick", k_raise_on_click},
	{"RememberGeometry", k_remember},
	{"RootLineTimeout", k_root_line_timeout},
	{"RootWeave", k_root_weave},
	{"RootWordmark", k_root_wordmark},
	{"Scale", k_scale},
	{"Snap", k_snap},
	{"SnapDistance", k_snap_distance},
	{"SqueezeTitle", k_squeeze},
	{"Terminal", k_terminal},
	{"Theme", k_theme},
	{"TitleHeight", k_title_height},
	{"TitleHighlight", k_title_highlight},
	{"XWayland", k_xwayland},
	{"Notifications", k_notifications},
	{"TouchpadTap", k_tp_tap},
	{"TouchpadTapDrag", k_tp_tap_drag},
	{"TouchpadNaturalScroll", k_tp_natural},
	{"TouchpadDisableWhileTyping", k_tp_dwt},
	{"TouchpadScroll", k_tp_scroll},
	{"TouchpadClick", k_tp_click},
	{"TouchpadSpeed", k_tp_speed},
	{"TouchpadAccel", k_tp_accel},
	{"Touchscreen", k_touchscreen},
};

/* Keys that may appear more than once (they add rather than replace). */
static bool repeatable(const char *key) {
	return strcmp(key, "Menu") == 0 || strcmp(key, "Output") == 0;
}

static void apply_doc(struct twm_config *cfg, struct twmrc_doc *doc, bool builtin) {
	struct ctx c = {.cfg = cfg, .doc = doc, .builtin = builtin};
	for (size_t i = 0; i < doc->nstmts; i++) {
		const struct twmrc_stmt *st = &doc->stmts[i];
		c.st = st;
		if (st->kind == TWMRC_KEY) {
			add_key(&c);
			continue;
		}
		if (st->kind == TWMRC_BUTTON) {
			add_button(&c);
			continue;
		}
		bool found = false;
		for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
			if (strcmp(keys[k].name, st->key) == 0) {
				found = true;
				keys[k].fn(&c);
				break;
			}
		}
		if (!found) {
			/* B7: ".twmrc line 14: unknown key SqueezTitle" */
			ERR(&c, "unknown key %s", st->key);
			continue;
		}
		if (!builtin && !repeatable(st->key)) {
			for (size_t j = 0; j < i; j++) {
				if (doc->stmts[j].kind == TWMRC_SETTING &&
						strcmp(doc->stmts[j].key, st->key) == 0) {
					WARN(&c, "%s is set twice (line %d); this one wins",
						st->key, doc->stmts[j].line);
					break;
				}
			}
		}
	}
}

static void drop_builtin_bindings(struct twm_config *cfg) {
	size_t k = 0;
	for (size_t i = 0; i < cfg->nkeys; i++) {
		if (cfg->keys[i].builtin) {
			free(cfg->keys[i].spec);
			func_free(&cfg->keys[i].func);
		} else {
			cfg->keys[k++] = cfg->keys[i];
		}
	}
	cfg->nkeys = k;
	k = 0;
	for (size_t i = 0; i < cfg->nbuttons; i++) {
		if (cfg->buttons[i].builtin) {
			func_free(&cfg->buttons[i].func);
		} else {
			cfg->buttons[k++] = cfg->buttons[i];
		}
	}
	cfg->nbuttons = k;
}

struct twm_config *config_build(const char *src, size_t len, struct twmrc_doc **doc_out) {
	struct twm_config *cfg = calloc(1, sizeof(*cfg));
	if (!cfg) {
		abort();
	}
	struct twmrc_doc *defaults = twmrc_parse(config_default_twmrc, strlen(config_default_twmrc));
	apply_doc(cfg, defaults, true);
	if (defaults->ndiags) {
		/* A bug in the built-in text; fail loudly in tests. */
		fprintf(stderr, "twm: built-in defaults line %d: %s\n",
			defaults->diags[0].line, defaults->diags[0].message);
		abort();
	}
	twmrc_free(defaults);

	struct twmrc_doc *doc = twmrc_parse(src ? src : "", src ? len : 0);
	apply_doc(cfg, doc, false);
	if (cfg->no_default_bindings) {
		drop_builtin_bindings(cfg);
	}
	if (doc_out) {
		*doc_out = doc;
	} else {
		twmrc_free(doc);
	}
	return cfg;
}

void config_free(struct twm_config *cfg) {
	if (!cfg) {
		return;
	}
	for (size_t i = 0; i < cfg->noutputs; i++) {
		free(cfg->outputs[i].name);
	}
	free(cfg->outputs);
	for (size_t i = 0; i < cfg->nsqueeze; i++) {
		free(cfg->squeeze[i].pattern);
	}
	free(cfg->squeeze);
	for (size_t i = 0; i < cfg->nnotitle; i++) {
		free(cfg->notitle[i]);
	}
	free(cfg->notitle);
	free(cfg->terminal);
	free(cfg->hint_keys);
	for (size_t i = 0; i < cfg->nbuttons; i++) {
		func_free(&cfg->buttons[i].func);
	}
	free(cfg->buttons);
	for (size_t i = 0; i < cfg->nkeys; i++) {
		free(cfg->keys[i].spec);
		func_free(&cfg->keys[i].func);
	}
	free(cfg->keys);
	for (size_t i = 0; i < cfg->nmenus; i++) {
		menu_free(&cfg->menus[i]);
	}
	free(cfg->menus);
	free(cfg);
}

/* --- queries --- */

uint32_t config_resolve_mods(const struct twm_config *cfg, uint32_t mods) {
	if (mods & TWM_MOD_M) {
		mods = (mods & ~TWM_MOD_M) | cfg->modifier;
	}
	return mods;
}

/* Caps Lock and Num Lock never change what a binding means. */
#define IGNORED_MODS (TWM_MOD_CAPS | TWM_MOD_MOD2)

const struct twm_key_binding *config_match_key(const struct twm_config *cfg,
		uint32_t mods, xkb_keysym_t sym) {
	mods &= ~IGNORED_MODS;
	sym = xkb_keysym_to_lower(sym);
	for (size_t i = 0; i < cfg->nkeys; i++) {
		const struct twm_key_binding *k = &cfg->keys[i];
		if (k->sym == sym && config_resolve_mods(cfg, k->mods) == mods) {
			return k->func.kind == TWM_F_NOP ? NULL : k;
		}
	}
	return NULL;
}

const struct twm_button_binding *config_match_button(const struct twm_config *cfg,
		uint32_t button, uint32_t mods, enum twm_context ctx) {
	mods &= ~IGNORED_MODS;
	for (size_t i = 0; i < cfg->nbuttons; i++) {
		const struct twm_button_binding *b = &cfg->buttons[i];
		if (b->button == button && b->context == ctx &&
				config_resolve_mods(cfg, b->mods) == mods) {
			return b->func.kind == TWM_F_NOP ? NULL : b;
		}
	}
	return NULL;
}

const struct twm_menu *config_menu(const struct twm_config *cfg, const char *name) {
	for (size_t i = 0; i < cfg->nmenus; i++) {
		if (strcmp(cfg->menus[i].name, name) == 0) {
			return &cfg->menus[i];
		}
	}
	return NULL;
}

static bool func_equal(const struct twm_func *a, const struct twm_func *b) {
	if (a->kind != b->kind || a->num != b->num) {
		return false;
	}
	if (!a->arg || !b->arg) {
		return a->arg == b->arg;
	}
	return strcmp(a->arg, b->arg) == 0;
}

const struct twm_key_binding *config_key_for(const struct twm_config *cfg,
		const struct twm_func *func) {
	for (size_t i = 0; i < cfg->nkeys; i++) {
		if (func_equal(&cfg->keys[i].func, func)) {
			return &cfg->keys[i];
		}
	}
	return NULL;
}

const struct twm_button_binding *config_button_for(const struct twm_config *cfg,
		const struct twm_func *func, enum twm_context ctx) {
	for (size_t i = 0; i < cfg->nbuttons; i++) {
		if (cfg->buttons[i].context == ctx && func_equal(&cfg->buttons[i].func, func)) {
			return &cfg->buttons[i];
		}
	}
	return NULL;
}

static bool matches(const char *pattern, const char *app_id, const char *title) {
	return (app_id && fnmatch(pattern, app_id, 0) == 0) ||
		(title && fnmatch(pattern, title, 0) == 0);
}

enum twm_justify config_justify(const struct twm_config *cfg, const char *app_id,
		const char *title) {
	for (size_t i = 0; i < cfg->nsqueeze; i++) {
		if (matches(cfg->squeeze[i].pattern, app_id, title)) {
			return cfg->squeeze[i].justify;
		}
	}
	return TWM_JUSTIFY_LEFT;
}

bool config_notitle(const struct twm_config *cfg, const char *app_id, const char *title) {
	for (size_t i = 0; i < cfg->nnotitle; i++) {
		if (matches(cfg->notitle[i], app_id, title)) {
			return true;
		}
	}
	return false;
}
