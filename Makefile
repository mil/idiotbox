CFLAGS += -Wall

build: clean
	cc -c xml.c ${CFLAGS}
	cc -c youtube.c ${CFLAGS}
	# UIs
	# HTML
	cc -c cgi.c ${CFLAGS}
	# CLI
	cc -c cli.c ${CFLAGS}
	# CLI
	cc -c gph.c ${CFLAGS}
	# Link HTML CGI (static)
	cc -o cgi xml.o youtube.o cgi.o \
		${LDFLAGS} \
		-ltls -lssl -lcrypto -static
	# Link gph UI (static)
	cc -o gph xml.o youtube.o gph.o \
		${LDFLAGS} \
		-ltls -lssl -lcrypto -static
	# Link CLI UI
	cc -o cli xml.o youtube.o cli.o \
		${LDFLAGS} \
		-ltls

clean:
	rm -f cgi cli gph *.o
