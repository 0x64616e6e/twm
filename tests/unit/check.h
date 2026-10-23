/* Tiny check helper shared by the unit tests. */
#ifndef TWM_UNIT_CHECK_H
#define TWM_UNIT_CHECK_H
#include <stdio.h>

static int unit_failures;

#define CHECK(req, cond) do { \
	int ok_ = (cond); \
	printf("%s %-7s %s\n", ok_ ? "ok  " : "FAIL", req, #cond); \
	unit_failures += !ok_; \
} while (0)

#define CHECK_EQ(req, got, want) do { \
	long g_ = (long)(got), w_ = (long)(want); \
	printf("%s %-7s %s == %ld (got %ld)\n", g_ == w_ ? "ok  " : "FAIL", req, #got, w_, g_); \
	unit_failures += g_ != w_; \
} while (0)

#define UNIT_RESULT() (unit_failures ? 1 : 0)
#endif
