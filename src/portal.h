#ifndef TWM_PORTAL_H
#define TWM_PORTAL_H

#include "config.h"

struct twm_server;

/*
 * D-Bus, through the sd-bus API (libsystemd on Linux, basu on FreeBSD):
 *
 * - org.freedesktop.impl.portal.desktop.twm: the Access portal backend.
 *   Consent dialogs are trusted prompts (TRU-1).
 * - org.freedesktop.Notifications, when the name is free and the config
 *   allows: notifications become root-line messages (ROOT-7, no toasts).
 * - When twm is the session, the D-Bus (and systemd) activation environment
 *   learns WAYLAND_DISPLAY, DISPLAY and XDG_CURRENT_DESKTOP=twm, so portals
 *   started on demand find this display.
 *
 * Without a session bus all of this is skipped, quietly.
 */

#if HAVE_PORTAL
void portal_init(struct twm_server *server);
void portal_finish(struct twm_server *server);
/* The config changed (Notifications on/off). */
void portal_config_changed(struct twm_server *server);
#else
static inline void portal_init(struct twm_server *server) {}
static inline void portal_finish(struct twm_server *server) {}
static inline void portal_config_changed(struct twm_server *server) {}
#endif

#endif
