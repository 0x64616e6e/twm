#ifndef TWM_GRAB_STATE_H
#define TWM_GRAB_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "geometry.h"
#include "snap.h"

struct twm_view;

/* State of the current interactive operation (see grab.h). */
enum twm_grab_mode {
	TWM_GRAB_NONE,
	TWM_GRAB_PENDING_MOVE, /* B1 on a title: raise, move once dragged 4 px */
	TWM_GRAB_MOVE,
	TWM_GRAB_RESIZE,
	TWM_GRAB_PLACE,        /* outline hangs from the pointer */
	TWM_GRAB_PLACE_SIZE,   /* B2 held during placement: drag out the size */
};

struct twm_grab {
	enum twm_grab_mode mode;
	struct twm_view *view;
	uint32_t button;   /* the button that ends it; 0 for keyboard */
	double cx, cy;     /* pointer at the start */
	struct geo_box start, box;
	int start_w, start_h; /* client size at the start (HIS-1) */
	uint32_t edges;
	int key_dx, key_dy; /* arrow-key nudges (OUT-7) */
	bool free;          /* SNP-7: M pressed during the drag */
	bool mod_held;      /* the modifier as of the last key event */
	struct snap_result snap;
	struct geo_box output; /* the output the outline is on */
};

#endif
