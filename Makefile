.POSIX:

COMPATOBJ = strlcpy.o strlcat.o
COMPATSRC = strlcpy.c strlcat.c

build: clean
	# compat sources (strlcpy, strlcat).
	${CC} -c ${COMPATSRC}
	#
	${CC} -c xml.c ${CFLAGS}
	${CC} -c youtube.c ${CFLAGS}
	# UIs
	# HTML
	${CC} -c cgi.c ${CFLAGS}
	# CLI
	${CC} -c cli.c ${CFLAGS}
	# CLI
	${CC} -c gph.c ${CFLAGS}
	# Link HTML CGI (static-link for chroot)
	${CC} -o cgi xml.o youtube.o cgi.o \
		${COMPATOBJ} \
		${LDFLAGS} \
		-ltls -lssl -lcrypto -static
	# Link gph UI (static-link for chroot)
	${CC} -o gph xml.o youtube.o gph.o \
		${COMPATOBJ} \
		${LDFLAGS} \
		-ltls -lssl -lcrypto -static
	# Link CLI UI
	${CC} -o cli xml.o youtube.o cli.o \
		${COMPATOBJ} \
		${LDFLAGS} \
		-ltls

clean:
	rm -f cgi cli gph *.o
