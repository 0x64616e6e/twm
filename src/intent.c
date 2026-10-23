#include <stddef.h>
#include "intent.h"

enum intent_action focus_intent_motion(struct focus_intent *fi, const void *under,
		const void *focused, uint32_t now, uint32_t delay, uint32_t *deadline) {
	if (under == fi->candidate) {
		return INTENT_NONE; /* FOC-3: motion inside the same frame */
	}
	fi->candidate = under;
	fi->since = now;
	if (!under || under == focused) {
		return INTENT_CANCEL; /* FOC-1: the root never takes focus */
	}
	if (delay == 0) {
		return INTENT_COMMIT;
	}
	*deadline = now + delay;
	return INTENT_ARM;
}

const void *focus_intent_fire(struct focus_intent *fi, const void *under,
		uint32_t now, uint32_t delay) {
	if (!fi->candidate || under != fi->candidate) {
		return NULL;
	}
	if ((uint32_t)(now - fi->since) < delay) {
		return NULL;
	}
	return fi->candidate;
}

void focus_intent_reset(struct focus_intent *fi, const void *focused, uint32_t now) {
	fi->candidate = focused;
	fi->since = now;
}
