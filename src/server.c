#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>
#include "histview.h"
#include "imgr.h"
#include "capture.h"
#include "hints.h"
#include "ipc.h"
#include "layers.h"
#include "logview.h"
#include "menu.h"
#include "panner.h"
#include "media.h"
#include "portal.h"
#include "prompt.h"
#include "setwin.h"
#include "ui.h"
#include "outline.h"
#include "rc.h"
#include "remember.h"
#include "rootline.h"
#include "server.h"
#include "card.h"
#include "settings.h"
#include "text.h"
#include "view.h"

static int handle_sigchld(int signo, void *data) {
	struct twm_server *server = data;
	int status;
	pid_t pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (pid != server->startup_pid) {
			continue;
		}
		server->startup_pid = 0;
		int code;
		if (WIFSIGNALED(status)) {
			/* as a shell reports it: 128 + the signal */
			code = 128 + WTERMSIG(status);
			wlr_log(WLR_INFO, "startup command killed by signal %d (%s)", WTERMSIG(status),
				strsignal(WTERMSIG(status)));
		} else {
			code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
			wlr_log(WLR_INFO, "startup command exited with %d", code);
		}
		if (server->opts.exit_with_startup) {
			server->exit_code = code;
			wl_display_terminate(server->display);
		}
	}
	return 0;
}

static int handle_terminate(int signo, void *data) {
	struct twm_server *server = data;
	wl_display_terminate(server->display);
	return 0;
}

static pid_t spawn(const char *cmd, const char *token) {
	pid_t pid = fork();
	if (pid < 0) {
		wlr_log_errno(WLR_ERROR, "fork");
		return 0;
	}
	if (pid == 0) {
		/* The event loop blocks the signals it watches; children must not
		 * inherit that. */
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		setsid();
		if (token) {
			setenv("XDG_ACTIVATION_TOKEN", token, 1);
			setenv("DESKTOP_STARTUP_ID", token, 1);
		}
		execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
	return pid;
}

void server_spawn(struct twm_server *server, const char *cmd) {
	spawn(cmd, focus_launch_token(server));
}

static struct wlr_backend *create_backend(struct twm_server *server) {
	if (server->opts.headless_width > 0) {
		struct wlr_backend *backend = wlr_headless_backend_create(server->loop);
		if (backend) {
			wlr_headless_add_output(backend, server->opts.headless_width,
				server->opts.headless_height);
		}
		return backend;
	}
	return wlr_backend_autocreate(server->loop, &server->session);
}

uint64_t server_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

bool server_init(struct twm_server *server, const struct twm_options *opts) {
	server->opts = *opts;
	/* Nested in another session (or headless), twm must not speak for the
	 * session: no activation-environment updates (portal.c). */
	server->opts.session = !opts->headless_width && !getenv("WAYLAND_DISPLAY") &&
		!getenv("DISPLAY") && !getenv("WAYLAND_SOCKET");
	theme_init_defaults(&server->theme);
	wl_list_init(&server->views);
	wl_list_init(&server->devices); /* before the config is applied (rc_init) */
	server->touch_pointer = -1;
	server->desktop = 1;
	wl_list_init(&server->place_queue);
	server->root_hit.kind = TWM_HIT_ROOT;

	server->display = wl_display_create();
	server->loop = wl_display_get_event_loop(server->display);

	server->sigchld = wl_event_loop_add_signal(server->loop, SIGCHLD,
		handle_sigchld, server);
	server->sigint = wl_event_loop_add_signal(server->loop, SIGINT,
		handle_terminate, server);
	server->sigterm = wl_event_loop_add_signal(server->loop, SIGTERM,
		handle_terminate, server);

	if (opts->headless_width > 0 && !getenv("WLR_RENDERER")) {
		/* Deterministic pixels for screenshot tests (D-7). */
		setenv("WLR_RENDERER", "pixman", 1);
	}

	server->backend = create_backend(server);
	if (!server->backend) {
		wlr_log(WLR_ERROR, "failed to create a backend");
		return false;
	}
	server->renderer = wlr_renderer_autocreate(server->backend);
	if (!server->renderer) {
		wlr_log(WLR_ERROR, "failed to create a renderer");
		return false;
	}
	wlr_renderer_init_wl_display(server->renderer, server->display);
	server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
	if (!server->allocator) {
		wlr_log(WLR_ERROR, "failed to create an allocator");
		return false;
	}

	server->compositor = wlr_compositor_create(server->display, 6, server->renderer);
	wlr_subcompositor_create(server->display);
	wlr_data_device_manager_create(server->display);
	wlr_primary_selection_v1_device_manager_create(server->display);
	wlr_viewporter_create(server->display);
	wlr_presentation_create(server->display, server->backend, 2);
	wlr_fractional_scale_manager_v1_create(server->display, 1);
	wlr_single_pixel_buffer_manager_v1_create(server->display);

	server->scene = wlr_scene_create();
	for (int i = 0; i < TWM_LAYER_COUNT; i++) {
		server->layers[i] = wlr_scene_tree_create(&server->scene->tree);
	}

	if (!text_init()) {
		wlr_log(WLR_ERROR, "fcft failed to initialize; titles will be blank");
	}
	rootline_init(server);
	prompt_init(server);
	media_init(server);
	output_init(server);
	rc_init(server, opts->config_path);
	remember_init(server);
	wlr_xdg_output_manager_v1_create(server->display, server->output_layout);
	xdg_init(server);
	layers_init(server);
	decoration_init(server);
	input_init(server);
	touch_init(server);
	focus_init(server);
	capture_init(server);
	outline_init(server);
	histview_init(server);
	imgr_init(server);
	panner_init(server);
	logview_init(server);
	setwin_init(server);
	card_init(server);
#if HAVE_XWAYLAND
	if (opts->xwayland && server->config->xwayland) {
		xwayland_init(server);
	}
#endif
	return true;
}

bool server_start(struct twm_server *server) {
	server->socket = wl_display_add_socket_auto(server->display);
	if (!server->socket) {
		wlr_log(WLR_ERROR, "failed to open a Wayland socket "
			"(is XDG_RUNTIME_DIR set?)");
		return false;
	}
	if (!wlr_backend_start(server->backend)) {
		wlr_log(WLR_ERROR, "failed to start the backend");
		return false;
	}
	setenv("WAYLAND_DISPLAY", server->socket, true);
	ipc_init(server);
	portal_init(server);
	wlr_log(WLR_INFO, "twm running on WAYLAND_DISPLAY=%s", server->socket);
	if (server->opts.startup_cmd) {
		server->startup_pid = spawn(server->opts.startup_cmd, NULL);
	}
	return true;
}

void server_run(struct twm_server *server) {
	wl_display_run(server->display);
}

void server_finish(struct twm_server *server) {
	server->shutting_down = true;
	prompt_finish(server);
	media_finish(server);
	card_finish(server);
	portal_finish(server);
	if (server->startup_pid > 0) {
		kill(server->startup_pid, SIGTERM);
	}
	if (server->display) {
		wl_display_destroy_clients(server->display);
	}
#if HAVE_XWAYLAND
	xwayland_finish(server);
#endif
	ipc_finish(server);
	capture_finish(server);
	hints_finish(server);
	menu_close_all(server);
	select_cancel(server);
	ui_finish(server);
	imgr_finish(server);
	panner_finish(server);
	setwin_finish(server);
	logview_finish(server);
	histview_finish(server);
	remember_finish(server);
	rc_finish(server);
	rootline_finish(server);
	xdg_finish(server);
	layers_finish(server);
	decoration_finish(server);
	focus_finish(server);
	touch_finish(server);
	input_finish(server);
	output_finish(server);
	outline_finish(server);
	root_finish(server);
	if (server->sigchld) {
		wl_event_source_remove(server->sigchld);
		wl_event_source_remove(server->sigint);
		wl_event_source_remove(server->sigterm);
	}
	if (server->scene) {
		wlr_scene_node_destroy(&server->scene->tree.node);
	}
	if (server->allocator) {
		wlr_allocator_destroy(server->allocator);
	}
	if (server->renderer) {
		wlr_renderer_destroy(server->renderer);
	}
	if (server->backend) {
		wlr_backend_destroy(server->backend);
	}
	if (server->display) {
		wl_display_destroy(server->display);
	}
	text_finish();
}

struct twm_hit server_hit_at(struct twm_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	*surface = NULL;
	double nx = 0, ny = 0;
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node, lx, ly, &nx, &ny);
	if (!node) {
		return server->root_hit;
	}
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
		if (scene_surface) {
			*surface = scene_surface->surface;
			*sx = nx;
			*sy = ny;
		}
	}
	for (struct wlr_scene_node *n = node; n; n = n->parent ? &n->parent->node : NULL) {
		if (n->data) {
			return *(struct twm_hit *)n->data;
		}
	}
	return server->root_hit;
}
