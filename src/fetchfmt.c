#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fetchfmt.h"
#include "history.h"
#include "theme.h"

void fetch_init(struct fetch_facts *f) {
	memset(f, 0, sizeof(*f));
	f->uptime = -1;
	f->twmrc_lines = -1;
	struct twm_theme t;
	theme_init_defaults(&t);
	uint32_t c[FC_COUNT] = {
		t.ground, t.surface, t.graphite, t.hairline, t.hairline_strong, t.ink, t.ink_2,
		t.muted, t.dim, t.accent, t.on_accent, t.ok, t.alert,
	};
	memcpy(f->colors, c, sizeof(c));
}

static const char *const color_names[FC_COUNT] = {
	"ground", "surface", "graphite", "hairline", "hairline_strong", "ink", "ink_2",
	"muted", "dim", "accent", "on_accent", "ok", "alert",
};

void fetch_parse_info(const char *reply, struct fetch_facts *f) {
	for (const char *l = reply; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		char line[512];
		size_t n = strcspn(l, "\n");
		snprintf(line, sizeof(line), "%.*s", (int)(n < sizeof(line) ? n : sizeof(line) - 1), l);
		char *fld[6] = {0};
		int nf = 0;
		for (char *tok = line; tok && nf < 6; nf++) {
			fld[nf] = tok;
			char *tab = strchr(tok, '\t');
			tok = tab ? (*tab = '\0', tab + 1) : NULL;
		}
		if (nf < 2) {
			continue;
		}
		const char *k = fld[0];
		if (strcmp(k, "version") == 0) {
			f->wm = true;
			snprintf(f->version, sizeof(f->version), "%s", fld[1]);
		} else if (strcmp(k, "xwayland") == 0) {
			f->xwayland = strcmp(fld[1], "on") == 0;
		} else if (strcmp(k, "frames") == 0 && nf >= 4) {
			f->open = atoi(fld[1]);
			f->iconified = atoi(fld[2]);
			f->groups = atoi(fld[3]);
		} else if (strcmp(k, "desktops") == 0 && nf >= 4) {
			f->desktops = atoi(fld[1]);
			f->desktop = atoi(fld[2]);
			f->quiet = atoi(fld[3]);
		} else if (strcmp(k, "output") == 0 && nf >= 5 && f->noutputs < FETCH_MAX_OUTPUTS) {
			int i = f->noutputs++;
			snprintf(f->outputs[i].name, sizeof(f->outputs[i].name), "%s", fld[1]);
			f->outputs[i].width = atoi(fld[2]);
			f->outputs[i].height = atoi(fld[3]);
			f->outputs[i].scale = strtod(fld[4], NULL);
		} else if (strcmp(k, "focus") == 0 && nf >= 3) {
			snprintf(f->focus, sizeof(f->focus), "%s", fld[1]);
			f->focus_delay = atoi(fld[2]);
		} else if (strcmp(k, "font") == 0 && nf >= 3) {
			snprintf(f->font, sizeof(f->font), "%s", fld[1]);
			f->font_size = atoi(fld[2]);
		} else if (strcmp(k, "twmrc") == 0 && nf >= 5) {
			snprintf(f->twmrc_path, sizeof(f->twmrc_path), "%s", fld[1]);
			f->twmrc_lines = atoi(fld[2]);
			snprintf(f->twmrc_state, sizeof(f->twmrc_state), "%s", fld[3]);
			f->applied = strtoll(fld[4], NULL, 10);
		} else if (strcmp(k, "color") == 0 && nf >= 3) {
			for (int c = 0; c < FC_COUNT; c++) {
				if (strcmp(fld[1], color_names[c]) == 0) {
					f->colors[c] = (uint32_t)strtoul(fld[2], NULL, 16) & 0xFFFFFF;
				}
			}
		}
	}
}

/* --- a growing buffer --- */

struct sb {
	char *s;
	size_t len, o;
};

static void add(struct sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void add(struct sb *b, const char *fmt, ...) {
	if (b->o >= b->len) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b->s + b->o, b->len - b->o, fmt, ap);
	va_end(ap);
	if (n > 0) {
		b->o += (size_t)n;
		if (b->o >= b->len) {
			b->o = b->len - 1;
		}
	}
}

/* --- the facts as words --- */

static void short_version(const char *v, char *out, size_t len) {
	/* "0.1.0" -> "0.1" */
	const char *dot = strchr(v, '.');
	const char *dot2 = dot ? strchr(dot + 1, '.') : NULL;
	snprintf(out, len, "%.*s", dot2 ? (int)(dot2 - v) : (int)strlen(v), v);
}

static void scale_str(double s, char *out, size_t len) {
	snprintf(out, len, "@%g", s);
}

static void memory_str(const struct sys_mem *m, const char *unit, char *out, size_t len) {
	double gib = 1024.0 * 1024 * 1024;
	snprintf(out, len, "%.1f / %.0f%s", (double)m->used / gib, (double)m->total / gib, unit);
}

/* "2h ago", "just now" */
static void applied_str(const struct fetch_facts *f, char *out, size_t len) {
	char age[16];
	history_age(f->now, (time_t)f->applied, age, sizeof(age));
	if (strcmp(age, "now") == 0) {
		snprintf(out, len, "reloaded just now");
	} else {
		snprintf(out, len, "reloaded %s ago", age);
	}
}

/* --- colors --- */

static void fg(struct sb *b, bool color, uint32_t rgb) {
	if (color) {
		add(b, "\x1b[38;2;%u;%u;%um", rgb >> 16 & 0xff, rgb >> 8 & 0xff, rgb & 0xff);
	}
}

static void bgc(struct sb *b, bool color, uint32_t rgb) {
	if (color) {
		add(b, "\x1b[48;2;%u;%u;%um", rgb >> 16 & 0xff, rgb >> 8 & 0xff, rgb & 0xff);
	}
}

static void bold(struct sb *b, bool color) {
	if (color) {
		add(b, "\x1b[1m");
	}
}

static void reset(struct sb *b, bool color) {
	if (color) {
		add(b, "\x1b[0m");
	}
}

/* --- B9's logo: ten lines, 25 columns --- */

#define LOGO_LINES 10
#define LOGO_WIDTH 25

static void logo_line(struct sb *b, const struct fetch_facts *f, bool color, int i) {
	const uint32_t *c = f->colors;
	static const char *const letters[3] = {
		"   ╺┳╸ ╻ ╻ ┏┳┓         ",
		"    ┃  ┃╻┃ ┃┃┃         ",
		"    ╹  ┗┻┛ ╹╹╹         ",
	};
	switch (i) {
	case 0: /* the squeezed tab */
		bgc(b, color, c[FC_ACCENT]);
		fg(b, color, c[FC_ON_ACCENT]);
		bold(b, color);
		add(b, " ■ tom's wm ░░░░ ");
		reset(b, color);
		add(b, "%*s", LOGO_WIDTH - 17, "");
		return;
	case 1:
	case 9:
		fg(b, color, c[FC_ACCENT]);
		add(b, i == 1 ? "┌───────────────────────┐" : "└───────────────────────┘");
		reset(b, color);
		return;
	default:
		break;
	}
	fg(b, color, c[FC_ACCENT]);
	add(b, "│");
	reset(b, color);
	if (i >= 3 && i <= 5) {
		add(b, "%s", letters[i - 3]);
	} else if (i == 7) {
		add(b, "   ");
		fg(b, color, c[FC_DIM]);
		add(b, "since 1987");
		reset(b, color);
		add(b, "          ");
	} else {
		add(b, "%23s", "");
	}
	fg(b, color, c[FC_ACCENT]);
	add(b, "│");
	reset(b, color);
}

/* --- the facts column --- */

#define MAX_ROWS 20

struct row {
	char text[600]; /* already colored */
};

static void label(struct sb *b, const struct fetch_facts *f, bool color, const char *name) {
	fg(b, color, f->colors[FC_ACCENT]);
	add(b, "%-8s", name);
	reset(b, color);
	add(b, "  ");
}

static int build_rows(const struct fetch_facts *f, bool color, struct row *rows) {
	int n = 0;
	const uint32_t *c = f->colors;
	struct sb b;
#define ROW() do { b = (struct sb){rows[n].text, sizeof(rows[n].text), 0}; rows[n].text[0] = '\0'; } while (0)
#define NEXT() do { n++; } while (0)

	/* user@host */
	ROW();
	fg(&b, color, c[FC_ACCENT]);
	bold(&b, color);
	add(&b, "%s", f->user);
	reset(&b, color);
	add(&b, "@");
	bold(&b, color);
	add(&b, "%s", f->host);
	reset(&b, color);
	NEXT();
	ROW();
	fg(&b, color, c[FC_HAIRLINE_STRONG]);
	size_t w = strlen(f->user) + 1 + strlen(f->host);
	for (size_t i = 0; i < w; i++) {
		add(&b, "─");
	}
	reset(&b, color);
	NEXT();

	ROW();
	label(&b, f, color, "os");
	add(&b, "%s", f->os);
	NEXT();
	char up[48];
	sys_fmt_uptime(f->uptime, up, sizeof(up));
	ROW();
	label(&b, f, color, "uptime");
	add(&b, "%s", up);
	NEXT();

	if (!f->wm) {
		ROW();
		label(&b, f, color, "wm");
		fg(&b, color, c[FC_DIM]);
		add(&b, "not running");
		reset(&b, color);
		NEXT();
	} else {
		char v[32];
		short_version(f->version, v, sizeof(v));
		ROW();
		label(&b, f, color, "wm");
		add(&b, "Tom's WM %s · wayland · xwayland %s", v, f->xwayland ? "on" : "off");
		NEXT();
		ROW();
		label(&b, f, color, "frames");
		add(&b, "%d open", f->open);
		if (f->iconified) {
			add(&b, " · %d iconified", f->iconified);
		}
		if (f->groups) {
			add(&b, " · %d tab group%s", f->groups, f->groups == 1 ? "" : "s");
		}
		NEXT();
		ROW();
		label(&b, f, color, "desktops");
		add(&b, "%d · on %d", f->desktops, f->desktop);
		if (f->quiet) {
			add(&b, " · %d quiet", f->quiet);
		}
		NEXT();
		if (f->noutputs) {
			ROW();
			label(&b, f, color, "outputs");
			for (int i = 0; i < f->noutputs; i++) {
				char sc[16];
				scale_str(f->outputs[i].scale, sc, sizeof(sc));
				add(&b, "%s%s %d×%d %s", i ? " · " : "", f->outputs[i].name,
					f->outputs[i].width, f->outputs[i].height, sc);
			}
			NEXT();
		}
		ROW();
		label(&b, f, color, "focus");
		if (strcmp(f->focus, "sloppy") == 0) {
			add(&b, "sloppy · intent %d ms", f->focus_delay);
		} else {
			add(&b, "%s", f->focus);
		}
		NEXT();
		ROW();
		label(&b, f, color, "font");
		add(&b, "%s %d", f->font, f->font_size);
		NEXT();
	}
	if (f->have_pool) {
		char fr[16];
		sys_fmt_bytes(f->pool.free, fr, sizeof(fr));
		ROW();
		label(&b, f, color, "pool");
		add(&b, "%s%s%s · %s free · ", f->pool.name, f->pool.layout[0] ? " " : "",
			f->pool.layout, fr);
		fg(&b, color, strcmp(f->pool.health, "ONLINE") == 0 ? c[FC_OK] : c[FC_ALERT]);
		add(&b, "%s", f->pool.health);
		reset(&b, color);
		NEXT();
	}
	if (f->have_mem) {
		char m[48];
		memory_str(&f->mem, " GiB", m, sizeof(m));
		ROW();
		label(&b, f, color, "memory");
		add(&b, "%s", m);
		NEXT();
	}
	if (f->wm) {
		ROW();
		label(&b, f, color, ".twmrc");
		if (f->twmrc_lines < 0) {
			add(&b, "no file · built-in defaults");
		} else {
			char ap[48];
			applied_str(f, ap, sizeof(ap));
			add(&b, "%d lines · ", f->twmrc_lines);
			bool ok = strcmp(f->twmrc_state, "ok") == 0;
			fg(&b, color, ok ? c[FC_OK] : c[FC_ALERT]);
			add(&b, ok ? "check ok" : "check failed");
			reset(&b, color);
			add(&b, " · %s", ap);
		}
		NEXT();
	}
	if (color) {
		ROW(); /* blank */
		NEXT();
		ROW(); /* the palette strip, from the theme's own tokens */
		fg(&b, color, c[FC_HAIRLINE]);
		bgc(&b, color, c[FC_GROUND]);
		add(&b, "▕  ▏");
		reset(&b, color);
		static const enum fetch_color strip[] = {
			FC_SURFACE, FC_GRAPHITE, FC_DIM, FC_INK, FC_ACCENT, FC_OK, FC_ALERT,
		};
		for (size_t i = 0; i < sizeof(strip) / sizeof(strip[0]); i++) {
			bgc(&b, color, c[strip[i]]);
			add(&b, "    ");
		}
		reset(&b, color);
		NEXT();
	}
#undef ROW
#undef NEXT
	return n;
}

void fetch_render(const struct fetch_facts *f, bool color, bool logo, char *out, size_t len) {
	struct row rows[MAX_ROWS];
	int nrows = build_rows(f, color, rows);
	struct sb b = {out, len, 0};
	out[0] = '\0';
	int lines = logo && nrows < LOGO_LINES ? LOGO_LINES : nrows;
	for (int i = 0; i < lines; i++) {
		if (logo) {
			if (i < LOGO_LINES) {
				logo_line(&b, f, color, i);
			} else {
				add(&b, "%*s", LOGO_WIDTH, "");
			}
			add(&b, "    ");
		}
		if (i < nrows) {
			add(&b, "%s", rows[i].text);
		}
		/* no trailing blanks */
		while (b.o > 0 && out[b.o - 1] == ' ') {
			out[--b.o] = '\0';
		}
		add(&b, "\n");
	}
	if (f->shot[0]) {
		add(&b, "\n");
		fg(&b, color, f->colors[FC_DIM]);
		add(&b, "%s", f->shot);
		reset(&b, color);
		add(&b, "\n");
	}
}

/* --- JSON --- */

static void jstr(struct sb *b, const char *s) {
	add(b, "\"");
	for (; *s; s++) {
		unsigned char ch = (unsigned char)*s;
		if (ch == '"' || ch == '\\') {
			add(b, "\\%c", ch);
		} else if (ch < 0x20) {
			add(b, "\\u%04x", ch);
		} else {
			add(b, "%c", ch);
		}
	}
	add(b, "\"");
}

void fetch_json(const struct fetch_facts *f, char *out, size_t len) {
	struct sb b = {out, len, 0};
	out[0] = '\0';
	add(&b, "{\"user\":");
	jstr(&b, f->user);
	add(&b, ",\"host\":");
	jstr(&b, f->host);
	add(&b, ",\"os\":");
	jstr(&b, f->os);
	add(&b, ",\"uptime_s\":%ld", f->uptime);
	if (f->wm) {
		add(&b, ",\"wm\":{\"name\":\"Tom's WM\",\"version\":");
		jstr(&b, f->version);
		add(&b, ",\"xwayland\":%s}", f->xwayland ? "true" : "false");
		add(&b, ",\"frames\":{\"open\":%d,\"iconified\":%d,\"groups\":%d}",
			f->open, f->iconified, f->groups);
		add(&b, ",\"desktops\":{\"count\":%d,\"current\":%d,\"quiet\":%d}",
			f->desktops, f->desktop, f->quiet);
		add(&b, ",\"outputs\":[");
		for (int i = 0; i < f->noutputs; i++) {
			add(&b, "%s{\"name\":", i ? "," : "");
			jstr(&b, f->outputs[i].name);
			add(&b, ",\"width\":%d,\"height\":%d,\"scale\":%g}", f->outputs[i].width,
				f->outputs[i].height, f->outputs[i].scale);
		}
		add(&b, "],\"focus\":{\"policy\":");
		jstr(&b, f->focus);
		add(&b, ",\"delay_ms\":%d},\"font\":{\"family\":", f->focus_delay);
		jstr(&b, f->font);
		add(&b, ",\"size\":%d},\"twmrc\":{\"path\":", f->font_size);
		jstr(&b, f->twmrc_path);
		add(&b, ",\"lines\":%d,\"check\":", f->twmrc_lines);
		jstr(&b, f->twmrc_state);
		add(&b, ",\"applied\":%lld}", f->applied);
	} else {
		add(&b, ",\"wm\":null");
	}
	if (f->have_pool) {
		add(&b, ",\"pool\":{\"name\":");
		jstr(&b, f->pool.name);
		add(&b, ",\"layout\":");
		jstr(&b, f->pool.layout);
		add(&b, ",\"health\":");
		jstr(&b, f->pool.health);
		add(&b, ",\"size\":%llu,\"alloc\":%llu,\"free\":%llu}",
			(unsigned long long)f->pool.size, (unsigned long long)f->pool.alloc,
			(unsigned long long)f->pool.free);
	}
	if (f->have_mem) {
		add(&b, ",\"memory\":{\"used\":%llu,\"total\":%llu}",
			(unsigned long long)f->mem.used, (unsigned long long)f->mem.total);
	}
	if (f->shot[0]) {
		add(&b, ",\"shot\":");
		jstr(&b, f->shot);
	}
	add(&b, "}\n");
}

void fetch_short(const struct fetch_facts *f, char *out, size_t len) {
	struct sb b = {out, len, 0};
	out[0] = '\0';
	char up[32];
	sys_fmt_uptime_short(f->uptime, up, sizeof(up));
	add(&b, "%s@%s", f->user, f->host);
	if (f->wm) {
		char v[32];
		short_version(f->version, v, sizeof(v));
		add(&b, " · twm %s · %d frame%s · desk %d/%d", v, f->open, f->open == 1 ? "" : "s",
			f->desktop, f->desktops);
	}
	add(&b, " · up %s\n", up);
}
