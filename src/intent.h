#ifndef TWM_INTENT_H
#define TWM_INTENT_H

#include <stdint.h>

/*
 * Sloppy focus with an intent delay (FOC-1 … FOC-3). The pointer must stay
 * inside one frame for `delay` ms before keyboard focus moves there; a fast
 * crossing never moves it. Pure state machine; the caller owns the timer.
 * Targets are opaque pointers (views), NULL meaning the root.
 */

struct focus_intent {
	const void *candidate;
	uint32_t since; /* ms, when the pointer entered `candidate` */
};

enum intent_action {
	INTENT_NONE,   /* keep whatever timer is running */
	INTENT_CANCEL, /* stop the timer; focus stays */
	INTENT_ARM,    /* (re)start the timer for *deadline */
	INTENT_COMMIT, /* focus `candidate` now (delay 0) */
};

/* The pointer is over `under` at `now`. */
enum intent_action focus_intent_motion(struct focus_intent *fi, const void *under,
	const void *focused, uint32_t now, uint32_t delay, uint32_t *deadline);

/* The timer fired: returns the target to focus, or NULL if the pointer has
 * moved on or hasn't rested long enough yet. */
const void *focus_intent_fire(struct focus_intent *fi, const void *under,
	uint32_t now, uint32_t delay);

/* Focus changed for another reason (a click, FOC-5): start over from it. */
void focus_intent_reset(struct focus_intent *fi, const void *focused, uint32_t now);

#endif
