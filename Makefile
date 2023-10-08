NAME=cutmp3
VERSION=4.0
PREFIX?=/usr/local
BINDIR=$(PREFIX)/bin
DATADIR=$(PREFIX)/share
MANDIR=$(PREFIX)/share/man/man1
DOCDIR=$(DATADIR)/doc/$(NAME)-$(VERSION)
CC=gcc
CFLAGS?=-Wall -Wformat-security -Wunused-result -O2
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
	@echo ""

clean:
	@rm -vf *.o linenoise/*.o $(NAME)

install: $(NAME)
	@install -vd $(BINDIR)
	@install -vd $(MANDIR)
	@install -vd $(DOCDIR)
	@echo -n "strip: " && strip -vso $(BINDIR)/$(NAME) $(NAME)
	@gzip -vc9 $(NAME).1 2>&1 1> $(MANDIR)/$(NAME).1.gz | sed 's|stdout|$(MANDIR)/$(NAME).1.gz|'
	@install -vm 644 README* USAGE $(DOCDIR)
	@if [ ! -z "$(KDEDIR)" ]; then install -vm 644 $(NAME).desktop $(KDEDIR)/share/apps/konqueror/servicemenus; elif [ -d /usr/share/apps/konqueror/servicemenus ]; then install -vm 644 $(NAME).desktop /usr/share/apps/konqueror/servicemenus; elif [ -d /opt/kde/share/apps/konqueror/servicemenus ]; then install -vm 644 $(NAME).desktop /opt/kde/share/apps/konqueror/servicemenus; elif [ -d /opt/kde3/share/apps/konqueror/servicemenus ]; then install -vm 644 $(NAME).desktop /opt/kde3/share/apps/konqueror/servicemenus; fi

uninstall:
	@rm -rvf $(BINDIR)/$(NAME) $(MANDIR)/$(NAME).1.gz $(DOCDIR) $(KDEDIR)/share/apps/konqueror/servicemenus/$(NAME).desktop /usr/share/apps/konqueror/servicemenus/$(NAME).desktop /opt/kde3/share/apps/konqueror/servicemenus/$(NAME).desktop

prof:
	if [ ! -e "$(NAME)" ]; then gcc $(CFLAGS) $(DBGFLAGS) -pg -DVERSION=\"$(VERSION)\" main.c mpglib.c $(LDFLAGS) -o $(NAME); elif [ -e "gmon.out" ]; then gprof -c --inline-file-names $(NAME) gmon.out > gmon.txt; fi
