#ifndef TWM_PNG_H
#define TWM_PNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * PNG for screenshots (SHOT-*, D-11), pure: an RGB writer with twm's own
 * deflate (fixed Huffman codes over LZ77 matches; flat chrome compresses
 * well), UTF-8 text chunks (iTXt, uncompressed), and a reader for those
 * chunks alone — applying a shared look never decodes the pixels.
 */

struct png_text {
	const char *key;   /* "twm:twmrc" */
	const char *value; /* UTF-8 */
};

/* `pixels` are 0xAARRGGBB words (alpha ignored), `stride` in bytes. The
 * whole file into a malloc'd buffer; NULL on failure. */
uint8_t *png_encode(const uint32_t *pixels, int width, int height, size_t stride,
	const struct png_text *texts, int ntexts, size_t *out_len);
/* The same, written to `path` (created 0644, replaced atomically). */
bool png_write(const char *path, const uint32_t *pixels, int width, int height,
	size_t stride, const struct png_text *texts, int ntexts);

/* The value of text chunk `key` (tEXt or uncompressed iTXt) in a PNG file
 * held in memory; malloc'd, or NULL. */
char *png_text_get(const uint8_t *data, size_t len, const char *key);
/* Whether these bytes start like a PNG. */
bool png_is_png(const uint8_t *data, size_t len);

#endif
