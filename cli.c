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

/* TODO: escape control-characters etc */
#define OUT(s) (fputs((s), stdout))

struct video *videos;
static int nvideos;

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
		/* TODO: better printing of other types */
		switch (videos[i].linktype) {
		case Channel:
			OUT("[Channel] ");
			OUT(videos[i].channeltitle);
			break;
		case Movie:
			OUT("[Movie] ");
			OUT(videos[i].title);
			break;
		case Playlist:
			OUT("[Playlist] ");
			OUT(videos[i].title);
			break;
		default:
			OUT(videos[i].title);
			break;
		}
		OUT("\n");

		if (videos[i].id[0]) {
			OUT("URL:           https://www.youtube.com/embed/");
                        OUT(videos[i].id);
			OUT("\n");
		}

		if (videos[i].channelid[0] || videos[i].userid[0]) {
			OUT("Atom feed:     https://www.youtube.com/feeds/videos.xml?");
			if (videos[i].channelid[0]) {
				OUT("channel_id=");
				OUT(videos[i].channelid);
			} else if (videos[i].userid[0]) {
				OUT("user=");
				OUT(videos[i].userid);
			}
			OUT("\n");
		}

		if (videos[i].channelid[0] || videos[i].userid[0]) {
			OUT("Channel title: ");
			OUT(videos[i].channeltitle);
			OUT("\n");
			if (videos[i].channelid[0]) {
				OUT("Channelid:     ");
				OUT(videos[i].channelid);
				OUT("\n");
			} else if (videos[i].userid[0]) {
				OUT("Userid:        ");
				OUT(videos[i].userid);
				OUT("\n");
			}
		}
		if (videos[i].publishedat[0]) {
			OUT("Published:     ");
			OUT(videos[i].publishedat);
			OUT("\n");
		}
		if (videos[i].viewcount[0]) {
			OUT("Viewcount:     ");
			OUT(videos[i].viewcount);
			OUT("\n");
		}
		if (videos[i].duration[0]) {
			OUT("Duration:      " );
			OUT(videos[i].duration);
			OUT("\n");
		}
		OUT("===\n");
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

	videos = youtube_search(&nvideos, argv[1], "", "", "", "relevance");
	if (!videos || nvideos <= 0) {
		OUT("No videos found\n");
		exit(1);
	}

	render();

	return 0;
}
