#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include "config.h"
#include "ipc.h"
#include "rc.h"
#include "server.h"

static const char usage[] =
	"Usage: twm [options]\n"
	"\n"
	"Tom's WM, a Wayland compositor after twm.\n"
	"\n"
	"  -h, --help               Show this help and exit.\n"
	"  -v, --version            Show the version and exit.\n"
	"  -f, --config <file>      Use <file> instead of $TWMRC or ~/.twmrc.\n"
	"      --check [file]       Check a config and exit (0: ok, 1: errors).\n"
	"      --reload             Ask the running twm to reload its config.\n"
	"  -s, --startup <cmd>      Run <cmd> with /bin/sh once twm is up.\n"
	"      --exit-with-startup  Exit when the startup command exits, with its status.\n"
	"      --headless <W>x<H>   Run on the headless backend with one output.\n"
	"      --no-xwayland        Don't offer XWayland to X11 clients.\n"
	"      --test               Test harness mode: trust the startup command\n"
	"                           (its captures go unmarked, its synthetic input\n"
	"                           may answer prompts). For automated tests only.\n"
	"  -d, --debug              Verbose logging.\n";

enum {
	OPT_EXIT_WITH_STARTUP = 256,
	OPT_HEADLESS,
	OPT_NO_XWAYLAND,
	OPT_CHECK,
	OPT_TEST,
	OPT_RELOAD,
};

static bool parse_size(const char *s, int *w, int *h) {
	char *end;
	long lw = strtol(s, &end, 10);
	if (*end != 'x') {
		return false;
	}
	long lh = strtol(end + 1, &end, 10);
	if (*end != '\0' || lw <= 0 || lh <= 0 || lw > 16384 || lh > 16384) {
		return false;
	}
	*w = (int)lw;
	*h = (int)lh;
	return true;
}

int main(int argc, char *argv[]) {
	static const struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'v'},
		{"startup", required_argument, NULL, 's'},
		{"debug", no_argument, NULL, 'd'},
		{"exit-with-startup", no_argument, NULL, OPT_EXIT_WITH_STARTUP},
		{"headless", required_argument, NULL, OPT_HEADLESS},
		{"no-xwayland", no_argument, NULL, OPT_NO_XWAYLAND},
		{"config", required_argument, NULL, 'f'},
		{"check", no_argument, NULL, OPT_CHECK},
		{"test", no_argument, NULL, OPT_TEST},
		{"reload", no_argument, NULL, OPT_RELOAD},
		{0},
	};

	struct twm_options opts = {.xwayland = true};
	bool debug = false, check = false, reload = false;
	int c;
	while ((c = getopt_long(argc, argv, "hvs:df:", long_options, NULL)) != -1) {
		switch (c) {
		case 'h':
			fputs(usage, stdout);
			return 0;
		case 'v':
			printf("twm (Tom's WM) %s\n", TWM_VERSION);
			return 0;
		case 's':
			opts.startup_cmd = optarg;
			break;
		case 'd':
			debug = true;
			break;
		case OPT_EXIT_WITH_STARTUP:
			opts.exit_with_startup = true;
			break;
		case OPT_HEADLESS:
			if (!parse_size(optarg, &opts.headless_width, &opts.headless_height)) {
				fprintf(stderr, "twm: --headless wants WIDTHxHEIGHT, got '%s'\n", optarg);
				return 2;
			}
			break;
		case OPT_NO_XWAYLAND:
			opts.xwayland = false;
			break;
		case 'f':
			opts.config_path = optarg;
			break;
		case OPT_CHECK:
			check = true;
			break;
		case OPT_TEST:
			opts.test_mode = true;
			break;
		case OPT_RELOAD:
			reload = true;
			break;
		default:
			fputs(usage, stderr);
			return 2;
		}
	}
	if (check) {
		/* CFG-4: twm --check [file] */
		if (optind + 1 < argc) {
			fputs(usage, stderr);
			return 2;
		}
		char *path = rc_path(optind < argc ? argv[optind] : opts.config_path);
		int status = rc_check(path, stdout, stderr);
		free(path);
		return status;
	}
	if (reload) {
		return ipc_request("reload"); /* CFG-12 */
	}
	if (optind < argc) {
		fputs(usage, stderr);
		return 2;
	}
	if (opts.exit_with_startup && !opts.startup_cmd) {
		fprintf(stderr, "twm: --exit-with-startup needs --startup\n");
		return 2;
	}

	wlr_log_init(debug ? WLR_DEBUG : WLR_INFO, NULL);

	struct twm_server server = {0};
	int ret = 1;
	if (server_init(&server, &opts) && server_start(&server)) {
		server_run(&server);
		ret = server.exit_code;
	}
	server_finish(&server);
	return ret;
}
