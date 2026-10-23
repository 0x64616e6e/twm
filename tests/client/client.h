#ifndef TWM_TEST_CLIENT_H
#define TWM_TEST_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Test client for screenshot tests (DESIGN.md D-7). It talks to twm only
 * through Wayland protocols: xdg-shell for windows, wlr-virtual-pointer for
 * input, wlr-screencopy for pixels.
 */

#define TC_BTN_LEFT 0x110
#define TC_BTN_RIGHT 0x111
#define TC_BTN_MIDDLE 0x112

/* evdev key codes (virtual keyboard). */
#define TC_KEY_ESC 1
#define TC_KEY_R 19
#define TC_KEY_ENTER 28
#define TC_KEY_H 35
#define TC_KEY_LEFTSHIFT 42
#define TC_KEY_Z 44
#define TC_KEY_M 50
#define TC_KEY_UP 103
#define TC_KEY_LEFT 105
#define TC_KEY_RIGHT 106
#define TC_KEY_DOWN 108
#define TC_KEY_LEFTMETA 125

struct tc;
struct tc_window;

struct tc_image {
	int width, height;
	uint32_t *pixels; /* 0xFFRRGGBB, top-down */
};

struct tc_window_opts {
	const char *title;
	const char *app_id;
	int width, height;
	uint32_t color; /* 0xRRGGBB */
	bool fixed_size; /* min == max == width x height (FRM-6) */
	bool request_ssd;
};

struct tc *tc_connect(void);
void tc_disconnect(struct tc *tc);
int tc_output_width(struct tc *tc);
int tc_output_height(struct tc *tc);

struct tc_window *tc_window_create(struct tc *tc, const struct tc_window_opts *opts);
void tc_window_destroy(struct tc_window *win);
bool tc_window_activated(struct tc_window *win);
void tc_window_size(struct tc_window *win, int *w, int *h);
/* Ask to be activated without a token (URG-2 in M1). */
void tc_window_request_activation(struct tc_window *win);

/* Virtual pointer, layout coordinates of the single output. */
void tc_pointer_move(struct tc *tc, double x, double y);
void tc_pointer_button(struct tc *tc, uint32_t button, bool pressed);
void tc_pointer_click(struct tc *tc, uint32_t button);

/* Virtual keyboard (default xkb keymap). */
void tc_key(struct tc *tc, uint32_t key, bool pressed);
/* Press the keys in order, release them in reverse: tc_chord(tc, 3,
 * TC_KEY_LEFTMETA, TC_KEY_LEFTSHIFT, TC_KEY_Z) is M-S-z. */
void tc_chord(struct tc *tc, int n, ...);

/* Dispatch events for `ms` milliseconds. */
void tc_wait(struct tc *tc, int ms);
void tc_roundtrip(struct tc *tc);

bool tc_screenshot(struct tc *tc, struct tc_image *img);
void tc_image_free(struct tc_image *img);
uint32_t tc_pixel(const struct tc_image *img, int x, int y);
bool tc_image_write_png(const struct tc_image *img, const char *path);

/*
 * Checks. Each prints one line naming the requirement it covers and counts
 * failures; tc_report() prints the summary and returns the exit status.
 */
bool tc_expect_pixel(const char *req, const struct tc_image *img,
	int x, int y, uint32_t rgb, int tolerance);
bool tc_expect(const char *req, bool ok, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
int tc_report(void);

/* Opaque result of `top` at `alpha` over opaque `bottom`, both 0xRRGGBB,
 * rounded like the compositor rounds (nearest). */
uint32_t tc_blend(uint32_t top, double alpha, uint32_t bottom);

#endif
