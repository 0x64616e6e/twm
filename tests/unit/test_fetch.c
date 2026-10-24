/* Unit tests for twmfetch (FET-*) and the facts behind it (CARD-2). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "fetchfmt.h"
#include "sysinfo.h"

int main(void) {
	/* --- parsers --- */
	struct sys_mem m;
	CHECK("FET-3", sys_parse_meminfo("MemTotal:       65323516 kB\nMemFree: 1 kB\n"
		"MemAvailable:   41085188 kB\n", &m) && m.total == 65323516ull * 1024 &&
		m.used == (65323516ull - 41085188ull) * 1024);
	CHECK("FET-3", !sys_parse_meminfo("MemTotal: 10 kB\n", &m));
	char os[64];
	CHECK("FET-3", sys_parse_os_release("NAME=\"Debian\"\nPRETTY_NAME=\"Debian GNU/Linux 13 (trixie)\"\n",
		os, sizeof(os)) && strcmp(os, "Debian GNU/Linux 13 (trixie)") == 0);
	struct sys_pool p;
	CHECK("FET-3", sys_parse_zpool("tank\t4000\t1000\t3000\tONLINE\n"
		"\traidz2-0\t4000\t1000\t3000\tONLINE\n\t\tda0\t-\t-\t-\tONLINE\n"
		"boot\t10\t1\t9\tONLINE\n", &p) && strcmp(p.name, "tank") == 0 &&
		strcmp(p.layout, "raidz2") == 0 && p.free == 3000 && strcmp(p.health, "ONLINE") == 0);
	CHECK("FET-3", sys_parse_zpool("tank\t10\t1\t9\tDEGRADED\n\tsdb\t10\t1\t9\tONLINE\n", &p) &&
		p.layout[0] == '\0' && strcmp(p.health, "DEGRADED") == 0); /* a bare disk: no layout */
	CHECK("FET-3", !sys_parse_zpool("", &p));

	char s[64];
	sys_fmt_uptime(12 * 86400 + 4 * 3600 + 31 * 60 + 5, s, sizeof(s));
	CHECK("FET-1", strcmp(s, "12d 4h 31m") == 0);
	sys_fmt_uptime(31 * 60, s, sizeof(s));
	CHECK("FET-1", strcmp(s, "31m") == 0);
	sys_fmt_uptime_short(12 * 86400 + 4 * 3600 + 31 * 60, s, sizeof(s));
	CHECK("CARD-2", strcmp(s, "12d 4h") == 0);
	sys_fmt_bytes(4508000000000ull, s, sizeof(s));
	CHECK("CARD-2", strcmp(s, "4.1T") == 0);
	sys_fmt_bytes(33ull << 30, s, sizeof(s));
	CHECK("CARD-2", strcmp(s, "33G") == 0);

	/* --- twm's info answer --- */
	struct fetch_facts f;
	fetch_init(&f);
	snprintf(f.user, sizeof(f.user), "tom");
	snprintf(f.host, sizeof(f.host), "kestrel");
	snprintf(f.os, sizeof(f.os), "FreeBSD 15.0-RELEASE amd64");
	f.uptime = 12 * 86400 + 4 * 3600 + 31 * 60;
	f.now = 100000;
	fetch_parse_info("version\t0.1.0\nxwayland\ton\nframes\t7\t3\t2\ndesktops\t4\t1\t3\n"
		"output\tDP-1\t2560\t1440\t1.25\noutput\teDP-1\t2880\t1800\t1.5\n"
		"focus\tsloppy\t80\nfont\tIBM Plex Mono\t11\ntwmrc\t/home/tom/.twmrc\t84\tok\t92800\n"
		"color\taccent\t7FD1AE\n", &f);
	CHECK("FET-3", f.wm && f.open == 7 && f.iconified == 3 && f.groups == 2 && f.quiet == 3 &&
		f.noutputs == 2 && f.outputs[0].scale == 1.25 && f.colors[FC_ACCENT] == 0x7FD1AE);

	/* --- B9's text, without colour --- */
	static char out[16384];
	fetch_render(&f, false, true, out, sizeof(out));
	CHECK("FET-2", strstr(out, " ■ tom's wm ░░░░ ") == out);
	CHECK("FET-2", strstr(out, "│   since 1987          │") != NULL);
	CHECK("FET-1", strstr(out, "tom@kestrel\n") && strstr(out, "───────────\n"));
	CHECK("FET-1", strstr(out, "wm        Tom's WM 0.1 · wayland · xwayland on\n") != NULL);
	CHECK("FET-1", strstr(out, "frames    7 open · 3 iconified · 2 tab groups\n") != NULL);
	CHECK("FET-1", strstr(out, "desktops  4 · on 1 · 3 quiet\n") != NULL);
	CHECK("FET-1", strstr(out, "outputs   DP-1 2560×1440 @1.25 · eDP-1 2880×1800 @1.5\n") != NULL);
	CHECK("FET-1", strstr(out, "focus     sloppy · intent 80 ms\n") != NULL);
	CHECK("FET-1", strstr(out, ".twmrc    84 lines · check ok · reloaded 2h ago\n") != NULL);
	CHECK("FET-1", strstr(out, "\x1b[") == NULL); /* no escapes without colour */
	/* the logo and the facts sit side by side: column 30 */
	CHECK("FET-2", strstr(out, "\n│                       │    os        FreeBSD 15.0-RELEASE amd64\n") != NULL);
	fetch_render(&f, false, false, out, sizeof(out));
	CHECK("FET-4", strncmp(out, "tom@kestrel\n", 12) == 0 && !strstr(out, "tom's wm"));
	fetch_render(&f, true, true, out, sizeof(out));
	CHECK("FET-1", strstr(out, "\x1b[38;2;127;209;174m") != NULL); /* the running accent */

	fetch_short(&f, out, sizeof(out));
	CHECK("FET-4", strcmp(out, "tom@kestrel · twm 0.1 · 7 frames · desk 1/4 · up 12d 4h\n") == 0);
	fetch_json(&f, out, sizeof(out));
	CHECK("FET-4", strstr(out, "\"frames\":{\"open\":7,\"iconified\":3,\"groups\":2}") &&
		strstr(out, "\"host\":\"kestrel\"") && out[strlen(out) - 2] == '}');

	/* outside twm */
	struct fetch_facts g;
	fetch_init(&g);
	fetch_render(&g, false, false, out, sizeof(out));
	CHECK("FET-3", strstr(out, "wm        not running") && !strstr(out, "frames"));
	return UNIT_RESULT();
}
