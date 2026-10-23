#include <string.h>
#include "hintcode.h"

size_t hint_codes(size_t n, const char *keys, char codes[][4]) {
	size_t k = strlen(keys);
	if (k == 0) {
		return 0;
	}
	if (n <= k) {
		for (size_t i = 0; i < n; i++) {
			codes[i][0] = keys[i];
			codes[i][1] = '\0';
		}
		return n;
	}
	size_t made = 0;
	for (size_t i = 0; i < k && made < n; i++) {
		for (size_t j = 0; j < k && made < n; j++) {
			codes[made][0] = keys[i];
			codes[made][1] = keys[j];
			codes[made][2] = '\0';
			made++;
		}
	}
	return made;
}
