NAME=cutmp3
VERSION=2.0
PREFIX=/usr
BINDIR=${PREFIX}/bin
DATADIR=${PREFIX}/share
MANDIR=${PREFIX}/share/man/man1
DOCDIR=${DATADIR}/doc/${NAME}-${VERSION}

all:
	@echo -e "\n\n*** You need readline-devel, ncurses-devel or similar to compile ${NAME} ***\n*** Maybe you want to try the binary on compile failures? ***\n\n"
	gcc -O ${CFLAGS} -c mpglib.c
	gcc -O ${CFLAGS} -c main.c
	gcc main.o mpglib.o -o ${NAME} -lm -lreadline -lncurses

i386:
	gcc -Wall -O -c mpglib.c
	gcc -Wall -O -c main.c
	gcc main.o mpglib.o -o ${NAME} -lm -lreadline -lncurses

clean:
	rm -f *.o
	rm -f ${NAME}

install:
	install -d ${BINDIR}
	install ${NAME} ${BINDIR}
	if [ ! -z "${KDEDIR}" ]; then install -m 644 ${NAME}.desktop ${KDEDIR}/share/apps/konqueror/servicemenus; fi
	if [ -d /usr/share/apps/konqueror/servicemenus ]; then install -m 644 ${NAME}.desktop /usr/share/apps/konqueror/servicemenus; fi
	if [ -d /opt/kde/share/apps/konqueror/servicemenus ]; then install -m 644 ${NAME}.desktop /opt/kde/share/apps/konqueror/servicemenus; fi
	if [ -d /opt/kde3/share/apps/konqueror/servicemenus ]; then install -m 644 ${NAME}.desktop /opt/kde3/share/apps/konqueror/servicemenus; fi
	install -d ${DOCDIR}/${NAME}
	install -m 644 README* USAGE ${DOCDIR}/${NAME}
	install -d ${MANDIR}
	gzip ${NAME}.1
	install -m 644 ${NAME}.1.gz ${MANDIR}
	gunzip ${NAME}.1.gz

uninstall:
	rm -f ${BINDIR}/${NAME}
	rm -f ${KDEDIR}/share/apps/konqueror/servicemenus/${NAME}.desktop

debug:
	gcc -g -Wall -O -c mpglib.c
	gcc -g -Wall -O -c main.c
	gcc main.o mpglib.o -o ${NAME} -lm -lreadline -lncurses
