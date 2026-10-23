#ifndef TWM_HINTCODE_H
#define TWM_HINTCODE_H

#include <stddef.h>

/*
 * HNT-2: codes for hint mode. With at least as many keys as windows, one
 * letter each, in key order; otherwise two letters (keys[i] keys[j]). Pure.
 * Returns how many codes were made (at most keys², at most `n`).
 */
size_t hint_codes(size_t n, const char *keys, char codes[][4]);

#endif
