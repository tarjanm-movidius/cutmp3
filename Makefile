NAME=cutmp3
VERSION=3.0.3
PREFIX?=/usr/local
BINDIR=${PREFIX}/bin
DATADIR=${PREFIX}/share
MANDIR=${PREFIX}/share/man/man1
DOCDIR=${DATADIR}/doc/${NAME}-${VERSION}
CFLAGS?=-Wall -Wformat-security
#CFLAGS+=-DDEBUG

all:
	@echo -e "\n\n*** You need readline-devel, ncurses-devel or similar to compile ${NAME} ***\n*** Maybe you want to try the binary on compile failures? ***\n\n"
	gcc -O2 ${CFLAGS} -c mpglib.c
	gcc -O2 ${CFLAGS} -c main.c
	gcc main.o mpglib.o -o ${NAME} -lm -lreadline
#-lncurses

i386:
	gcc -Wall -O2 -c mpglib.c
	gcc -Wall -O2 -c main.c
	gcc main.o mpglib.o -o ${NAME} -lm -lreadline

clean:
	rm -f *.o
	rm -f ${NAME}

install:
	install -d ${BINDIR}
	install ${NAME} ${BINDIR}
	strip ${BINDIR}/${NAME}
	if [ ! -z "${KDEDIR}" ]; then install -m 644 ${NAME}.desktop ${KDEDIR}/share/apps/konqueror/servicemenus; elif [ -d /usr/share/apps/konqueror/servicemenus ]; then install -m 644 ${NAME}.desktop /usr/share/apps/konqueror/servicemenus; elif [ -d /opt/kde/share/apps/konqueror/servicemenus ]; then install -m 644 ${NAME}.desktop /opt/kde/share/apps/konqueror/servicemenus; elif [ -d /opt/kde3/share/apps/konqueror/servicemenus ]; then install -m 644 ${NAME}.desktop /opt/kde3/share/apps/konqueror/servicemenus; fi
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
	gcc ${CFLAGS} -DDEBUG -g mpglib.c main.c -o ${NAME} -lm -lreadline
