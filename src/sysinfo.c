#ifdef __FreeBSD__
#undef _POSIX_C_SOURCE /* sysctl, getloadavg */
#else
#define _DEFAULT_SOURCE /* getloadavg */
#endif
#include <ctype.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#include <sys/time.h>
#endif
#include "sysinfo.h"

#ifndef __FreeBSD__ /* Linux: /proc and os-release */
static char *read_text(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return NULL;
	}
	size_t cap = 4096, n = 0;
	char *buf = malloc(cap);
	size_t r;
	while (buf && (r = fread(buf + n, 1, cap - 1 - n, f)) > 0) {
		n += r;
		if (n == cap - 1) {
			char *nb = realloc(buf, cap * 2);
			if (!nb) {
				break;
			}
			buf = nb;
			cap *= 2;
		}
	}
	fclose(f);
	if (buf) {
		buf[n] = '\0';
	}
	return buf;
}
#endif

bool sys_parse_os_release(const char *text, char *out, size_t len) {
	for (const char *l = text; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		if (strncmp(l, "PRETTY_NAME=", 12) != 0) {
			continue;
		}
		const char *v = l + 12;
		size_t n = strcspn(v, "\n");
		if (n >= 2 && (v[0] == '"' || v[0] == '\'') && v[n - 1] == v[0]) {
			v++;
			n -= 2;
		}
		snprintf(out, len, "%.*s", (int)n, v);
		return n > 0;
	}
	return false;
}

void sys_os(char *out, size_t len) {
	struct utsname u;
	if (uname(&u) != 0) {
		snprintf(out, len, "unknown");
		return;
	}
#ifdef __linux__
	char pretty[160];
	char *text = read_text("/etc/os-release");
	if (!text) {
		text = read_text("/usr/lib/os-release");
	}
	bool have = text && sys_parse_os_release(text, pretty, sizeof(pretty));
	free(text);
	if (have) {
		snprintf(out, len, "%s %s", pretty, u.machine);
		return;
	}
#endif
	snprintf(out, len, "%s %s %s", u.sysname, u.release, u.machine);
}

void sys_host(char *out, size_t len) {
	char h[256] = "";
	if (gethostname(h, sizeof(h) - 1) != 0 || !*h) {
		snprintf(h, sizeof(h), "localhost");
	}
	h[strcspn(h, ".")] = '\0';
	snprintf(out, len, "%s", h);
}

void sys_user(char *out, size_t len) {
	struct passwd *pw = getpwuid(getuid());
	const char *name = pw ? pw->pw_name : getenv("USER");
	snprintf(out, len, "%s", name ? name : "user");
}

long sys_uptime(void) {
#ifdef __FreeBSD__
	struct timeval boot;
	size_t sz = sizeof(boot);
	int mib[2] = {CTL_KERN, KERN_BOOTTIME};
	if (sysctl(mib, 2, &boot, &sz, NULL, 0) != 0) {
		return -1;
	}
	return (long)(time(NULL) - boot.tv_sec);
#elif defined(CLOCK_BOOTTIME)
	struct timespec ts;
	if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
		return -1;
	}
	return (long)ts.tv_sec;
#else
	return -1;
#endif
}

bool sys_parse_meminfo(const char *text, struct sys_mem *m) {
	unsigned long long total = 0, avail = 0;
	bool have_total = false, have_avail = false;
	for (const char *l = text; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		if (sscanf(l, "MemTotal: %llu kB", &total) == 1) {
			have_total = true;
		} else if (sscanf(l, "MemAvailable: %llu kB", &avail) == 1) {
			have_avail = true;
		}
	}
	if (!have_total || !have_avail || avail > total) {
		return false;
	}
	m->total = (uint64_t)total * 1024;
	m->used = (uint64_t)(total - avail) * 1024;
	return true;
}

#ifdef __FreeBSD__
static bool sysctl_u64(const char *name, uint64_t *v) {
	unsigned char buf[8] = {0};
	size_t sz = sizeof(buf);
	if (sysctlbyname(name, buf, &sz, NULL, 0) != 0) {
		return false;
	}
	if (sz == 4) {
		uint32_t x;
		memcpy(&x, buf, 4);
		*v = x;
	} else {
		memcpy(v, buf, sizeof(*v));
	}
	return true;
}
#endif

bool sys_mem(struct sys_mem *m) {
#ifdef __FreeBSD__
	uint64_t phys, page, freec = 0, inact = 0, laundry = 0;
	if (!sysctl_u64("hw.physmem", &phys) || !sysctl_u64("vm.stats.vm.v_page_size", &page)) {
		return false;
	}
	sysctl_u64("vm.stats.vm.v_free_count", &freec);
	sysctl_u64("vm.stats.vm.v_inactive_count", &inact);
	sysctl_u64("vm.stats.vm.v_laundry_count", &laundry);
	uint64_t avail = (freec + inact + laundry) * page;
	m->total = phys;
	m->used = avail < phys ? phys - avail : 0;
	return true;
#else
	char *text = read_text("/proc/meminfo");
	bool ok = text && sys_parse_meminfo(text, m);
	free(text);
	return ok;
#endif
}

double sys_load(void) {
	double l[1];
	return getloadavg(l, 1) == 1 ? l[0] : -1;
}

static uint64_t field_u64(const char *s) {
	return (uint64_t)strtoull(s, NULL, 10);
}

bool sys_parse_zpool(const char *out, struct sys_pool *p) {
	memset(p, 0, sizeof(*p));
	bool have = false;
	for (const char *l = out; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		size_t n = strcspn(l, "\n");
		char line[512];
		snprintf(line, sizeof(line), "%.*s", (int)(n < sizeof(line) ? n : sizeof(line) - 1), l);
		if (!*line) {
			continue;
		}
		bool vdev = isspace((unsigned char)line[0]);
		if (!vdev && have) {
			break; /* the next pool: one is enough */
		}
		char *f[6] = {0};
		int nf = 0;
		char *s = line;
		while (*s && isspace((unsigned char)*s)) {
			s++;
		}
		for (char *tok = s; nf < 6 && tok; nf++) {
			f[nf] = tok;
			char *tab = strchr(tok, '\t');
			if (tab) {
				*tab = '\0';
				tok = tab + 1;
			} else {
				tok = NULL;
			}
		}
		if (!vdev) {
			if (nf < 5) {
				return false;
			}
			snprintf(p->name, sizeof(p->name), "%s", f[0]);
			p->size = field_u64(f[1]);
			p->alloc = field_u64(f[2]);
			p->free = field_u64(f[3]);
			snprintf(p->health, sizeof(p->health), "%s", f[4]);
			have = true;
		} else if (have && !p->layout[0] && f[0]) {
			/* "raidz2-0", "mirror-0", "draid1:…": the layout; a bare disk: none */
			const char *kinds[] = {"raidz", "mirror", "draid"};
			for (size_t k = 0; k < 3; k++) {
				if (strncmp(f[0], kinds[k], strlen(kinds[k])) == 0) {
					size_t m = strcspn(f[0], "-:");
					snprintf(p->layout, sizeof(p->layout), "%.*s", (int)m, f[0]);
					break;
				}
			}
			if (!p->layout[0]) {
				p->layout[0] = '\x01'; /* looked: no layout word */
			}
		}
	}
	if (p->layout[0] == '\x01') {
		p->layout[0] = '\0';
	}
	return have;
}

void sys_fmt_uptime(long s, char *out, size_t len) {
	if (s < 0) {
		snprintf(out, len, "unknown");
		return;
	}
	long d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60;
	if (d) {
		snprintf(out, len, "%ldd %ldh %ldm", d, h, m);
	} else if (h) {
		snprintf(out, len, "%ldh %ldm", h, m);
	} else {
		snprintf(out, len, "%ldm", m);
	}
}

void sys_fmt_uptime_short(long s, char *out, size_t len) {
	if (s < 0) {
		snprintf(out, len, "?");
		return;
	}
	long d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60;
	if (d) {
		snprintf(out, len, "%ldd %ldh", d, h);
	} else if (h) {
		snprintf(out, len, "%ldh %ldm", h, m);
	} else {
		snprintf(out, len, "%ldm", m);
	}
}

void sys_fmt_bytes(uint64_t b, char *out, size_t len) {
	static const char units[] = "BKMGTPE";
	double v = (double)b;
	int u = 0;
	while (v >= 1024 && u < 6) {
		v /= 1024;
		u++;
	}
	if (u == 0) {
		snprintf(out, len, "%lluB", (unsigned long long)b);
	} else if (v < 10) {
		snprintf(out, len, "%.1f%c", v, units[u]);
	} else {
		snprintf(out, len, "%.0f%c", v, units[u]);
	}
}
