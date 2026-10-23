#ifndef TWM_PIXBUF_H
#define TWM_PIXBUF_H

#include <pixman.h>
#include <wlr/interfaces/wlr_buffer.h>

/*
 * A CPU buffer backed by a pixman image (premultiplied ARGB8888), handed to
 * the scene graph as a wlr_buffer. All chrome that isn't a solid rectangle is
 * drawn into one of these (D-4).
 */
struct twm_pixbuf {
	struct wlr_buffer base;
	pixman_image_t *image;
	uint32_t *data;
	int stride;
};

/* A transparent buffer of the given device-pixel size, or NULL. */
struct twm_pixbuf *pixbuf_create(int width, int height);

/* Release the caller's reference; the scene keeps its own while it shows it. */
void pixbuf_drop(struct twm_pixbuf *buf);

#endif
