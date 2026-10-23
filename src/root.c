#include <math.h>
#include <wlr/types/wlr_output.h>
#include "draw.h"
#include "pixbuf.h"
#include "server.h"

/* ROOT-1: each output's root is `ground` with the root weave. */
void root_update(struct twm_output *output) {
	struct twm_server *server = output->server;
	const struct twm_theme *t = &server->theme;
	float scale = output->wlr_output->scale;
	int dw = (int)lroundf(output->box.width * scale);
	int dh = (int)lroundf(output->box.height * scale);

	struct twm_pixbuf *buf = pixbuf_create(dw, dh);
	if (!buf) {
		return;
	}
	struct twm_rgba ground = rgba_hex(t->ground, 1);
	draw_fill(buf->image, rgba_argb32(ground), 0, 0, dw, dh);
	if (t->root_weave) {
		struct twm_rgba dot = rgba_over(rgba_hex(t->ink, 0.07f), ground);
		draw_weave(buf->image, scale, 6, output->box.x, output->box.y,
			rgba_argb32(dot));
	}

	if (!output->root) {
		output->root = wlr_scene_buffer_create(server->layers[TWM_LAYER_ROOT], NULL);
		output->root->node.data = &server->root_hit;
	}
	wlr_scene_buffer_set_buffer(output->root, &buf->base);
	pixbuf_drop(buf);
	wlr_scene_buffer_set_dest_size(output->root, output->box.width, output->box.height);
	pixman_region32_t opaque;
	pixman_region32_init_rect(&opaque, 0, 0, output->box.width, output->box.height);
	wlr_scene_buffer_set_opaque_region(output->root, &opaque);
	pixman_region32_fini(&opaque);
	wlr_scene_node_set_position(&output->root->node, output->box.x, output->box.y);
}
