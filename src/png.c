#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "png.h"

/* --- a growing byte buffer --- */

struct buf {
	uint8_t *d;
	size_t len, cap;
	bool failed;
};

static void reserve(struct buf *b, size_t more) {
	if (b->failed || b->len + more <= b->cap) {
		return;
	}
	size_t cap = b->cap ? b->cap : 65536;
	while (cap < b->len + more) {
		cap *= 2;
	}
	uint8_t *d = realloc(b->d, cap);
	if (!d) {
		b->failed = true;
		return;
	}
	b->d = d;
	b->cap = cap;
}

static void put(struct buf *b, const void *p, size_t n) {
	reserve(b, n);
	if (!b->failed) {
		memcpy(b->d + b->len, p, n);
		b->len += n;
	}
}

static void put8(struct buf *b, uint8_t v) {
	put(b, &v, 1);
}

static void put32be(struct buf *b, uint32_t v) {
	uint8_t x[4] = {v >> 24, v >> 16 & 0xff, v >> 8 & 0xff, v & 0xff};
	put(b, x, 4);
}

/* --- checksums --- */

static uint32_t crc_table[256];

static void crc_init(void) {
	if (crc_table[1]) {
		return;
	}
	for (uint32_t n = 0; n < 256; n++) {
		uint32_t c = n;
		for (int k = 0; k < 8; k++) {
			c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		}
		crc_table[n] = c;
	}
}

static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n) {
	for (size_t i = 0; i < n; i++) {
		c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
	}
	return c;
}

static uint32_t adler32(const uint8_t *p, size_t n) {
	uint32_t a = 1, b = 0;
	while (n > 0) {
		size_t k = n < 5552 ? n : 5552;
		n -= k;
		while (k--) {
			a += *p++;
			b += a;
		}
		a %= 65521;
		b %= 65521;
	}
	return b << 16 | a;
}

/* --- deflate: fixed Huffman codes over LZ77 matches (D-11) --- */

struct bits {
	struct buf *out;
	uint32_t acc;
	int n;
};

static void put_bits(struct bits *w, uint32_t v, int n) {
	w->acc |= v << w->n;
	w->n += n;
	while (w->n >= 8) {
		put8(w->out, (uint8_t)w->acc);
		w->acc >>= 8;
		w->n -= 8;
	}
}

static uint32_t reverse(uint32_t code, int len) {
	uint32_t r = 0;
	for (int i = 0; i < len; i++) {
		r = r << 1 | (code >> i & 1);
	}
	return r;
}

/* Huffman codes go most significant bit first. */
static void put_code(struct bits *w, uint32_t code, int len) {
	put_bits(w, reverse(code, len), len);
}

static void put_litlen(struct bits *w, int v) {
	if (v < 144) {
		put_code(w, 0x30 + (uint32_t)v, 8);
	} else if (v < 256) {
		put_code(w, 0x190 + (uint32_t)(v - 144), 9);
	} else if (v < 280) {
		put_code(w, (uint32_t)(v - 256), 7);
	} else {
		put_code(w, 0xC0 + (uint32_t)(v - 280), 8);
	}
}

static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
	35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
	4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
	9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void put_match(struct bits *w, int len, int dist) {
	int i = 28;
	while (len_base[i] > len) {
		i--;
	}
	put_litlen(w, 257 + i);
	if (len_extra[i]) {
		put_bits(w, (uint32_t)(len - len_base[i]), len_extra[i]);
	}
	int j = 29;
	while (dist_base[j] > dist) {
		j--;
	}
	put_code(w, (uint32_t)j, 5);
	if (dist_extra[j]) {
		put_bits(w, (uint32_t)(dist - dist_base[j]), dist_extra[j]);
	}
}

#define WINDOW 32768
#define HASH_BITS 15
#define MAX_CHAIN 24
#define MIN_MATCH 3
#define MAX_MATCH 258

static uint32_t hash3(const uint8_t *p) {
	return ((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ p[2]) & ((1u << HASH_BITS) - 1);
}

static void deflate(struct buf *out, const uint8_t *d, size_t n) {
	struct bits w = {out, 0, 0};
	put_bits(&w, 1, 1); /* the last block */
	put_bits(&w, 1, 2); /* fixed Huffman codes */
	int32_t *head = malloc(sizeof(int32_t) << HASH_BITS);
	int32_t *prev = malloc(sizeof(int32_t) * WINDOW);
	if (!head || !prev) {
		out->failed = true;
		free(head);
		free(prev);
		return;
	}
	memset(head, 0xff, sizeof(int32_t) << HASH_BITS);
	size_t i = 0;
	while (i < n) {
		int best = 0, best_dist = 0;
		if (i + MIN_MATCH <= n) {
			uint32_t h = hash3(d + i);
			int32_t cand = head[h];
			size_t limit = n - i < MAX_MATCH ? n - i : MAX_MATCH;
			for (int chain = 0; cand >= 0 && chain < MAX_CHAIN; chain++) {
				size_t dist = i - (size_t)cand;
				if (dist > WINDOW - 1) {
					break;
				}
				const uint8_t *a = d + cand, *b = d + i;
				if (a[best] == b[best]) {
					size_t k = 0;
					while (k < limit && a[k] == b[k]) {
						k++;
					}
					if ((int)k > best) {
						best = (int)k;
						best_dist = (int)dist;
						if (k == limit) {
							break;
						}
					}
				}
				int32_t next = prev[cand % WINDOW];
				if (next >= cand) {
					break;
				}
				cand = next;
			}
		}
		size_t step = best >= MIN_MATCH ? (size_t)best : 1;
		if (best >= MIN_MATCH) {
			put_match(&w, best, best_dist);
		} else {
			put_litlen(&w, d[i]);
		}
		/* every position goes into the hash chains */
		for (size_t k = 0; k < step; k++, i++) {
			if (i + MIN_MATCH <= n) {
				uint32_t h = hash3(d + i);
				prev[i % WINDOW] = head[h];
				head[h] = (int32_t)i;
			}
		}
	}
	put_litlen(&w, 256); /* end of block */
	if (w.n > 0) {
		put8(out, (uint8_t)w.acc);
	}
	free(head);
	free(prev);
}

/* --- chunks --- */

static void chunk(struct buf *b, const char type[4], const uint8_t *data, size_t len) {
	put32be(b, (uint32_t)len);
	size_t start = b->len;
	put(b, type, 4);
	if (len) {
		put(b, data, len);
	}
	uint32_t crc = b->failed ? 0 : crc_update(0xFFFFFFFFu, b->d + start, 4 + len) ^ 0xFFFFFFFFu;
	put32be(b, crc);
}

/* One scanline, filtered by whichever of None, Sub and Up looks smallest. */
static void filter_row(const uint8_t *row, const uint8_t *up, size_t n, uint8_t *out) {
	uint8_t *cand[3];
	static uint8_t *scratch;
	static size_t scratch_n;
	if (scratch_n < 3 * n) {
		free(scratch);
		scratch = malloc(3 * n);
		scratch_n = scratch ? 3 * n : 0;
	}
	if (!scratch) {
		out[0] = 0;
		memcpy(out + 1, row, n);
		return;
	}
	for (int f = 0; f < 3; f++) {
		cand[f] = scratch + (size_t)f * n;
	}
	unsigned long cost[3] = {0, 0, 0};
	for (size_t i = 0; i < n; i++) {
		uint8_t left = i >= 3 ? row[i - 3] : 0;
		uint8_t above = up ? up[i] : 0;
		cand[0][i] = row[i];
		cand[1][i] = (uint8_t)(row[i] - left);
		cand[2][i] = (uint8_t)(row[i] - above);
		for (int f = 0; f < 3; f++) {
			int8_t s = (int8_t)cand[f][i];
			cost[f] += (unsigned long)(s < 0 ? -s : s);
		}
	}
	int best = 0;
	for (int f = 1; f < 3; f++) {
		if (cost[f] < cost[best]) {
			best = f;
		}
	}
	out[0] = (uint8_t)best;
	memcpy(out + 1, cand[best], n);
}

uint8_t *png_encode(const uint32_t *pixels, int width, int height, size_t stride,
		const struct png_text *texts, int ntexts, size_t *out_len) {
	crc_init();
	if (width <= 0 || height <= 0) {
		return NULL;
	}
	size_t row_n = (size_t)width * 3;
	size_t raw_n = (row_n + 1) * (size_t)height;
	uint8_t *raw = malloc(raw_n);
	uint8_t *rgb = malloc(row_n * 2);
	if (!raw || !rgb) {
		free(raw);
		free(rgb);
		return NULL;
	}
	for (int y = 0; y < height; y++) {
		const uint32_t *src = (const uint32_t *)((const uint8_t *)pixels + (size_t)y * stride);
		uint8_t *cur = rgb + (size_t)(y & 1) * row_n;
		uint8_t *up = y ? rgb + (size_t)((y - 1) & 1) * row_n : NULL;
		for (int x = 0; x < width; x++) {
			cur[x * 3] = (uint8_t)(src[x] >> 16);
			cur[x * 3 + 1] = (uint8_t)(src[x] >> 8);
			cur[x * 3 + 2] = (uint8_t)src[x];
		}
		filter_row(cur, up, row_n, raw + (size_t)y * (row_n + 1));
	}
	free(rgb);

	struct buf z = {0};
	put8(&z, 0x78);
	put8(&z, 0x01);
	deflate(&z, raw, raw_n);
	put32be(&z, adler32(raw, raw_n));
	free(raw);

	struct buf b = {0};
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	put(&b, sig, 8);
	uint8_t ihdr[13] = {
		(uint8_t)(width >> 24), (uint8_t)(width >> 16), (uint8_t)(width >> 8), (uint8_t)width,
		(uint8_t)(height >> 24), (uint8_t)(height >> 16), (uint8_t)(height >> 8), (uint8_t)height,
		8, 2, 0, 0, 0, /* 8-bit RGB, deflate, adaptive filters, no interlace */
	};
	chunk(&b, "IHDR", ihdr, sizeof(ihdr));
	for (int t = 0; t < ntexts; t++) {
		/* iTXt: keyword, NUL, uncompressed, method 0, no language, no
		 * translated keyword, then UTF-8 text */
		struct buf tx = {0};
		put(&tx, texts[t].key, strlen(texts[t].key));
		static const uint8_t mid[5] = {0, 0, 0, 0, 0};
		put(&tx, mid, 5);
		put(&tx, texts[t].value, strlen(texts[t].value));
		if (!tx.failed) {
			chunk(&b, "iTXt", tx.d, tx.len);
		}
		b.failed |= tx.failed;
		free(tx.d);
	}
	if (z.failed) {
		b.failed = true;
	} else {
		chunk(&b, "IDAT", z.d, z.len);
	}
	free(z.d);
	chunk(&b, "IEND", NULL, 0);
	if (b.failed) {
		free(b.d);
		return NULL;
	}
	*out_len = b.len;
	return b.d;
}

bool png_write(const char *path, const uint32_t *pixels, int width, int height,
		size_t stride, const struct png_text *texts, int ntexts) {
	size_t len;
	uint8_t *data = png_encode(pixels, width, height, stride, texts, ntexts, &len);
	if (!data) {
		return false;
	}
	char tmp[4096];
	snprintf(tmp, sizeof(tmp), "%s.part", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	bool ok = fd >= 0;
	for (size_t o = 0; ok && o < len;) {
		ssize_t w = write(fd, data + o, len - o);
		if (w < 0 && errno == EINTR) {
			continue;
		}
		ok = w > 0;
		o += ok ? (size_t)w : 0;
	}
	if (fd >= 0) {
		ok = close(fd) == 0 && ok;
	}
	ok = ok && rename(tmp, path) == 0;
	if (!ok) {
		unlink(tmp);
	}
	free(data);
	return ok;
}

bool png_is_png(const uint8_t *data, size_t len) {
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	return len >= 8 && memcmp(data, sig, 8) == 0;
}

static uint32_t get32be(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

char *png_text_get(const uint8_t *data, size_t len, const char *key) {
	if (!png_is_png(data, len)) {
		return NULL;
	}
	size_t klen = strlen(key);
	for (size_t o = 8; o + 12 <= len;) {
		uint32_t n = get32be(data + o);
		if (n > len - o - 12) {
			return NULL; /* cut short */
		}
		const uint8_t *type = data + o + 4, *d = data + o + 8;
		bool text = memcmp(type, "tEXt", 4) == 0, itext = memcmp(type, "iTXt", 4) == 0;
		if ((text || itext) && n > klen && memcmp(d, key, klen) == 0 && d[klen] == 0) {
			const uint8_t *v = d + klen + 1, *end = d + n;
			if (itext) {
				if (end - v < 2 || v[0] != 0) {
					return NULL; /* compressed: not ours */
				}
				v += 2;
				for (int skip = 0; skip < 2; skip++) { /* language, translated keyword */
					const uint8_t *z = memchr(v, 0, (size_t)(end - v));
					if (!z) {
						return NULL;
					}
					v = z + 1;
				}
			}
			size_t vl = (size_t)(end - v);
			char *out = malloc(vl + 1);
			if (out) {
				memcpy(out, v, vl);
				out[vl] = '\0';
			}
			return out;
		}
		if (memcmp(type, "IEND", 4) == 0) {
			break;
		}
		o += 12 + n;
	}
	return NULL;
}
