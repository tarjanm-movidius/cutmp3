NAME=cutmp3
VERSION=4.0
PREFIX?=/usr/local
BINDIR=$(PREFIX)/bin
DATADIR=$(PREFIX)/share
MANDIR=$(PREFIX)/share/man/man1
DOCDIR=$(DATADIR)/doc/$(NAME)-$(VERSION)
CC=gcc
CFLAGS?=-Wall -O2
LDFLAGS?=-lm -lreadline
DBGFLAGS:=-DDEBUG -g
LNSFLAGS:=-DLINENOISE
OBJECTS:=main.o mpglib.o

.PHONY: clean debug install uninstall

all: $(NAME)
debug: CFLAGS+=$(DBGFLAGS)
debug: $(NAME)
main.o: cutmp3.h
main.o: CFLAGS+=-DVERSION=\"$(VERSION)\"
*.o: Makefile mpglib.h

ifeq ($(LNOISE), yes)
CFLAGS+=$(LNSFLAGS)
OBJECTS+=linenoise/linenoise.o
LDFLAGS:=$(filter-out -lreadline,$(LDFLAGS))
*.o: linenoise/linenoise.h
linenoise/linenoise.o: Makefile linenoise/linenoise.h
endif

all: $(info )
all: $(info *** You need readline-devel or similar to compile $(NAME). Alternatively call 'make LNOISE=yes' ***)
all: $(info ***  to build with linenoise, a self contained line editor. (see README for more information)  ***)
all: $(info )

$(NAME): $(OBJECTS)
	$(CC) -o $(NAME) $(OBJECTS) $(LDFLAGS)

clean:
	@rm -vf *.o linenoise/*.o $(NAME)

install:
	install -d $(BINDIR)
	install $(NAME) $(BINDIR)
	strip $(BINDIR)/$(NAME)
	if [ ! -z "$(KDEDIR)" ]; then install -m 644 $(NAME).desktop $(KDEDIR)/share/apps/konqueror/servicemenus; elif [ -d /usr/share/apps/konqueror/servicemenus ]; then install -m 644 $(NAME).desktop /usr/share/apps/konqueror/servicemenus; elif [ -d /opt/kde/share/apps/konqueror/servicemenus ]; then install -m 644 $(NAME).desktop /opt/kde/share/apps/konqueror/servicemenus; elif [ -d /opt/kde3/share/apps/konqueror/servicemenus ]; then install -m 644 $(NAME).desktop /opt/kde3/share/apps/konqueror/servicemenus; fi
	install -d $(DOCDIR)/$(NAME)
	install -m 644 README* USAGE $(DOCDIR)/$(NAME)
	install -d $(MANDIR)
	install -m 644 $(NAME).1 $(MANDIR)
	gzip $(MANDIR)/$(NAME).1

uninstall:
	@rm -rvf $(BINDIR)/$(NAME) $(DOCDIR)/$(NAME) $(MANDIR)/$(NAME).1.gz $(KDEDIR)/share/apps/konqueror/servicemenus/$(NAME).desktop /usr/share/apps/konqueror/servicemenus/$(NAME).desktop /opt/kde3/share/apps/konqueror/servicemenus/$(NAME).desktop
