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

extern char **environ;

struct video *videos;
static int curpage = 1, nvideos;

/* CGI parameters */
static char rawsearch[4096], search[4096], mode[16], order[16], page[64];
static char chan[1024], user[1024];

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

/* Escape characters below as HTML 2.0 / XML 1.0. */
void
xmlencode(const char *s)
{
	for (; *s; s++) {
		switch(*s) {
		case '<':  OUT("&lt;");   break;
		case '>':  OUT("&gt;");   break;
		case '\'': OUT("&#39;");  break;
		case '&':  OUT("&amp;");  break;
		case '"':  OUT("&quot;"); break;
		default:   putchar(*s);
		}
	}
}

void
parsecgi(void)
{
	char *query, *p;
	size_t len;

	if (!(query = getenv("QUERY_STRING")))
		query = "";

	/* channel: search in channel */
	if ((p = getparam(query, "chan"))) {
		if (decodeparam(chan, sizeof(chan), p) == -1)
			chan[0] = '\0';
	}
	/* user: search in user */
	if ((p = getparam(query, "user"))) {
		if (decodeparam(user, sizeof(user), p) == -1)
			user[0] = '\0';
	}
	if (!strcmp(chan, "Search all") || !strcmp(user, "Search all")) {
		chan[0] = '\0';
		user[0] = '\0';
	}

	/* order */
	if ((p = getparam(query, "o"))) {
		if (decodeparam(order, sizeof(order), p) == -1 ||
			(strcmp(order, "date") &&
			strcmp(order, "relevance") &&
			strcmp(order, "views")))
			order[0] = '\0';
	}
	if (!order[0])
		snprintf(order, sizeof(order), chan[0] || user[0] ? "date" : "relevance");

	/* page */
	if ((p = getparam(query, "page"))) {
		if (decodeparam(page, sizeof(page), p) == -1)
			page[0] = '\0';
		/* check if it's a number > 0 and < 100 */
		errno = 0;
		curpage = strtol(page, NULL, 10);
		if (errno || curpage < 0 || curpage > 100) {
			curpage = 1;
			page[0] = '\0';
		}
	}

	/* mode */
	if ((p = getparam(query, "m"))) {
		if (decodeparam(mode, sizeof(mode), p) != -1) {
			/* fixup first character (label) for matching */
			if (mode[0])
				mode[0] = tolower((unsigned char)mode[0]);
			/* allowed themes */
			if (strcmp(mode, "light") &&
			    strcmp(mode, "dark") &&
			    strcmp(mode, "pink") &&
			    strcmp(mode, "templeos"))
				mode[0] = '\0';
		}
	}
	if (!mode[0])
		snprintf(mode, sizeof(mode), "light");

	/* search */
	if ((p = getparam(query, "q"))) {
		if ((len = strcspn(p, "&")) && len + 1 < sizeof(rawsearch)) {
			memcpy(rawsearch, p, len);
			rawsearch[len] = '\0';
		}

		if (decodeparam(search, sizeof(search), p) == -1) {
			OUT("Status: 401 Bad Request\r\n\r\n");
			exit(1);
		}
	}
}

int
render(void)
{
	char tmp[64];
	int i;

	if (pledge("stdio", NULL) == -1) {
		OUT("Status: 500 Internal Server Error\r\n\r\n");
		exit(1);
	}

	OUT(
		"Content-Type: text/html; charset=utf-8\r\n\r\n"
		"<!DOCTYPE html>\n<html>\n<head>\n"
		"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\" />\n"
		"<title>Search: \"");
		xmlencode(search);
		OUT("\"");
		if (nvideos && (chan[0] || user[0])) {
			if (videos[0].channelid[0])
				printf(" in %s", videos[0].channeltitle);
			else if (videos[0].userid[0])
				printf(" in %s", videos[0].userid);
		}
		printf(" sorted by %s</title>\n", order);
	OUT(
		"<link rel=\"stylesheet\" href=\"css/");
	xmlencode(mode);
	OUT(
		".css\" type=\"text/css\" media=\"screen\" />\n"
		"<link rel=\"icon\" type=\"image/png\" href=\"/favicon.png\" />\n"
		"<meta content=\"width=device-width\" name=\"viewport\" />\n"
		"</head>\n"
	        "<body class=\"search\">\n"
		"<form method=\"get\" action=\"\">\n");

	OUT("<input type=\"hidden\" name=\"m\" value=\"");
	xmlencode(mode);
	OUT("\" />\n");
	if (chan[0]) {
		OUT("<input type=\"hidden\" name=\"chan\" value=\"");
		xmlencode(chan);
		OUT("\" />\n");
	}

	OUT(
		"<table class=\"search\" width=\"100%\" border=\"0\" cellpadding=\"0\" cellspacing=\"0\">\n"
		"<tr>\n"
		"	<td width=\"100%\" class=\"input\">\n"
		"		<input type=\"search\" name=\"q\" value=\"");
	xmlencode(search);
	OUT(
		"\" placeholder=\"Search...\" size=\"72\" autofocus=\"autofocus\" class=\"search\" accesskey=\"f\" />\n"
		"	</td>\n"
		"	<td nowrap class=\"nowrap\">\n"
		"		<input type=\"submit\" value=\"Search\" class=\"button\"/>\n");

	if (chan[0])
		OUT("		<input type=\"submit\" name=\"chan\" value=\"Search all\" title=\"Search globally and not in the selected channel\" accesskey=\"c\" />\n");

	OUT(
		"		<select name=\"o\" title=\"Order by\" accesskey=\"o\">\n");
	printf("			<option value=\"date\"%s>Creation date</option>\n", !strcmp(order, "date") ? " selected=\"selected\"" : "");
	printf("			<option value=\"relevance\"%s>Relevance</option>\n", !strcmp(order, "relevance") ? " selected=\"selected\"" : "");
	printf("			<option value=\"views\"%s>Views</option>\n", !strcmp(order, "views") ? " selected=\"selected\"" : "");
	OUT(
		"		</select>\n"
		"		<label for=\"m\">Style: </label>\n");

	if (!strcmp(mode, "light"))
		OUT("\t\t<input type=\"submit\" name=\"m\" value=\"Dark\" title=\"Dark mode\" id=\"m\" accesskey=\"s\"/>\n");
	else
		OUT("\t\t<input type=\"submit\" name=\"m\" value=\"Light\" title=\"Light mode\" id=\"m\" accesskey=\"s\"/>\n");

	OUT(
		"	</td>\n"
		"</tr>\n"
		"</table>\n"
		"</form>\n");

	if (nvideos) {
		OUT(
			"<hr/>\n"
			"<table class=\"videos\" width=\"100%\" border=\"0\" cellpadding=\"0\" cellspacing=\"0\">\n"
			"<tbody>\n");

		for (i = 0; i < nvideos; i++) {
			OUT(
				"<tr class=\"v\">\n"
				"	<td class=\"thumb\" width=\"120\" align=\"center\">\n"
				"		<a href=\"https://www.youtube.com/embed/");
			xmlencode(videos[i].id);
			/* TODO: for channel show channel picture in some way? */
			OUT("\"><img src=\"https://i.ytimg.com/vi/");
			xmlencode(videos[i].id);
			OUT(
				"/default.jpg\" alt=\"\" height=\"90\" border=\"0\" /></a>\n"
				"	</td>\n"
				"	<td>\n"
				"		<span class=\"title\"><a href=\"https://www.youtube.com/embed/");
			xmlencode(videos[i].id);
			printf("\" accesskey=\"%d\">", i);

			/* TODO: better printing of other types */
			switch (videos[i].linktype) {
			case Channel:
				OUT("[Channel] ");
				xmlencode(videos[i].channeltitle);
				break;
			case Movie:
				OUT("[Movie] ");
				xmlencode(videos[i].title);
				break;
			case Playlist:
				OUT("[Playlist] ");
				xmlencode(videos[i].title);
				break;
			default:
				xmlencode(videos[i].title);
				break;
			}

			OUT(
				"</a></span><br/>\n"
				"\t\t<span class=\"channel\">");

			OUT("<a title=\"Search in ");
			xmlencode(videos[i].channeltitle);
			OUT("\" href=\"?");
			if (videos[i].channelid[0]) {
				OUT("chan=");
				xmlencode(videos[i].channelid);
			} else if (videos[i].userid[0]) {
				OUT("user=");
				xmlencode(videos[i].userid);
			}
			OUT("&amp;m=");
			xmlencode(mode);
			OUT("\">");
			xmlencode(videos[i].channeltitle);
			OUT("</a>");
			if (videos[i].channelid[0] || videos[i].userid[0]) {
				OUT(" | <a title=\"");
				xmlencode(videos[i].channeltitle);
				OUT(" Atom feed\" href=\"https://www.youtube.com/feeds/videos.xml?");
				if (videos[i].channelid[0]) {
					OUT("channel_id=");
					xmlencode(videos[i].channelid);
				} else if (videos[i].userid[0]) {
					OUT("user=");
					xmlencode(videos[i].userid);
				}
				OUT("\">Atom feed</a>");
			}
			OUT("</span><br/>\n");
			if (videos[i].publishedat[0]) {
				OUT("		<span class=\"publishedat\">Published: ");
				OUT(videos[i].publishedat);
			}
			OUT(
				"</span><br/>\n"
				"		<span class=\"stats\">");
			OUT(videos[i].viewcount);
			OUT(
				"</span><br/>\n"
				"	</td>\n"
				"	<td align=\"right\" class=\"a-r\">\n"
				"		<span class=\"duration\">");
			OUT(videos[i].duration);
			OUT(
				"</span>\n"
				"	</td>\n"
				"</tr>\n"
				"<tr class=\"hr\">\n"
				"	<td colspan=\"3\"><hr/></td>\n"
				"</tr>\n");
		}
		OUT("</tbody>\n");

		OUT(
			"<tfoot>\n"
			"<tr>\n"
			"\t<td align=\"left\" class=\"nowrap\" nowrap>\n");
		if (curpage > 1) {
			OUT("\t\t<a href=\"?q=");
			xmlencode(search);
			OUT("&amp;page=");
			snprintf(tmp, sizeof(tmp), "%d", curpage - 1);
			xmlencode(tmp);
			OUT("&amp;m=");
			xmlencode(mode);
			OUT("&amp;o=");
			xmlencode(order);
			if (chan[0]) {
				OUT("&amp;chan=");
				xmlencode(chan);
			}
			OUT("\" rel=\"prev\" accesskey=\"p\">&larr; prev</a>\n");
		}
		OUT(
			"\t</td>\n\t<td></td>\n"
			"\t<td align=\"right\" class=\"a-r nowrap\" nowrap>\n");

		OUT("\t\t<a href=\"?q=");
		xmlencode(search);
		OUT("&amp;page=");
		snprintf(tmp, sizeof(tmp), "%d", curpage + 1);
		xmlencode(tmp);
		OUT("&amp;m=");
		xmlencode(mode);
		OUT("&amp;o=");
		xmlencode(order);
		if (chan[0]) {
			OUT("&amp;chan=");
			xmlencode(chan);
		}
		OUT("\" rel=\"next\" accesskey=\"n\">next &rarr;</a>\n");

		OUT(
			"\t</td>\n"
			"</tr>\n"
			"</tfoot>\n");

		OUT("</table>\n");
	}

	OUT("</body>\n</html>\n");

	return 0;
}

int
main(void)
{
	if (pledge("stdio dns inet rpath unveil", NULL) == -1) {
		OUT("Status: 500 Internal Server Error\r\n\r\n");
		exit(1);
	}
	if (unveil(TLS_CA_CERT_FILE, "r") == -1) {
		OUT("Status: 500 Internal Server Error\r\n\r\n");
		exit(1);
	}
	if (unveil(NULL, NULL) == -1) {
		OUT("Status: 500 Internal Server Error\r\n\r\n");
		exit(1);
	}

	parsecgi();

	if (!rawsearch[0] && !chan[0] && !user[0])
		goto show;

	videos = youtube_search(&nvideos, rawsearch, chan, user, page, order);
	if (!videos || nvideos <= 0) {
		OUT("Status: 500 Internal Server Error\r\n\r\n");
		exit(1);
	}

show:
	render();

	return 0;
}
