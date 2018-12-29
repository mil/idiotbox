build: clean
	cc -c xml.c ${CFLAGS} -Wall
	cc -c youtube.c ${CFLAGS} -Wall
	# UIs
	# HTML
	cc -c main.c ${CFLAGS} -Wall
	# CLI
	cc -c cli.c ${CFLAGS} -Wall
	# Link HTML CGI (static)
	cc -o main xml.o youtube.o main.o \
		${LDFLAGS} \
		-ltls -lssl -lcrypto -static
	# Link CLI UI
	cc -o cli xml.o youtube.o cli.o \
		${LDFLAGS} \
		-ltls

clean:
	rm -f main cli *.o
