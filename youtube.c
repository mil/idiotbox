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

#include <tls.h>

#include "youtube.h"
#include "xml.h"

#undef strlcat
size_t strlcat(char *, const char *, size_t);
#undef strlcpy
size_t strlcpy(char *, const char *, size_t);

#define READ_BUF_SIZ        16384  /* read buffer in bytes */
#define MAX_RESPONSETIMEOUT 10     /* timeout in seconds */
#define MAX_RESPONSESIZ     500000 /* max download size in bytes */

#define STRP(s) s,sizeof(s)-1

/* temporary variables to copy for states */
static char id[256], userid[256];

/* states */
static int metainfocount;
static enum ItemState {
	None  = 0,
	Item  = 1, Pager = 2,
	Metainfo = 4, Title = 8, User = 16, Videotime = 32,
} state;

static struct item *videos;
static size_t nvideos;

/* data buffers, size and offset used for parsing XML, see getnext() */
static const char *xml_data_buf;
static size_t xml_data_size;
static size_t xml_data_off;

void
setxmldata(const char *s, size_t len)
{
	xml_data_off = 0;
	xml_data_size = len;
	xml_data_buf = s;
}

int
getnext(void)
{
	if (xml_data_off >= xml_data_size)
		return EOF;
	return xml_data_buf[xml_data_off++];
}

/* ? TODO: don't die in youtube.c ? */
static void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	exit(1);
}

static int
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
static int
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

static char *
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

static int
isclassmatch(const char *classes, const char *clss, size_t len)
{
	const char *p;

	if (!(p = strstr(classes, clss)))
		return 0;
	return (p == classes || isspace((unsigned char)p[-1])) &&
	        (isspace((unsigned char)p[len]) || !p[len]);
}

/* XML/HTML entity conversion */
static const char *
entitytostr(const char *s)
{
	static char buf[16];
	ssize_t len;

	if ((len = xml_entitytostr(s, buf, sizeof(buf))) > 0)
		return buf;

	return s;
}

void
xmlattr(XMLParser *x, const char *t, size_t tl, const char *a, size_t al,
        const char *v, size_t vl)
{
	/* grouped channel index, used for channelid and channel title */
	static int grouped = -1;

	if (!strcmp(t, "div") && !strcmp(a, "class") && isclassmatch(v, STRP("search-pager"))) {
		/* last video */
		if (videos[nvideos].linktype && nvideos < MAX_VIDEOS) {
			if (grouped != -1 && !videos[nvideos].channelid[0]) {
				strlcpy(videos[nvideos].channelid, videos[grouped].channelid, sizeof(videos[nvideos].channelid));
				strlcpy(videos[nvideos].channeltitle, videos[grouped].channeltitle, sizeof(videos[nvideos].channeltitle));
			}
			nvideos++;
		}
		state &= ~Item;
		state |= Pager;
	}

	if (nvideos >= MAX_VIDEOS)
		return;

	if (!strcmp(t, "div") && !strcmp(a, "class") &&
		isclassmatch(v, STRP("yt-lockup"))) {
		state |= Item;
		if (videos[nvideos].linktype) {
			if (videos[nvideos].channelid[0] || videos[nvideos].userid[0] ||
			    videos[nvideos].linktype != Video)
				grouped = -1;
			if (videos[nvideos].linktype == Channel)
				grouped = nvideos;
			if (grouped != -1 && !videos[nvideos].channelid[0]) {
				strlcpy(videos[nvideos].channelid, videos[grouped].channelid, sizeof(videos[nvideos].channelid));
				strlcpy(videos[nvideos].channeltitle, videos[grouped].channeltitle, sizeof(videos[nvideos].channeltitle));
			}
			nvideos++;
		}
		if (strstr(v, " yt-lockup-channel "))
			videos[nvideos].linktype = Channel;
		else if (strstr(v, "yt-lockup-movie-"))
			videos[nvideos].linktype = Movie;
		else if (strstr(v, " yt-lockup-playlist "))
			videos[nvideos].linktype = Playlist;
		if (strstr(v, " yt-lockup-video "))
			videos[nvideos].linktype = Video;
	}
	if (!(state & Item))
		return;

	if (!strcmp(t, "span") && !strcmp(a, "class") && isclassmatch(v, STRP("video-time")))
		state |= Videotime;
	if (!strcmp(t, "ul") && !strcmp(a, "class") && isclassmatch(v, STRP("yt-lockup-meta-info"))) {
		state |= Metainfo;
		metainfocount = 0;
	}
	if (!strcmp(t, "h3") && !strcmp(a, "class") && isclassmatch(v, STRP("yt-lockup-title")))
		state |= Title;
	if (!strcmp(t, "div") && !strcmp(a, "class") && isclassmatch(v, STRP("yt-lockup-byline")))
		state |= User;

	if ((state & Title) && !strcmp(t, "a") && !strcmp(a, "title")) {
		if (videos[nvideos].linktype == Channel)
			strlcat(videos[nvideos].channeltitle, v, sizeof(videos[nvideos].channeltitle));
		else
			strlcat(videos[nvideos].title, v, sizeof(videos[nvideos].title));
	}

	if ((state & Title) && !strcmp(t, "a") && !strcmp(a, "href"))
		strlcat(id, v, sizeof(id));

	if (!strcmp(t, "button") && !strcmp(a, "data-channel-external-id"))
		strlcat(videos[nvideos].channelid, v, sizeof(videos[nvideos].channelid));

	if ((state & User) && !strcmp(t, "a") && !strcmp(a, "href"))
		strlcat(userid, v, sizeof(userid));
}

void
xmlattrentity(XMLParser *x, const char *t, size_t tl, const char *a, size_t al,
              const char *v, size_t vl)
{
	const char *s;

	if (!(state & Pager) && nvideos >= MAX_VIDEOS)
		return;

	s = entitytostr(v);
	xmlattr(x, t, tl, a, al, s, strlen(s));
}

void
xmldata(XMLParser *x, const char *d, size_t dl)
{
	if ((state & Pager))
		return;

	/* optimization: no need to process and must not process videos after this */
	if (!state || nvideos >= MAX_VIDEOS)
		return;

	/* use parsed link type for meta info since this metainfo differs per type like:
	   channel, playlist, video */
	if ((state & Metainfo)) {
		switch (videos[nvideos].linktype) {
		case Playlist:
			break; /* ignore */
		case Channel:
			if (metainfocount == 1)
				strlcat(videos[nvideos].channelvideos, d, sizeof(videos[nvideos].channelvideos));
			break;
		default:
			if (metainfocount == 1)
				strlcat(videos[nvideos].publishedat, d, sizeof(videos[nvideos].publishedat));
			else if (metainfocount == 2)
				strlcat(videos[nvideos].viewcount, d, sizeof(videos[nvideos].viewcount));
		}
	}
	if ((state & Videotime) && !strcmp(x->tag, "span"))
		strlcat(videos[nvideos].duration, d, sizeof(videos[nvideos].duration));
	if ((state & User) && !strcmp(x->tag, "a"))
		strlcat(videos[nvideos].channeltitle, d, sizeof(videos[nvideos].channeltitle));
}

void
xmldataentity(XMLParser *x, const char *d, size_t dl)
{
	const char *s;

	/* optimization: no need for entity conversion */
	if (!state || nvideos >= MAX_VIDEOS)
		return;

	s = entitytostr(d);
	xmldata(x, s, strlen(s));
}

void
xmltagend(XMLParser *x, const char *t, size_t tl, int isshort)
{
	char *p;

	if ((state & Metainfo) && !strcmp(t, "ul"))
		state &= ~Metainfo;
	if ((state & Title) && !strcmp(t, "h3")) {
		state &= ~Title;

		if (nvideos >= MAX_VIDEOS)
			return;

		if (!strncmp(id, "/watch", sizeof("/watch") - 1)) {
			if (!videos[nvideos].linktype)
				videos[nvideos].linktype = Video;
			if ((p = getparam(id, "v"))) {
				if (decodeparam(videos[nvideos].id, sizeof(videos[nvideos].id), p) == -1)
					videos[nvideos].id[0] = '\0';
			}
		}

		id[0] = '\0';
	}
	if ((state & User)) {
		state &= ~User;

		if (nvideos >= MAX_VIDEOS)
			return;

		/* can be user or channel */
		if (!strncmp(userid, "/channel/", sizeof("/channel/") - 1)) {
			strlcpy(videos[nvideos].channelid,
				userid + sizeof("/channel/") - 1,
				sizeof(videos[nvideos].channelid));
		} else if (!strncmp(userid, "/user/", sizeof("/user/") - 1)) {
			strlcpy(videos[nvideos].userid,
				userid + sizeof("/user/") - 1,
				sizeof(videos[nvideos].userid));
		}

		userid[0] = '\0';
	}
	if ((state & Videotime))
		state &= ~Videotime;
}

void
xmltagstart(XMLParser *x, const char *t, size_t tl)
{
	if ((state & Metainfo) && !strcmp(t, "li"))
		metainfocount++;
}

char *
readtls(struct tls *t)
{
	char *buf;
	size_t len = 0, size = 0;
	ssize_t r;

	/* always allocate an empty buffer */
	if (!(buf = calloc(1, size + 1)))
		die("calloc: %s\n", strerror(errno));

	while (1) {
		if (len + READ_BUF_SIZ + 1 > size) {
			/* allocate size: common case is small textfiles */
			size += READ_BUF_SIZ;
			if (!(buf = realloc(buf, size + 1)))
				die("realloc: %s\n", strerror(errno));
		}
		if ((r = tls_read(t, &buf[len], READ_BUF_SIZ)) <= 0)
			break;
		len += r;
		buf[len] = '\0';
		if (len > MAX_RESPONSESIZ)
			die("response is too big: > %zu bytes\n", MAX_RESPONSESIZ);
	}
	if (r < 0)
		die("tls_read: %s\n", tls_error(t));

	return buf;
}

int
edial(const char *host, const char *port)
{
	struct addrinfo hints, *res, *res0;
	int error, save_errno, s;
	const char *cause = NULL;
	struct timeval timeout;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV; /* numeric port only */
	if ((error = getaddrinfo(host, port, &hints, &res0)))
		die("%s: %s: %s:%s\n", __func__, gai_strerror(error), host, port);
	s = -1;
	for (res = res0; res; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype,
		           res->ai_protocol);
		if (s == -1) {
			cause = "socket";
			continue;
		}

		timeout.tv_sec = MAX_RESPONSETIMEOUT;
		timeout.tv_usec = 0;
		if (setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == -1)
			die("%s: setsockopt: %s\n", __func__, strerror(errno));

		timeout.tv_sec = MAX_RESPONSETIMEOUT;
		timeout.tv_usec = 0;
		if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1)
			die("%s: setsockopt: %s\n", __func__, strerror(errno));

		if (connect(s, res->ai_addr, res->ai_addrlen) == -1) {
			cause = "connect";
			save_errno = errno;
			close(s);
			errno = save_errno;
			s = -1;
			continue;
		}
		break;
	}
	if (s == -1)
		die("%s: %s: %s:%s\n", __func__, cause, host, port);
	freeaddrinfo(res0);

	return s;
}

char *
request(const char *path)
{
	struct tls *t;
	const char *host = "www.youtube.com";
	char request[4096];
	char *data;
	ssize_t w;
	int fd;

	/* use HTTP/1.0, don't use HTTP/1.1 using ugly chunked-encoding */
	snprintf(request, sizeof(request),
		"GET %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n"
		"Connection: close\r\n"
		"\r\n", path, host);

	if (tls_init() == -1)
		die("tls_init\n");

	if (!(t = tls_client()))
		die("tls_client: %s\n", tls_error(t));

	fd = edial(host, "443");

	if (tls_connect_socket(t, fd, host) == -1)
		die("tls_connect: %s\n", tls_error(t));

	if ((w = tls_write(t, request, strlen(request))) < 0)
		die("tls_write: %s\n", tls_error(t));

	data = readtls(t);

	tls_close(t);
	tls_free(t);

	return data;
}

char *
request_search(const char *s, const char *chan, const char *user,
               const char *page, const char *order)
{
	char path[4096];

	/* when searching in channel or user but the search string is empty:
	   fake a search with a single space. */
	if ((chan[0] || user[0]) && !s[0])
		s = "+";

	if (user[0])
		snprintf(path, sizeof(path), "/user/%s/search?query=%s", user, s);
	else if (chan[0])
		snprintf(path, sizeof(path), "/channel/%s/search?query=%s", chan, s);
	else
		snprintf(path, sizeof(path), "/results?search_query=%s", s);

	if (page[0]) {
		strlcat(path, "&page=", sizeof(path));
		strlcat(path, page, sizeof(path));
	}

	if (order[0]) {
		strlcat(path, "&search_sort=", sizeof(path));
		if (!strcmp(order, "date"))
			strlcat(path, "video_date_uploaded", sizeof(path));
		else if (!strcmp(order, "relevance"))
			strlcat(path, "video_relevance", sizeof(path));
		else if (!strcmp(order, "views"))
			strlcat(path, "video_view_count", sizeof(path));
	}

	/* check if request is too long (truncation) */
	if (strlen(path) >= sizeof(path) - 1)
		return NULL;

	return request(path);
}

struct search_response *
youtube_search(const char *rawsearch, const char *chan, const char *user,
               const char *page, const char *order)
{
	struct search_response *r;
	XMLParser x = { 0 };
	char *data, *s;

	if (!(data = request_search(rawsearch, chan, user, page, order)))
		return NULL;
	if (!(s = strstr(data, "\r\n\r\n")))
		return NULL; /* invalid response */
	/* skip header */
	s += strlen("\r\n\r\n");

	if (!(r = calloc(1, sizeof(*r))))
		return NULL;

	setxmldata(s, strlen(s));

	x.xmlattr = xmlattr;
	x.xmlattrentity = xmlattrentity;
	x.xmldata = xmldata;
	x.xmldataentity = xmldataentity;
	x.xmltagend = xmltagend;
	x.xmltagstart = xmltagstart;

	x.getnext = getnext;

	nvideos = 0;
	videos = r->items;

	xml_parse(&x);

	r->nitems = nvideos;

	return r;
}
