#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "levels.h"

#define HAVE(tool) "command -v " tool " >/dev/null 2>&1"

void level_script(enum level_action action, int step, char *out, size_t len) {
	if (step < 1) {
		step = 1;
	} else if (step > 50) {
		step = 50;
	}
	char frac[8]; /* FreeBSD's mixer takes fractions: 5 % is 0.05 */
	snprintf(frac, sizeof(frac), "%d.%02d", step / 100, step % 100);
	switch (action) {
	case LEVEL_VOLUME_UP:
	case LEVEL_VOLUME_DOWN: {
		char sign = action == LEVEL_VOLUME_UP ? '+' : '-';
		snprintf(out, len,
			"if " HAVE("wpctl") "; then wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ %d%%%c && "
			"wpctl get-volume @DEFAULT_AUDIO_SINK@; "
			"elif " HAVE("pactl") "; then pactl set-sink-volume @DEFAULT_SINK@ %c%d%% && "
			"pactl get-sink-volume @DEFAULT_SINK@ && pactl get-sink-mute @DEFAULT_SINK@; "
			"elif " HAVE("amixer") "; then amixer -q sset Master %d%%%c && amixer sget Master; "
			"elif " HAVE("mixer") "; then mixer vol.volume=%c%s >/dev/null && mixer vol; "
			"else echo none; fi",
			step, sign, sign, step, step, sign, sign, frac);
		return;
	}
	case LEVEL_VOLUME_MUTE:
		snprintf(out, len,
			"if " HAVE("wpctl") "; then wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle && "
			"wpctl get-volume @DEFAULT_AUDIO_SINK@; "
			"elif " HAVE("pactl") "; then pactl set-sink-mute @DEFAULT_SINK@ toggle && "
			"pactl get-sink-volume @DEFAULT_SINK@ && pactl get-sink-mute @DEFAULT_SINK@; "
			"elif " HAVE("amixer") "; then amixer -q sset Master toggle && amixer sget Master; "
			"elif " HAVE("mixer") "; then mixer vol.mute=^ >/dev/null && mixer vol; "
			"else echo none; fi");
		return;
	case LEVEL_MIC_MUTE:
		snprintf(out, len,
			"if " HAVE("wpctl") "; then wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle && "
			"wpctl get-volume @DEFAULT_AUDIO_SOURCE@; "
			"elif " HAVE("pactl") "; then pactl set-source-mute @DEFAULT_SOURCE@ toggle && "
			"pactl get-source-volume @DEFAULT_SOURCE@ && pactl get-source-mute @DEFAULT_SOURCE@; "
			"elif " HAVE("amixer") "; then amixer -q sset Capture toggle && amixer sget Capture; "
			"elif " HAVE("mixer") "; then mixer rec.mute=^ >/dev/null && mixer rec; "
			"else echo none; fi");
		return;
	case LEVEL_BRIGHTER:
	case LEVEL_DIMMER: {
		char sign = action == LEVEL_BRIGHTER ? '+' : '-';
		/* never all the way to black: a screen at 0 can't be seen to turn up */
		snprintf(out, len,
			"if " HAVE("brightnessctl") "; then brightnessctl -q --min-value=1 set %d%%%c && "
			"brightnessctl -m; "
			"elif d=$(ls -d /sys/class/backlight/* 2>/dev/null | head -n 1) && [ -w \"$d/brightness\" ]; "
			"then m=$(cat \"$d/max_brightness\"); c=$(cat \"$d/brightness\"); "
			"n=$((c %c m * %d / 100)); [ $n -gt $m ] && n=$m; [ $n -lt 1 ] && n=1; "
			"echo $n > \"$d/brightness\" && echo \"$((n * 100 / m))%%\"; "
			"elif " HAVE("backlight") "; then backlight %s %d && backlight -q; "
			"else echo none; fi",
			step, sign, sign, step, action == LEVEL_BRIGHTER ? "incr" : "decr", step);
		return;
	}
	}
	snprintf(out, len, "echo none");
}

bool level_parse(const char *out, int *percent, bool *muted) {
	*percent = -1;
	*muted = false;
	if (!out || strncmp(out, "none", 4) == 0) {
		return false;
	}
	*muted = strstr(out, "MUTED") || strstr(out, "Mute: yes") || strstr(out, "[off]") ||
		strstr(out, "mute=1") || strstr(out, "mute=on");
	/* "41%" (pactl, amixer, brightnessctl -m, the sysfs line) */
	for (const char *p = out; *p; p++) {
		if (*p >= '0' && *p <= '9' && (p == out || p[-1] < '0' || p[-1] > '9')) {
			char *end;
			long v = strtol(p, &end, 10);
			if (*end == '%') {
				*percent = (int)v;
				return true;
			}
		}
	}
	/* "Volume: 0.41" (wpctl), "vol.volume=0.41:0.41" (FreeBSD's mixer) */
	const char *keys[] = {"Volume:", "volume="};
	for (int k = 0; k < 2; k++) {
		const char *at = strstr(out, keys[k]);
		if (at) {
			*percent = (int)lround(strtod(at + strlen(keys[k]), NULL) * 100);
			return true;
		}
	}
	/* a plain number (FreeBSD's backlight -q) */
	for (const char *p = out; *p; p++) {
		if (*p >= '0' && *p <= '9') {
			*percent = (int)strtol(p, NULL, 10);
			return true;
		}
	}
	return *muted;
}

void level_text(int percent, bool muted, char *out, size_t len) {
	if (percent < 0) {
		snprintf(out, len, "%s", muted ? "muted" : "on");
		return;
	}
	int p = percent > 100 ? 100 : percent;
	int cells = (p + 2) / 5; /* of 20 */
	size_t o = (size_t)snprintf(out, len, "%3d%%  ", percent);
	for (int i = 0; i < 20 && o + 4 < len; i++) {
		const char *c = i < cells ? "\xe2\x96\xa0" : "\xc2\xb7"; /* ■ or · */
		size_t cl = strlen(c);
		memcpy(out + o, c, cl);
		o += cl;
	}
	out[o] = '\0';
	if (muted && o + 9 < len) {
		snprintf(out + o, len - o, "  muted");
	}
}
