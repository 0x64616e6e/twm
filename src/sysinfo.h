#ifndef TWM_SYSINFO_H
#define TWM_SYSINFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The machine's own facts for twmfetch and the root card (FET-3, CARD-2):
 * Linux and FreeBSD. No wlroots. The parsers are separate from the reading,
 * so the unit tests feed them text.
 */

struct sys_mem {
	uint64_t total, used; /* bytes */
};

struct sys_pool {
	char name[64];
	char layout[24]; /* "raidz2", "mirror", or "" */
	char health[24]; /* "ONLINE", "DEGRADED", … */
	uint64_t size, alloc, free; /* bytes */
};

/* "FreeBSD 15.0-RELEASE amd64", "Debian GNU/Linux 13 (trixie) x86_64". */
void sys_os(char *out, size_t len);
/* The host name up to its first dot. */
void sys_host(char *out, size_t len);
/* The login name. */
void sys_user(char *out, size_t len);
/* Seconds since boot, or -1. */
long sys_uptime(void);
bool sys_mem(struct sys_mem *m);
/* The 1-minute load average, or -1. */
double sys_load(void);

/* `zpool list -H -p -v -o name,size,alloc,free,health` output: the first
 * pool (and its first vdev's layout). False if there is none. */
bool sys_parse_zpool(const char *out, struct sys_pool *p);
/* /proc/meminfo text. */
bool sys_parse_meminfo(const char *text, struct sys_mem *m);
/* PRETTY_NAME from os-release text, unquoted. */
bool sys_parse_os_release(const char *text, char *out, size_t len);

/* "12d 4h 31m", "4h 31m", "31m", "0m". */
void sys_fmt_uptime(long seconds, char *out, size_t len);
/* `max` cells: "12d 4h" (the card's short form). */
void sys_fmt_uptime_short(long seconds, char *out, size_t len);
/* "4.1T", "9.8G", "512M" (powers of 1024, one decimal below 10). */
void sys_fmt_bytes(uint64_t bytes, char *out, size_t len);

/* The command that lists pools (run by the caller, in the background). */
#define SYS_ZPOOL_CMD "command -v zpool >/dev/null 2>&1 && zpool list -H -p -v -o name,size,alloc,free,health 2>/dev/null"

#endif
