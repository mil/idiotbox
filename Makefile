.POSIX:

build: clean
	${CC} -c xml.c ${CFLAGS}
	${CC} -c youtube.c ${CFLAGS}
	# UIs
	# HTML
	${CC} -c cgi.c ${CFLAGS}
	# CLI
	${CC} -c cli.c ${CFLAGS}
	# CLI
	${CC} -c gph.c ${CFLAGS}
	# Link HTML CGI (static)
	${CC} -o cgi xml.o youtube.o cgi.o \
		${LDFLAGS} \
		-ltls -lssl -lcrypto -static
	# Link gph UI (static)
	${CC} -o gph xml.o youtube.o gph.o \
		${LDFLAGS} \
		-ltls -lssl -lcrypto -static
	# Link CLI UI
	${CC} -o cli xml.o youtube.o cli.o \
		${LDFLAGS} \
		-ltls

clean:
	rm -f cgi cli gph *.o
