/* Unit tests for tab layout (FRM-3 … FRM-6, FRM-11). */
#include "check.h"
#include "squeeze.h"

/* B0 metrics: 26 px tab, 1 px separators, 14 px padding, 72 px hatch, 8 gap. */
static const struct tab_metrics m = {
	.title_height = 26, .separator = 1, .pad = 14,
	.hatch_width = 72, .hatch_gap = 8, .ellipsis_width = 8,
};

int main(void) {
	struct tab_layout l;

	/* FRM-3, FRM-7: B0's focused xterm, 860 wide frame. */
	tab_layout_compute(&m, &(struct tab_input){
		.frame_width = 860, .text_width = 120, .hatch = true,
		.iconify = true, .resize = true, .justify = TWM_JUSTIFY_LEFT}, &l);
	CHECK_EQ("FRM-2", l.x, 0);
	CHECK_EQ("FRM-3", l.iconify.x, 0);
	CHECK_EQ("FRM-3", l.iconify.width, 26);
	CHECK_EQ("FRM-3", l.sep_left.x, 26);
	CHECK_EQ("FRM-3", l.text.x, 26 + 1 + 14);
	CHECK_EQ("FRM-3", l.text.width, 120);
	CHECK_EQ("FRM-3", l.hatch.x, 41 + 120 + 14);
	CHECK_EQ("FRM-3", l.hatch.width, 72);
	CHECK_EQ("FRM-3", l.sep_right.x, 175 + 72 + 8);
	CHECK_EQ("FRM-3", l.resize.x, 256);
	CHECK_EQ("FRM-3", l.width, 26 + 1 + 14 + 120 + 14 + 72 + 8 + 1 + 26);
	CHECK("FRM-5", !l.truncated);

	/* FRM-8, FRM-11: unfocused is 80 px narrower, left edge fixed. */
	int focused_width = l.width;
	tab_layout_compute(&m, &(struct tab_input){
		.frame_width = 860, .text_width = 120, .hatch = false,
		.iconify = true, .resize = true, .justify = TWM_JUSTIFY_LEFT}, &l);
	CHECK_EQ("FRM-11", focused_width - l.width, 80);
	CHECK_EQ("FRM-11", l.x, 0);
	CHECK_EQ("FRM-8", l.hatch.width, 0);
	CHECK_EQ("FRM-8", l.sep_right.x, 41 + 120 + 14);

	/* FRM-6: fixed-size client, no resize box or right separator. */
	tab_layout_compute(&m, &(struct tab_input){
		.frame_width = 232, .text_width = 43, .iconify = true,
		.resize = false, .justify = TWM_JUSTIFY_LEFT}, &l);
	CHECK_EQ("FRM-6", l.resize.width, 0);
	CHECK_EQ("FRM-6", l.sep_right.width, 0);
	CHECK_EQ("FRM-6", l.width, 26 + 1 + 14 + 43 + 14);

	/* FRM-4: center and right justification. */
	tab_layout_compute(&m, &(struct tab_input){
		.frame_width = 600, .text_width = 100, .iconify = true, .resize = true,
		.justify = TWM_JUSTIFY_RIGHT}, &l);
	CHECK_EQ("FRM-4", l.x + l.width, 600);
	tab_layout_compute(&m, &(struct tab_input){
		.frame_width = 600, .text_width = 100, .iconify = true, .resize = true,
		.justify = TWM_JUSTIFY_CENTER}, &l);
	CHECK_EQ("FRM-4", l.x, (600 - l.width) / 2);

	/* FRM-4 off: tab spans the frame; focused hatch fills the gap. */
	tab_layout_compute(&m, &(struct tab_input){
		.frame_width = 600, .text_width = 100, .hatch = true, .iconify = true,
		.resize = true, .justify = TWM_JUSTIFY_OFF}, &l);
	CHECK_EQ("FRM-4", l.width, 600);
	CHECK_EQ("FRM-4", l.resize.x + l.resize.width, 600);
	CHECK_EQ("FRM-4", l.hatch.x + l.hatch.width + 8, l.sep_right.x);

	/* FRM-5: a long title is cut; the hatch and both boxes stay. */
	tab_layout_compute(&m, &(struct tab_input){
		.frame_width = 300, .text_width = 500, .hatch = true, .iconify = true,
		.resize = true, .justify = TWM_JUSTIFY_LEFT}, &l);
	CHECK("FRM-5", l.truncated);
	CHECK_EQ("FRM-5", l.width, 300);
	CHECK_EQ("FRM-5", l.hatch.width, 72);
	CHECK_EQ("FRM-5", l.resize.x + l.resize.width, 300);
	CHECK_EQ("FRM-5", l.text.width, 300 - (26 + 1 + 28 + 1 + 26) - 80);

	/* FRM-5: narrower still, the hatch shrinks after the title is at "…". */
	int min = tab_min_width(&m, true, true);
	tab_layout_compute(&m, &(struct tab_input){
		.frame_width = min, .text_width = 500, .hatch = true, .iconify = true,
		.resize = true, .justify = TWM_JUSTIFY_LEFT}, &l);
	CHECK_EQ("FRM-5", l.width, min);
	CHECK_EQ("FRM-5", l.text.width, 8);
	CHECK_EQ("FRM-5", l.hatch.width, 0);
	CHECK_EQ("FRM-5", l.iconify.width, 26);
	CHECK_EQ("FRM-5", l.resize.width, 26);

	/* FRM-18: utility tab, no buttons. */
	struct tab_metrics um = m;
	um.pad = 12;
	tab_layout_compute(&um, &(struct tab_input){
		.frame_width = 236, .text_width = 86, .justify = TWM_JUSTIFY_LEFT}, &l);
	CHECK_EQ("FRM-18", l.width, 12 + 86 + 12);
	CHECK_EQ("FRM-18", l.text.x, 12);

	/* Hit parts for FRM-12. */
	tab_layout_compute(&m, &(struct tab_input){
		.frame_width = 860, .text_width = 120, .hatch = true,
		.iconify = true, .resize = true, .justify = TWM_JUSTIFY_LEFT}, &l);
	CHECK_EQ("FRM-12", tab_part_at(&l, 5), TAB_PART_ICONIFY);
	CHECK_EQ("FRM-12", tab_part_at(&l, 26), TAB_PART_TITLE);
	CHECK_EQ("FRM-12", tab_part_at(&l, 200), TAB_PART_TITLE);
	CHECK_EQ("FRM-12", tab_part_at(&l, l.resize.x), TAB_PART_RESIZE);
	CHECK_EQ("FRM-12", tab_part_at(&l, l.width), TAB_PART_NONE);
	return UNIT_RESULT();
}
