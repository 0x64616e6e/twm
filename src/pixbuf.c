#include <drm_fourcc.h>
#include <stdlib.h>
#include "pixbuf.h"

static void pixbuf_destroy(struct wlr_buffer *wlr_buffer) {
	struct twm_pixbuf *buf = wl_container_of(wlr_buffer, buf, base);
	pixman_image_unref(buf->image);
	free(buf->data);
	free(buf);
}

static bool pixbuf_begin_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
		void **data, uint32_t *format, size_t *stride) {
	struct twm_pixbuf *buf = wl_container_of(wlr_buffer, buf, base);
	*data = buf->data;
	*format = DRM_FORMAT_ARGB8888;
	*stride = (size_t)buf->stride;
	return true;
}

static void pixbuf_end_access(struct wlr_buffer *wlr_buffer) {
}

static const struct wlr_buffer_impl pixbuf_impl = {
	.destroy = pixbuf_destroy,
	.begin_data_ptr_access = pixbuf_begin_access,
	.end_data_ptr_access = pixbuf_end_access,
};

struct twm_pixbuf *pixbuf_create(int width, int height) {
	if (width <= 0 || height <= 0) {
		return NULL;
	}
	struct twm_pixbuf *buf = calloc(1, sizeof(*buf));
	if (!buf) {
		return NULL;
	}
	buf->stride = width * 4;
	buf->data = calloc((size_t)height, (size_t)buf->stride);
	if (!buf->data) {
		free(buf);
		return NULL;
	}
	buf->image = pixman_image_create_bits_no_clear(PIXMAN_a8r8g8b8,
		width, height, buf->data, buf->stride);
	if (!buf->image) {
		free(buf->data);
		free(buf);
		return NULL;
	}
	wlr_buffer_init(&buf->base, &pixbuf_impl, width, height);
	return buf;
}

void pixbuf_drop(struct twm_pixbuf *buf) {
	if (buf) {
		wlr_buffer_drop(&buf->base);
	}
}
