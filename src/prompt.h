#ifndef TWM_PROMPT_H
#define TWM_PROMPT_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

/*
 * The trusted frame (TRU-1 … TRU-6): prompts only twm can draw. A weave band
 * inside an accent frame, a lock in the tab, stacked above every client,
 * the desktop dimmed one step, all input going to the prompt and synthetic
 * input to nothing. One prompt shows at a time; others wait their turn.
 */

#define PROMPT_MAX_CHOICES 12

struct twm_server;
struct twm_prompt;

typedef void (*prompt_done_fn)(void *data, bool granted, int choice, bool checked);

struct prompt_spec {
	char purpose[48];  /* tab: "system · <purpose>" */
	char heading[120]; /* twm's own words, in the heading style */
	char client[160];  /* what the client says (app, origin): ink-2, verbatim */
	char detail[240];  /* more of the client's words, same style; may be empty */
	char legend[32];   /* "SHARE"; empty for none */
	char choices[PROMPT_MAX_CHOICES][96];
	int nchoices;
	int selected;
	char checkbox[64]; /* empty for none */
	bool checked;
	char deny[32], grant[32];
	prompt_done_fn done;
	void *data;
};

/* Queue a prompt; `done` is called exactly once, later (denied if closed).
 * NULL (and no `done`) if it can't be shown. */
struct twm_prompt *prompt_open(struct twm_server *server, const struct prompt_spec *spec);
/* Withdraw a prompt (its asker went away): `done` runs as denied. */
void prompt_close(struct twm_server *server, struct twm_prompt *prompt);
bool prompt_active(struct twm_server *server);

/* Input while a prompt is up (from real, or trusted test, devices). */
void prompt_key(struct twm_server *server, xkb_keysym_t sym);
/* A button press or release at the cursor. */
void prompt_pointer_button(struct twm_server *server, uint32_t button, bool pressed);
/* Recompute the dim overlay (outputs changed). */
void prompt_layout(struct twm_server *server);

void prompt_init(struct twm_server *server);
void prompt_finish(struct twm_server *server);

#endif
