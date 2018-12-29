struct video {
	enum LinkType { Unknown = 0, Channel, Movie, Playlist, Video } linktype;
	char id[32];
	char title[1024];
	char channeltitle[1024];
	char channelid[256];
	char userid[256];
	char publishedat[32];
	char viewcount[32];
	char duration[32];
	char channelvideos[32]; /* for channel */
};

struct video *
youtube_search(int *nretvideos,
               const char *rawsearch, const char *chan, const char *user,
               const char *page, const char *order);
