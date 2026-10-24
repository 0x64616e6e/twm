#ifndef TWM_LEVELS_H
#define TWM_LEVELS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Volume and brightness (LVL-*): the shell lines that change them with
 * whatever the system has (PipeWire's wpctl, PulseAudio's pactl, ALSA's
 * amixer, FreeBSD's mixer; brightnessctl, the backlight in sysfs, FreeBSD's
 * backlight) and print the new level, and the reading of what they print.
 * Pure C, no wlroots; unit-tested.
 */

enum level_action {
	LEVEL_VOLUME_UP,
	LEVEL_VOLUME_DOWN,
	LEVEL_VOLUME_MUTE, /* toggle */
	LEVEL_MIC_MUTE,    /* toggle */
	LEVEL_BRIGHTER,
	LEVEL_DIMMER,
};

/* The /bin/sh line for `action`, `step` percent (1 … 50). */
void level_script(enum level_action action, int step, char *out, size_t len);

/*
 * What the line printed: the level in percent (-1 if it shows none, as for
 * a mute toggle on some tools) and whether it is muted. False when no tool
 * was found ("none") or nothing could be read.
 */
bool level_parse(const char *out, int *percent, bool *muted);

/* "41%  ■■■■■■■■············" (a 20-cell bar), or "muted"; for the root line. */
void level_text(int percent, bool muted, char *out, size_t len);

#endif
