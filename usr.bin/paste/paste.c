/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam S. Moskowitz of Menlo Consulting.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)paste.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *delim;
int delimcnt;

void parallel(char **);
void sequential(char **);
int tr(char *);
static void usage(void);

char tab[] = "\t";

int
main(int argc, char *argv[])
{
	int ch, seq;

	seq = 0;
	while ((ch = getopt(argc, argv, "d:s")) != -1)
		switch(ch) {
		case 'd':
			delimcnt = tr(delim = optarg);
			break;
		case 's':
			seq = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (*argv == NULL)
		usage();
	if (!delim) {
		delimcnt = 1;
		delim = tab;
	}

	if (seq)
		sequential(argv);
	else
		parallel(argv);
	exit(0);
}

typedef struct _list {
	struct _list *next;
	FILE *fp;
	int cnt;
	char *name;
} LIST;

void
parallel(char **argv)
{
	LIST *lp;
	int cnt;
	char ch, *p;
	LIST *head, *tmp;
	int opencnt, output;
	char buf[_POSIX2_LINE_MAX + 1];

	for (cnt = 0, head = NULL; (p = *argv); ++argv, ++cnt) {
		if (!(lp = (LIST *)malloc((u_int)sizeof(LIST))))
			errx(1, "%s", strerror(ENOMEM));
		if (p[0] == '-' && !p[1])
			lp->fp = stdin;
		else if (!(lp->fp = fopen(p, "r")))
			err(1, "%s", p);
		lp->next = NULL;
		lp->cnt = cnt;
		lp->name = p;
		if (!head)
			head = tmp = lp;
		else {
			tmp->next = lp;
			tmp = lp;
		}
	}

	for (opencnt = cnt; opencnt;) {
		for (output = 0, lp = head; lp; lp = lp->next) {
			if (!lp->fp) {
				if (output && lp->cnt &&
				    (ch = delim[(lp->cnt - 1) % delimcnt]))
					putchar(ch);
				continue;
			}
			if (!fgets(buf, sizeof(buf), lp->fp)) {
				if (!--opencnt)
					break;
				lp->fp = NULL;
				if (output && lp->cnt &&
				    (ch = delim[(lp->cnt - 1) % delimcnt]))
					putchar(ch);
				continue;
			}
			if (!(p = index(buf, '\n')))
				errx(1, "%s: input line too long", lp->name);
			*p = '\0';
			/*
			 * make sure that we don't print any delimiters
			 * unless there's a non-empty file.
			 */
			if (!output) {
				output = 1;
				for (cnt = 0; cnt < lp->cnt; ++cnt)
					if ((ch = delim[cnt % delimcnt]))
						putchar(ch);
			} else if ((ch = delim[(lp->cnt - 1) % delimcnt]))
				putchar(ch);
			(void)printf("%s", buf);
		}
		if (output)
			putchar('\n');
	}
}

void
sequential(char **argv)
{
	FILE *fp;
	int cnt;
	char ch, *p, *dp;
	char buf[_POSIX2_LINE_MAX + 1];

	for (; (p = *argv); ++argv) {
		if (p[0] == '-' && !p[1])
			fp = stdin;
		else if (!(fp = fopen(p, "r"))) {
			warn("%s", p);
			continue;
		}
		if (fgets(buf, sizeof(buf), fp)) {
			for (cnt = 0, dp = delim;;) {
				if (!(p = index(buf, '\n')))
					errx(1, "%s: input line too long", *argv);
				*p = '\0';
				(void)printf("%s", buf);
				if (!fgets(buf, sizeof(buf), fp))
					break;
				if ((ch = *dp++))
					putchar(ch);
				if (++cnt == delimcnt) {
					dp = delim;
					cnt = 0;
				}
			}
			putchar('\n');
		}
		if (fp != stdin)
			(void)fclose(fp);
	}
}

int
tr(char *arg)
{
	int cnt;
	char ch, *p;

	for (p = arg, cnt = 0; (ch = *p++); ++arg, ++cnt)
		if (ch == '\\')
			switch(ch = *p++) {
			case 'n':
				*arg = '\n';
				break;
			case 't':
				*arg = '\t';
				break;
			case '0':
				*arg = '\0';
				break;
			default:
				*arg = ch;
				break;
		} else
			*arg = ch;

	if (!cnt)
		errx(1, "no delimiters specified");
	return(cnt);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: paste [-s] [-d delimiters] file ...\n");
	exit(1);
}
