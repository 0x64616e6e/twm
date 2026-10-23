/* Unit tests for the group tab row (GRP-2, GRP-3, GRP-6) and hint codes (HNT-2). */
#include <string.h>
#include "check.h"
#include "hintcode.h"
#include "squeeze.h"

static const struct tab_metrics m = {
	.title_height = 26, .separator = 1, .pad = 14,
	.hatch_width = 72, .hatch_gap = 8, .ellipsis_width = 8,
};

int main(void) {
	struct tab_group_layout l;
	/* B2: "foot — build" (active, focused) and "foot — logs". */
	tab_group_layout_compute(&m, &(struct tab_group_input){
		.frame_width = 820, .n = 2, .text_width = {90, 80}, .active = 0, .hatch = true,
		.resize = true, .slot_index = -1}, &l);
	CHECK_EQ("GRP-3", l.ntabs, 2);
	CHECK_EQ("GRP-3", l.iconify.width, 26);
	CHECK_EQ("GRP-3", l.tabs[0].x, 27);
	CHECK_EQ("GRP-3", l.tabs[0].text_x, 27 + 14);
	CHECK_EQ("GRP-3", l.tabs[0].hatch_x, 41 + 90 + 12);
	CHECK_EQ("GRP-3", l.tabs[0].hatch_w, 38);
	CHECK_EQ("GRP-3", l.tabs[0].width, 14 + 90 + 12 + 38 + 14);
	CHECK_EQ("GRP-3", l.tabs[1].x, l.tabs[0].sep_x + 1);
	CHECK_EQ("GRP-3", l.tabs[1].hatch_w, 0);
	CHECK_EQ("GRP-3", l.resize.x, l.tabs[1].sep_x + 1);
	CHECK_EQ("GRP-3", l.width, l.resize.x + 26);
	CHECK_EQ("GRP-4", tab_group_tab_at(&l, l.tabs[1].x + 5), 1);
	CHECK_EQ("GRP-4", tab_group_tab_at(&l, 5), -1);

	/* GRP-2: a slot for a 120 px tab between the two. */
	tab_group_layout_compute(&m, &(struct tab_group_input){
		.frame_width = 820, .n = 2, .text_width = {90, 80}, .active = 0, .hatch = true,
		.resize = true, .slot_index = 1, .slot_width = 120}, &l);
	CHECK_EQ("GRP-2", l.ntabs, 3);
	CHECK_EQ("GRP-2", l.tabs[1].index, -1);
	CHECK_EQ("GRP-2", l.tabs[1].width, 120);
	CHECK_EQ("GRP-2", l.tabs[1].x, l.tabs[0].sep_x + 1 + 4);
	CHECK_EQ("GRP-2", l.tabs[2].x, l.tabs[1].x + 120 + 4);
	CHECK_EQ("GRP-2", tab_group_insert_at(&l, l.tabs[0].x + 5, 2), 0);
	CHECK_EQ("GRP-2", tab_group_insert_at(&l, l.tabs[2].x + 5, 2), 1);
	CHECK_EQ("GRP-2", tab_group_insert_at(&l, 2000, 2), 2);

	/* GRP-6: folded, only the active tab. */
	tab_group_layout_compute(&m, &(struct tab_group_input){
		.frame_width = 820, .n = 3, .text_width = {90, 80, 70}, .active = 1, .resize = true,
		.folded = true, .slot_index = -1}, &l);
	CHECK("GRP-6", l.ntabs == 1 && l.tabs[0].index == 1);
	CHECK_EQ("GRP-6", l.width, 26 + 1 + 14 + 80 + 14 + 1 + 26);

	/* Too wide: the longest title gives way first. */
	tab_group_layout_compute(&m, &(struct tab_group_input){
		.frame_width = 300, .n = 2, .text_width = {300, 60}, .active = 1, .resize = true,
		.slot_index = -1}, &l);
	CHECK_EQ("GRP-3", l.width, 300);
	CHECK_EQ("GRP-3", l.tabs[1].text_w, 60);
	CHECK("GRP-3", l.tabs[0].text_w < 300 && l.tabs[0].text_w >= 8);

	/* HNT-2: home row first; two letters past the keys. */
	char codes[100][4];
	CHECK_EQ("HNT-2", hint_codes(3, "asdfjklgh", codes), 3);
	CHECK("HNT-2", strcmp(codes[0], "a") == 0 && strcmp(codes[2], "d") == 0);
	CHECK_EQ("HNT-2", hint_codes(12, "asdfjklgh", codes), 12);
	CHECK("HNT-2", strcmp(codes[0], "aa") == 0 && strcmp(codes[9], "sa") == 0);
	CHECK_EQ("HNT-2", hint_codes(100, "as", codes), 4);
	return UNIT_RESULT();
}
