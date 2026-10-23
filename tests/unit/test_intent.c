/* Unit tests for sloppy focus with intent (FOC-1 … FOC-3), fake clock. */
#include "check.h"
#include "intent.h"

static int A, B;

int main(void) {
	struct focus_intent fi = {0};
	uint32_t deadline = 0;
	const void *focused = &A;

	/* FOC-2 (B7): crossing B in 34 ms doesn't move focus. */
	focus_intent_reset(&fi, focused, 0);
	CHECK_EQ("FOC-2", focus_intent_motion(&fi, &B, focused, 1000, 80, &deadline), INTENT_ARM);
	CHECK_EQ("FOC-2", deadline, 1080);
	CHECK_EQ("FOC-1", focus_intent_motion(&fi, NULL, focused, 1034, 80, &deadline), INTENT_CANCEL);
	CHECK("FOC-2", focus_intent_fire(&fi, NULL, 1080, 80) == NULL);

	/* FOC-2: resting 80 ms commits. */
	CHECK_EQ("FOC-2", focus_intent_motion(&fi, &B, focused, 2000, 80, &deadline), INTENT_ARM);
	CHECK("FOC-2", focus_intent_fire(&fi, &B, 2079, 80) == NULL);
	CHECK("FOC-2", focus_intent_fire(&fi, &B, 2080, 80) == &B);

	/* FOC-3: motion inside the same frame doesn't restart the clock. */
	focus_intent_reset(&fi, focused, 3000);
	focus_intent_motion(&fi, &B, focused, 3000, 80, &deadline);
	CHECK_EQ("FOC-3", focus_intent_motion(&fi, &B, focused, 3050, 80, &deadline), INTENT_NONE);
	CHECK("FOC-3", focus_intent_fire(&fi, &B, 3080, 80) == &B);

	/* Leaving and coming back starts over ("continuously"). */
	focus_intent_reset(&fi, focused, 4000);
	focus_intent_motion(&fi, &B, focused, 4000, 80, &deadline);
	focus_intent_motion(&fi, NULL, focused, 4040, 80, &deadline);
	CHECK_EQ("FOC-2", focus_intent_motion(&fi, &B, focused, 4050, 80, &deadline), INTENT_ARM);
	CHECK("FOC-2", focus_intent_fire(&fi, &B, 4080, 80) == NULL);
	CHECK("FOC-2", focus_intent_fire(&fi, &B, 4130, 80) == &B);

	/* Entering the focused window cancels any pending switch. */
	focus_intent_reset(&fi, focused, 5000);
	focus_intent_motion(&fi, &B, focused, 5000, 80, &deadline);
	CHECK_EQ("FOC-1", focus_intent_motion(&fi, &A, focused, 5030, 80, &deadline), INTENT_CANCEL);

	/* FocusDelay 0: immediate. */
	focus_intent_reset(&fi, focused, 6000);
	CHECK_EQ("FOC-2", focus_intent_motion(&fi, &B, focused, 6000, 0, &deadline), INTENT_COMMIT);

	/* The ms clock wraps. */
	focus_intent_reset(&fi, focused, 0);
	focus_intent_motion(&fi, &B, focused, 0xFFFFFFF0u, 80, &deadline);
	CHECK("FOC-2", focus_intent_fire(&fi, &B, 0x40u, 80) == &B);
	return UNIT_RESULT();
}
