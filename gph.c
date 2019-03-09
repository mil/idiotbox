#include <sys/socket.h>
#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "youtube.h"

#ifndef __OpenBSD__
#define pledge(p1,p2) 0
#define unveil(p1,p2) 0
#endif

#ifndef TLS_CA_CERT_FILE
#define TLS_CA_CERT_FILE "/etc/ssl/cert.pem"
#endif

#define OUT(s) (fputs((s), stdout))
#define OUTESCAPE(s) (printescape(s))

struct video *videos;
static int nvideos;

/* print: ignore control-characters, escape | */
void
printescape(const char *s)
{
	for (; *s; ++s) {
		if (*s == '|')
			fputc('\\', stdout);
		if (!iscntrl((unsigned char)*s))
			fputc(*s, stdout);
	}
}

void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	exit(1);
}

int
uriencode(const char *s, char *buf, size_t bufsiz)
{
	static char hex[] = "0123456789ABCDEF";
	char *d = buf, *e = buf + bufsiz;
	unsigned char c;

	if (!bufsiz)
		return 0;

	for (; *s; ++s) {
		c = (unsigned char)*s;
		if (d + 4 >= e)
			return 0;
		if (c == ' ' || c == '#' || c == '%' || c == '?' || c == '"' ||
		    c == '&' || c == '<' || c <= 0x1f || c >= 0x7f) {
			*d++ = '%';
			*d++ = hex[c >> 4];
			*d++ = hex[c & 0x0f];
		} else {
			*d++ = *s;
		}
	}
	*d = '\0';

	return 1;
}

int
hexdigit(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	else if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	return 0;
}

/* decode until NUL separator or end of "key". */
int
decodeparam(char *buf, size_t bufsiz, const char *s)
{
	size_t i;

	if (!bufsiz)
		return -1;

	for (i = 0; *s && *s != '&'; s++) {
		if (i + 3 >= bufsiz)
			return -1;
		switch (*s) {
		case '%':
			if (!isxdigit((unsigned char)*(s+1)) ||
			    !isxdigit((unsigned char)*(s+2)))
				return -1;
			buf[i++] = hexdigit(*(s+1)) * 16 + hexdigit(*(s+2));
			s += 2;
			break;
		case '+':
			buf[i++] = ' ';
			break;
		default:
			buf[i++] = *s;
			break;
		}
	}
	buf[i] = '\0';

	return i;
}

char *
getparam(const char *query, const char *s)
{
	const char *p, *last = NULL;
	size_t len;

	len = strlen(s);
	for (p = query; (p = strstr(p, s)); p += len) {
		if (p[len] == '=' && (p == query || p[-1] == '&' || p[-1] == '?'))
			last = p + len + 1;
	}

	return (char *)last;
}

int
render(void)
{
	int i;

	if (pledge("stdio", NULL) == -1) {
		fprintf(stderr, "pledge: %s\n", strerror(errno));
		exit(1);
	}

	for (i = 0; i < nvideos; i++) {
		if (videos[i].id[0])
			OUT("[h|");
		else
			OUT("t");

		/* TODO: better printing of other types */
		switch (videos[i].linktype) {
		case Channel:
			OUT("[Channel] ");
			OUTESCAPE(videos[i].channeltitle);
			break;
		case Movie:
			OUT("[Movie] ");
			OUTESCAPE(videos[i].title);
			break;
		case Playlist:
			OUT("[Playlist] ");
			OUTESCAPE(videos[i].title);
			break;
		default:
			OUTESCAPE(videos[i].title);
			break;
		}

		if (videos[i].id[0]) {
			OUT("|URL:https://www.youtube.com/embed/");
			OUTESCAPE(videos[i].id);
			OUT("|server|port]\n");
		} else {
			OUT("\n");
		}

		if (videos[i].channelid[0] || videos[i].userid[0]) {
			OUT("[h|Atom feed of ");
			OUTESCAPE(videos[i].channeltitle);
			OUT("|URL:https://www.youtube.com/feeds/videos.xml?");
			if (videos[i].channelid[0]) {
				OUT("channel_id=");
				OUTESCAPE(videos[i].channelid);
			} else if (videos[i].userid[0]) {
				OUT("user=");
				OUTESCAPE(videos[i].userid);
			}
			OUT("|server|port]\n");
		}
		if (videos[i].duration[0]) {
			OUT("Duration:      " );
			OUTESCAPE(videos[i].duration);
			OUT("\n");
		}
		if (videos[i].publishedat[0]) {
			OUT("Published:     ");
			OUTESCAPE(videos[i].publishedat);
			OUT("\n");
		}
		if (videos[i].viewcount[0]) {
			OUT("Views:         ");
			OUTESCAPE(videos[i].viewcount);
			OUT("\n");
		}
		OUT("\n\n");
	}

	return 0;
}

static void
usage(const char *argv0)
{
	fprintf(stderr, "usage: %s <keywords>\n", argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char search[1024];

	if (pledge("stdio dns inet rpath unveil", NULL) == -1) {
		fprintf(stderr, "pledge: %s\n", strerror(errno));
		exit(1);
	}
	if (unveil(TLS_CA_CERT_FILE, "r") == -1) {
		fprintf(stderr, "unveil: %s\n", strerror(errno));
		exit(1);
	}
	if (unveil(NULL, NULL) == -1) {
		fprintf(stderr, "unveil: %s\n", strerror(errno));
		exit(1);
	}

	if (argc < 2 || !argv[1][0])
		usage(argv[0]);
	if (!uriencode(argv[1], search, sizeof(search)))
		usage(argv[0]);

	videos = youtube_search(&nvideos, search, "", "", "", "relevance");
	if (!videos || nvideos <= 0) {
		OUT("tNo videos found\n");
		exit(1);
	}

	render();

	return 0;
}
