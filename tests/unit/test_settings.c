/* Unit tests for the settings registry (CFG-3, CFG-9, CFG-10, KEY). */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "settings.h"

static char *slurp(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	char *buf = malloc(1 << 16);
	*len = fread(buf, 1, (1 << 16) - 1, f);
	fclose(f);
	buf[*len] = '\0';
	return buf;
}

static struct twm_config *build(const char *src, struct twmrc_doc **doc) {
	return config_build(src, strlen(src), doc);
}

static int has_error(struct twmrc_doc *doc, int line, const char *text) {
	for (size_t i = 0; i < doc->ndiags; i++) {
		if (doc->diags[i].severity == TWMRC_ERROR && doc->diags[i].line == line &&
				strstr(doc->diags[i].message, text)) {
			return 1;
		}
	}
	return 0;
}

int main(int argc, char *argv[]) {
	struct twmrc_doc *doc;
	struct twm_config *cfg;

	/* Defaults: DESIGN §5 and the KEY table. */
	cfg = config_build(NULL, 0, &doc);
	CHECK_EQ("CFG-1", doc->ndiags, 0);
	CHECK_EQ("§5", cfg->accent, 0xFFB000);
	CHECK_EQ("§5", cfg->title_height, 26);
	CHECK_EQ("§5", cfg->font_size, 12);
	CHECK_EQ("§5", cfg->focus_delay, 80);
	CHECK_EQ("§5", cfg->desktops, 4);
	CHECK_EQ("GEN-8", cfg->modifier, TWM_MOD_LOGO);
	CHECK("§5", cfg->snap == (TWM_SNAP_EDGES | TWM_SNAP_THIRDS));
	CHECK("§5", strcmp(cfg->terminal, "foot") == 0);
	const struct twm_key_binding *k = config_match_key(cfg, TWM_MOD_LOGO, XKB_KEY_r);
	CHECK("KEY", k && k->func.kind == TWM_F_RESIZE);
	k = config_match_key(cfg, TWM_MOD_LOGO | TWM_MOD_SHIFT, XKB_KEY_q);
	CHECK("KEY", k && k->func.kind == TWM_F_QUIT);
	k = config_match_key(cfg, TWM_MOD_LOGO, XKB_KEY_semicolon);
	CHECK("KEY", k && k->func.kind == TWM_F_HINTS);
	k = config_match_key(cfg, TWM_MOD_LOGO | TWM_MOD_SHIFT | TWM_MOD_CAPS, XKB_KEY_3);
	CHECK("KEY", k && k->func.kind == TWM_F_SEND && k->func.num == 3);
	CHECK("KEY", config_match_key(cfg, 0, XKB_KEY_r) == NULL);
	const struct twm_button_binding *b = config_match_button(cfg, 1, 0, TWM_CTX_TITLE);
	CHECK("FRM-12", b && b->func.kind == TWM_F_RAISEMOVE);
	b = config_match_button(cfg, 3, TWM_MOD_LOGO, TWM_CTX_WINDOW);
	CHECK("OUT-9", b && b->func.kind == TWM_F_RESIZE);
	CHECK("FOC-5", config_match_button(cfg, 1, 0, TWM_CTX_WINDOW) == NULL);
	const struct twm_menu *menu = config_menu(cfg, "main");
	CHECK("MNU-4", menu && menu->nitems == 19 && strcmp(menu->items[3].label, "run…") == 0 && strcmp(menu->items[8].label, "zoom") == 0 && strcmp(menu->items[9].label, "fullscreen") == 0 && strcmp(menu->items[12].label, "close") == 0 && strcmp(menu->items[13].label, "kill") == 0 && strcmp(menu->items[18].label, "exit") == 0);
	struct twm_func term = {.kind = TWM_F_TERMINAL};
	k = config_key_for(cfg, &term);
	CHECK("MNU-3", k && strcmp(k->spec, "M-Return") == 0);
	config_free(cfg);
	twmrc_free(doc);

	/* CFG-9: B0's sample, 0 errors and 0 warnings. */
	size_t len = 0;
	char *b0 = slurp(argc > 1 ? argv[1] : "b0.twmrc", &len);
	cfg = config_build(b0, len, &doc);
	CHECK_EQ("CFG-9", doc->ndiags, 0);
	CHECK_EQ("CFG-9", cfg->font_size, 11);
	CHECK("CFG-9", strcmp(cfg->font, "IBM Plex Mono") == 0);
	k = config_match_key(cfg, TWM_MOD_LOGO, XKB_KEY_Return);
	CHECK("CFG-9", k && k->func.kind == TWM_F_EXEC && strcmp(k->func.arg, "foot") == 0);
	b = config_match_button(cfg, 3, 0, TWM_CTX_TITLE);
	CHECK("CFG-9", b && b->func.kind == TWM_F_SQUEEZE);
	config_free(cfg);
	twmrc_free(doc);

	/* CFG-9: the B8 diff keys are valid. */
	cfg = build("TitleHeight   28\nSnap          { edges thirds }\nTitleHighlight on\n", &doc);
	CHECK_EQ("CFG-9", doc->ndiags, 0);
	CHECK_EQ("CFG-9", cfg->title_height, 28);
	config_free(cfg);
	twmrc_free(doc);

	/* CFG-9 (B7): a misspelled key at line 14. */
	char b7[1024] = "";
	for (int i = 1; i <= 13; i++) {
		strcat(b7, i % 3 ? "\n" : "# comment\n");
	}
	strcat(b7, "SqueezTitle { \"*\" left }\n");
	cfg = build(b7, &doc);
	CHECK("CFG-9", has_error(doc, 14, "unknown key SqueezTitle"));
	CHECK_EQ("CFG-9", twmrc_count(doc, TWMRC_ERROR), 1);
	config_free(cfg);
	twmrc_free(doc);

	/* CFG-3: wrong values are errors, with their lines. */
	cfg = build("TitleHeight 40\nAccent red\nFocusPolicy nervous\nEffects wobble\n"
		"\"M-Retrun\" : f.move\n\"M-x\" : f.bogus\nButton9 = root : f.move\n"
		"Button1 = desk : f.move\nTheme \"aqua\"\nScale 9\n", &doc);
	CHECK("CFG-3", has_error(doc, 1, "TitleHeight wants a whole number from 22 to 34"));
	CHECK("CFG-3", has_error(doc, 2, "Accent wants a color"));
	CHECK("CFG-3", has_error(doc, 3, "sloppy or click"));
	CHECK("GEN-1", has_error(doc, 4, "Effects"));
	CHECK("CFG-3", has_error(doc, 5, "unknown key name \"M-Retrun\""));
	CHECK("CFG-3", has_error(doc, 6, "unknown function f.bogus"));
	CHECK("CFG-3", has_error(doc, 7, "unknown key Button9"));
	CHECK("CFG-3", has_error(doc, 8, "context is one of"));
	CHECK("CFG-3", has_error(doc, 9, "phosphor"));
	CHECK("CFG-3", has_error(doc, 10, "Scale"));
	CHECK_EQ("CFG-3", twmrc_count(doc, TWMRC_ERROR), 10);
	config_free(cfg);
	twmrc_free(doc);

	/* CFG-10: warnings don't make a config invalid. */
	cfg = build("Accent #7FD1AE\nAccent #8AB4FF\n", &doc);
	CHECK_EQ("CFG-10", twmrc_count(doc, TWMRC_ERROR), 0);
	CHECK_EQ("CFG-10", twmrc_count(doc, TWMRC_WARNING), 1);
	CHECK_EQ("CFG-10", cfg->accent, 0x8AB4FF);
	config_free(cfg);
	twmrc_free(doc);

	/* GEN-8: Modifier Alt changes what M- means, wherever it is set. */
	cfg = build("\"M-k\" : f.raise\nModifier Alt\n", &doc);
	CHECK_EQ("GEN-8", doc->ndiags, 0);
	k = config_match_key(cfg, TWM_MOD_ALT, XKB_KEY_k);
	CHECK("GEN-8", k && k->func.kind == TWM_F_RAISE);
	CHECK("GEN-8", config_match_key(cfg, TWM_MOD_LOGO, XKB_KEY_k) == NULL);
	config_free(cfg);
	twmrc_free(doc);

	/* User bindings replace built-in ones; f.nop unbinds; NoDefaultBindings. */
	cfg = build("\"M-r\" : f.nop\n", &doc);
	CHECK("KEY", config_match_key(cfg, TWM_MOD_LOGO, XKB_KEY_r) == NULL);
	config_free(cfg);
	twmrc_free(doc);
	cfg = build("NoDefaultBindings\n\"M-x\" : f.quit\n", &doc);
	CHECK_EQ("KEY", cfg->nkeys, 1);
	CHECK_EQ("KEY", cfg->nbuttons, 0);
	config_free(cfg);
	twmrc_free(doc);

	/* FRM-4: SqueezeTitle patterns; FRM-15: NoTitle. */
	cfg = build("SqueezeTitle { \"xclock\" right \"*term*\" center\n \"*\" left }\n"
		"NoTitle { \"mpv\" }\n", &doc);
	CHECK_EQ("FRM-4", doc->ndiags, 0);
	CHECK_EQ("FRM-4", config_justify(cfg, "xclock", "xclock"), TWM_JUSTIFY_RIGHT);
	CHECK_EQ("FRM-4", config_justify(cfg, "foot", "xterm — ~/.twmrc"), TWM_JUSTIFY_CENTER);
	CHECK_EQ("FRM-4", config_justify(cfg, "foot", "zpool"), TWM_JUSTIFY_LEFT);
	CHECK("FRM-15", config_notitle(cfg, "mpv", "video") && !config_notitle(cfg, "foot", "x"));
	config_free(cfg);
	twmrc_free(doc);

	/* Outputs, menus, quiet desktops. */
	cfg = build("Output \"DP-1\" { scale 1.25 mode 2560x1440@59.95 position 0,0 }\n"
		"Menu \"tools\" {\n \"top\" !\"foot -e htop\"\n \"\" f.separator\n}\nQuiet { 3 }\n", &doc);
	CHECK_EQ("GEN-3", doc->ndiags, 0);
	CHECK("GEN-3", cfg->noutputs == 1 && cfg->outputs[0].scale == 1.25 &&
		cfg->outputs[0].width == 2560 && cfg->outputs[0].refresh_mhz == 59950 &&
		cfg->outputs[0].has_position);
	menu = config_menu(cfg, "tools");
	CHECK("MNU-7", menu && menu->nitems == 2 && menu->items[0].func.kind == TWM_F_EXEC);
	CHECK("URG-3", cfg->quiet == 4u);
	config_free(cfg);
	twmrc_free(doc);
	free(b0);
	return UNIT_RESULT();
}
