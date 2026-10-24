/* Unit tests for volume and brightness (LVL-*). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "levels.h"

static int parsed(const char *out, int pct, bool muted) {
	int p;
	bool m;
	return level_parse(out, &p, &m) && p == pct && m == muted;
}

int main(void) {
	/* what each tool prints */
	CHECK("LVL-2", parsed("Volume: 0.41\n", 41, false));                     /* wpctl */
	CHECK("LVL-2", parsed("Volume: 0.41 [MUTED]\n", 41, true));
	CHECK("LVL-2", parsed("Volume: front-left: 27000 /  41% / -23.30 dB,   front-right: 27000 /  41%\n"
		"Mute: no\n", 41, false));                                              /* pactl */
	CHECK("LVL-2", parsed("Volume: front-left: 0 /   0%\nMute: yes\n", 0, true));
	CHECK("LVL-2", parsed("Simple mixer control 'Master',0\n  Mono: Playback 52 [65%] [-15.75dB] [off]\n",
		65, true));                                                             /* amixer */
	CHECK("LVL-2", parsed("vol.volume=0.75:0.75\nvol.mute=0\n", 75, false));  /* FreeBSD mixer */
	CHECK("LVL-2", parsed("intel_backlight,backlight,96002,55%,174545\n", 55, false)); /* brightnessctl */
	CHECK("LVL-2", parsed("48%\n", 48, false));                              /* the sysfs line */
	CHECK("LVL-2", parsed("50\n", 50, false));                               /* FreeBSD backlight */
	int p;
	bool m;
	CHECK("LVL-3", !level_parse("none\n", &p, &m));                          /* no tool */
	CHECK("LVL-3", !level_parse("", &p, &m));

	/* the lines: the step in each tool's own terms */
	char s[2048];
	level_script(LEVEL_VOLUME_UP, 5, s, sizeof(s));
	CHECK("LVL-1", strstr(s, "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+") &&
		strstr(s, "pactl set-sink-volume @DEFAULT_SINK@ +5%") && strstr(s, "amixer -q sset Master 5%+") &&
		strstr(s, "mixer vol.volume=+0.05"));
	level_script(LEVEL_VOLUME_DOWN, 10, s, sizeof(s));
	CHECK("LVL-1", strstr(s, "@DEFAULT_AUDIO_SINK@ 10%-") && strstr(s, "mixer vol.volume=-0.10"));
	level_script(LEVEL_DIMMER, 5, s, sizeof(s));
	CHECK("LVL-1", strstr(s, "brightnessctl -q --min-value=1 set 5%-") && strstr(s, "backlight decr 5") &&
		strstr(s, "[ $n -lt 1 ] && n=1"));
	level_script(LEVEL_MIC_MUTE, 5, s, sizeof(s));
	CHECK("LVL-1", strstr(s, "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle") != NULL);

	/* the root line's words */
	char t[128];
	level_text(41, false, t, sizeof(t));
	CHECK("LVL-4", strncmp(t, " 41%  ", 6) == 0 && strstr(t, "\xe2\x96\xa0") && !strstr(t, "muted"));
	level_text(100, false, t, sizeof(t));
	CHECK("LVL-4", strstr(t, "\xc2\xb7") == NULL); /* full: no empty cell */
	level_text(41, true, t, sizeof(t));
	CHECK("LVL-4", strstr(t, "muted") != NULL);
	level_text(-1, true, t, sizeof(t));
	CHECK("LVL-4", strcmp(t, "muted") == 0);
	return UNIT_RESULT();
}
