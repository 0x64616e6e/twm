/*
 * twmfetch (FET-*): the machine's facts and what only Tom's WM knows, in
 * its colors. `twmfetch <image>` applies a look shared in a fetch shot
 * (SHR-*).
 */
#ifdef __FreeBSD__
#undef _POSIX_C_SOURCE
#else
#define _DEFAULT_SOURCE /* realpath */
#endif
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "fetchfmt.h"
#include "ipcclient.h"
#include "sysinfo.h"

static void usage(FILE *out) {
	fprintf(out,
		"Usage: twmfetch [options] [image]\n"
		"\n"
		"Print this machine's facts and Tom's WM's own, and leave a fetch shot\n"
		"in /tmp that carries your look. Given an image, apply the look it carries.\n"
		"\n"
		"  -j, --json        every fact as one JSON object\n"
		"  -s, --short       one line, for a prompt\n"
		"      --logo off    facts only\n"
		"  -n, --no-shot     print only; write nothing\n"
		"  -v, --version\n"
		"  -h, --help\n");
}

static bool pool_facts(struct sys_pool *p) {
	FILE *pipe = popen(SYS_ZPOOL_CMD, "r");
	if (!pipe) {
		return false;
	}
	char buf[8192];
	size_t n = fread(buf, 1, sizeof(buf) - 1, pipe);
	buf[n] = '\0';
	pclose(pipe);
	return sys_parse_zpool(buf, p);
}

int main(int argc, char *argv[]) {
	static const struct option opts[] = {
		{"json", no_argument, NULL, 'j'},
		{"short", no_argument, NULL, 's'},
		{"logo", required_argument, NULL, 'l'},
		{"no-shot", no_argument, NULL, 'n'},
		{"version", no_argument, NULL, 'v'},
		{"help", no_argument, NULL, 'h'},
		{0},
	};
	bool json = false, shortline = false, logo = true, shot = true;
	int c;
	while ((c = getopt_long(argc, argv, "jsnvh", opts, NULL)) != -1) {
		switch (c) {
		case 'j':
			json = true;
			break;
		case 's':
			shortline = true;
			break;
		case 'l':
			logo = strcmp(optarg, "off") != 0;
			break;
		case 'n':
			shot = false;
			break;
		case 'v':
			printf("twmfetch (Tom's WM) %s\n", TWM_VERSION);
			return 0;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}

	if (optind < argc) {
		/* SHR-1: apply the look an image carries — twm does it, with the
		 * diff in view, so the request names the file. */
		char path[4096];
		if (!realpath(argv[optind], path)) {
			fprintf(stderr, "twmfetch: %s: no such file\n", argv[optind]);
			return 1;
		}
		char req[4200], reply[8192];
		snprintf(req, sizeof(req), "absorb\t%s", path);
		if (!ipcc_request(req, reply, sizeof(reply), false)) {
			return 2;
		}
		fputs(reply, stdout);
		return strncmp(reply, "look ", 5) == 0 ? 0 : 1;
	}

	struct fetch_facts f;
	fetch_init(&f);
	f.now = time(NULL);
	sys_user(f.user, sizeof(f.user));
	sys_host(f.host, sizeof(f.host));
	sys_os(f.os, sizeof(f.os));
	f.uptime = sys_uptime();
	f.have_mem = sys_mem(&f.mem);
	f.have_pool = pool_facts(&f.pool);

	char reply[8192];
	if (ipcc_request("info", reply, sizeof(reply), true)) {
		fetch_parse_info(reply, &f);
	}
	/* FET-5: a plain run leaves a fetch shot (SHOT-2) — not for --short in
	 * a prompt, which runs all the time */
	if (f.wm && shot && !shortline) {
		char r[sizeof(f.shot)];
		if (ipcc_request("fetchshot", r, sizeof(r), true) && r[0] == '/') {
			r[strcspn(r, "\n")] = '\0';
			snprintf(f.shot, sizeof(f.shot), "%s", r);
		}
	}

	static char out[32768];
	if (json) {
		fetch_json(&f, out, sizeof(out));
	} else if (shortline) {
		fetch_short(&f, out, sizeof(out));
	} else {
		const char *term = getenv("TERM");
		bool color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR") &&
			!(term && strcmp(term, "dumb") == 0);
		fetch_render(&f, color, logo, out, sizeof(out));
	}
	fputs(out, stdout);
	return 0;
}
