#ifndef TWM_MENU_H
#define TWM_MENU_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include "server.h"

/*
 * Root menus (MNU-1 … MNU-7), drawn by twm in the menu layer. While one is
 * open it holds the pointer and the keyboard. Window functions picked from a
 * menu ask for their target with a crosshair click (MNU-5).
 */

/* Open menu `name` with its top-left at (x, y); `by_press` when a button
 * press opened it (press-drag-release, or click to keep it open). */
void menu_open(struct twm_server *server, const char *name, double x, double y, bool by_press);
void menu_close_all(struct twm_server *server);
bool menu_active(struct twm_server *server);

void menu_motion(struct twm_server *server, double x, double y);
void menu_button(struct twm_server *server, uint32_t button, bool pressed);
void menu_key(struct twm_server *server, xkb_keysym_t sym);

/* MNU-5: the next click on a window runs `func` on it. */
void select_begin(struct twm_server *server, const struct twm_func *func);
void select_cancel(struct twm_server *server);
/* A press while selecting: returns true (it's taken). */
bool select_press(struct twm_server *server);

/* "M-↵", "B2", "M-S-r": how a function's binding reads in a menu (MNU-3). */
void menu_shortcut(struct twm_server *server, const struct twm_func *func, char *buf, size_t len);

#endif
