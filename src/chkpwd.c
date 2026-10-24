/*
 * twm-chkpwd (GRT-12, D-13): checks the password of the user who runs it,
 * for twm's lock on systems where PAM can't check a password for an
 * unprivileged process (FreeBSD's OpenPAM: pam_unix reads master.passwd
 * itself). The counterpart of Linux-PAM's unix_chkpwd. Installed setuid
 * root, so it does as little as it can:
 *
 *   - the account is the real uid's; there is no way to name another one;
 *   - the PAM service is fixed (twm-password);
 *   - one password line on stdin, never from a terminal;
 *   - the answer is the exit status: 0 right, 1 wrong, 2 can't check;
 *   - the environment is cleared, inherited descriptors closed, the
 *     password wiped, and a wrong one costs two seconds.
 */
#ifdef __FreeBSD__
#undef _POSIX_C_SOURCE
#else
#define _DEFAULT_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <security/pam_appl.h>

#define SERVICE "twm-password"
#define MAX_PW 512

static char password[MAX_PW + 1];

static void wipe(void *p, size_t n) {
	volatile unsigned char *v = p;
	while (n--) {
		*v++ = 0;
	}
}

static int conv(int num, const struct pam_message **msg, struct pam_response **resp, void *data) {
	(void)data;
	struct pam_response *r = calloc((size_t)num, sizeof(*r));
	if (!r) {
		return PAM_BUF_ERR;
	}
	for (int i = 0; i < num; i++) {
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
			r[i].resp = strdup(password); /* PAM frees and (pam_unix) wipes it */
			if (!r[i].resp) {
				free(r);
				return PAM_BUF_ERR;
			}
		} else if (msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
			/* nothing visible to answer: this helper only knows one secret */
			free(r);
			return PAM_CONV_ERR;
		}
	}
	*resp = r;
	return PAM_SUCCESS;
}

int main(void) {
	static char *clean_env[] = {"PATH=/bin:/usr/bin:/sbin:/usr/sbin", NULL};
	extern char **environ;
	environ = clean_env;
	signal(SIGPIPE, SIG_IGN);
	long max = sysconf(_SC_OPEN_MAX);
	for (int fd = 3; fd < (max > 0 && max < 65536 ? max : 4096); fd++) {
		close(fd);
	}
	if (geteuid() != 0) {
		fprintf(stderr, "twm-chkpwd: not setuid root: can't check\n");
		return 2;
	}
	if (isatty(STDIN_FILENO)) {
		fprintf(stderr, "twm-chkpwd: for twm's lock, not for a terminal\n");
		return 2;
	}
	struct passwd *pw = getpwuid(getuid());
	if (!pw || !pw->pw_name || !pw->pw_name[0]) {
		return 2;
	}
	char user[256];
	snprintf(user, sizeof(user), "%s", pw->pw_name);

	size_t n = 0;
	while (n < MAX_PW) {
		ssize_t r = read(STDIN_FILENO, password + n, 1);
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r <= 0 || password[n] == '\n') {
			break;
		}
		n++;
	}
	password[n] = '\0';

	struct pam_conv c = {conv, NULL};
	pam_handle_t *h = NULL;
	int rc = pam_start(SERVICE, user, &c, &h);
	if (rc == PAM_SUCCESS) {
		rc = pam_authenticate(h, PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
	}
	if (rc == PAM_SUCCESS) {
		rc = pam_acct_mgmt(h, PAM_SILENT);
	}
	if (h) {
		pam_end(h, rc);
	}
	wipe(password, sizeof(password));
	if (rc != PAM_SUCCESS) {
		sleep(2); /* a wrong guess costs time */
		return 1;
	}
	return 0;
}
