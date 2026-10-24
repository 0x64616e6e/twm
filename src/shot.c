#include <drm_fourcc.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wlr/render/allocator.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "card.h"
#include "config.h"
#include "png.h"
#include "rootline.h"
#include "server.h"
#include "shot.h"

/* mkdir -p */
static bool make_dirs(const char *path) {
	char p[4096];
	snprintf(p, sizeof(p), "%s", path);
	for (char *s = p + 1; *s; s++) {
		if (*s == '/') {
			*s = '\0';
			mkdir(p, 0755);
			*s = '/';
		}
	}
	return mkdir(p, 0755) == 0 || errno == EEXIST;
}

/* SHOT-1: $XDG_PICTURES_DIR (from user-dirs.dirs), else ~/Pictures. */
static void pictures_dir(char *out, size_t len) {
	const char *home = getenv("HOME");
	home = home ? home : "/tmp";
	snprintf(out, len, "%s/Pictures", home);
	char cfg[4096];
	const char *xch = getenv("XDG_CONFIG_HOME");
	if (xch && *xch) {
		snprintf(cfg, sizeof(cfg), "%s/user-dirs.dirs", xch);
	} else {
		snprintf(cfg, sizeof(cfg), "%s/.config/user-dirs.dirs", home);
	}
	FILE *f = fopen(cfg, "r");
	if (!f) {
		return;
	}
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "XDG_PICTURES_DIR=\"", 18) != 0) {
			continue;
		}
		char *v = line + 18;
		v[strcspn(v, "\"")] = '\0';
		if (strncmp(v, "$HOME", 5) == 0) {
			snprintf(out, len, "%s%s", home, v + 5);
		} else if (v[0] == '/') {
			snprintf(out, len, "%s", v);
		}
	}
	fclose(f);
}

/* ~ for $HOME, for the root line */
static void tilde(const char *path, char *out, size_t len) {
	const char *home = getenv("HOME");
	size_t n = home ? strlen(home) : 0;
	if (n && strncmp(path, home, n) == 0 && path[n] == '/') {
		snprintf(out, len, "~%s", path + n);
	} else {
		snprintf(out, len, "%s", path);
	}
}

static struct twm_output *output_for_shot(struct twm_server *server) {
	struct twm_output *o = output_at(server, server->cursor->x, server->cursor->y);
	if (!o && !wl_list_empty(&server->outputs)) {
		o = wl_container_of(server->outputs.next, o, link);
	}
	return o;
}

/* Render the output's scene into a buffer of our own and read it back:
 * 0xAARRGGBB words, w*4 a row. */
static uint32_t *render_output(struct twm_server *server, struct twm_output *out, int *w, int *h) {
	struct wlr_output *wo = out->wlr_output;
	struct wlr_scene_output *so = wlr_scene_get_scene_output(server->scene, wo);
	if (!so || !wo->swapchain) {
		return NULL;
	}
	struct wlr_swapchain *sc = wlr_swapchain_create(server->allocator, wo->width, wo->height,
		&wo->swapchain->format);
	if (!sc) {
		return NULL;
	}
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	struct wlr_scene_output_state_options opts = {.swapchain = sc};
	uint32_t *px = NULL;
	if (wlr_scene_output_build_state(so, &state, &opts) &&
			(state.committed & WLR_OUTPUT_STATE_BUFFER) && state.buffer) {
		struct wlr_texture *tex = wlr_texture_from_buffer(server->renderer, state.buffer);
		if (tex) {
			*w = (int)tex->width;
			*h = (int)tex->height;
			px = malloc((size_t)*w * (size_t)*h * 4);
			uint32_t fmt = wlr_texture_preferred_read_format(tex);
			bool bgr = fmt == DRM_FORMAT_XBGR8888 || fmt == DRM_FORMAT_ABGR8888;
			if (!bgr && fmt != DRM_FORMAT_XRGB8888) {
				fmt = DRM_FORMAT_ARGB8888;
			}
			if (px && !wlr_texture_read_pixels(tex, &(struct wlr_texture_read_pixels_options){
					.data = px, .format = fmt, .stride = (uint32_t)*w * 4})) {
				free(px);
				px = NULL;
			}
			if (px && bgr) {
				for (size_t i = 0; i < (size_t)*w * (size_t)*h; i++) {
					uint32_t p = px[i];
					px[i] = (p & 0xFF00FF00) | (p >> 16 & 0xFF) | (p & 0xFF) << 16;
				}
			}
			wlr_texture_destroy(tex);
		}
	}
	wlr_output_state_finish(&state);
	wlr_swapchain_destroy(sc);
	return px;
}

bool shot_take(struct twm_server *server, bool fetch, char *path, size_t len) {
	if (server->prompt) {
		snprintf(path, len, "a trusted prompt is open"); /* SHOT-3 */
		rootline_post(server, ROOTLINE_INFO, "screenshot", "not while a prompt is open");
		return false;
	}
	struct twm_output *out = output_for_shot(server);
	if (!out) {
		snprintf(path, len, "no output");
		return false;
	}
	char dir[4096];
	if (fetch) {
		const char *tmp = getenv("TMPDIR"); /* SHOT-2: kept only if moved out */
		snprintf(dir, sizeof(dir), "%s", tmp && *tmp == '/' ? tmp : "/tmp");
	} else {
		pictures_dir(dir, sizeof(dir));
		size_t n = strlen(dir);
		snprintf(dir + n, sizeof(dir) - n, "/Screenshots");
	}
	if (!make_dirs(dir)) {
		snprintf(path, len, "can't make %s: %s", dir, strerror(errno));
		rootline_post(server, ROOTLINE_ERROR, "screenshot", "%s", path);
		return false;
	}
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
	const char *stem = fetch ? "twmfetch" : "twm";
	snprintf(path, len, "%s/%s-%s.png", dir, stem, stamp);
	for (int i = 2; access(path, F_OK) == 0 && i < 100; i++) {
		snprintf(path, len, "%s/%s-%s-%d.png", dir, stem, stamp, i);
	}

	if (fetch) {
		card_force(server, true); /* SHOT-2: the card drawn in */
	}
	int w = 0, h = 0;
	uint32_t *px = render_output(server, out, &w, &h);
	if (fetch) {
		card_force(server, false);
	}
	if (!px) {
		snprintf(path, len, "couldn't read the output back");
		rootline_post(server, ROOTLINE_ERROR, "screenshot", "%s", path);
		return false;
	}
	/* Compress and write in a child, so the desktop never waits on it; it
	 * is reaped with the others (server.c). */
	pid_t pid = fork();
	if (pid == 0) {
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		struct png_text texts[] = {
			{"twm:twmrc", server->config_text ? server->config_text : ""},
			{"twm:version", TWM_VERSION},
		};
		_exit(png_write(path, px, w, h, (size_t)w * 4, texts, fetch ? 2 : 0) ? 0 : 1);
	}
	free(px);
	if (pid < 0) {
		snprintf(path, len, "fork: %s", strerror(errno));
		return false;
	}
	char shown[4096];
	tilde(path, shown, sizeof(shown));
	rootline_post(server, ROOTLINE_INFO, fetch ? "fetch shot" : "screenshot", "%s", shown);
	return true;
}
