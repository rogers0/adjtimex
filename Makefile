#### Start of system configuration section.             -*-makefile-*- ####
 srcdir = .
VPATH = .

VERSION=1.27.1

CFLAGS = -Wall -g -O2 -Wall
prefix = /usr
man1dir=${prefix}/share/man/man1
exec_prefix = ${prefix}
bindir=/sbin
datadir = ${datarootdir}
datarootdir = ${prefix}/share

INSTALL=/usr/bin/install -c


# Extension (not including `.') for the manual page filenames.
manext = 8
# Where to put the manual pages.
mandir = $(prefix)/share/man/man$(manext)

#### End of system configuration section. ####

SRC = adjtimex.c adjtimex.8 mat.c mat.h install-sh configure.in		\
 configure Makefile.in config.h.in README README.ru adjtimex.lsm	\
  adjtimex.lsm.in COPYING COPYRIGHT ChangeLog

all: adjtimex adjtimex.lsm Makefile

configure config.h.in: configure.in
	autoconf
	autoheader
Makefile config.h: Makefile.in config.h.in
	./configure

adjtimex: adjtimex.c mat.o config.h Makefile
	$(CC) $(CFLAGS) -I. -DVERSION=\"$(VERSION)\" -o adjtimex  \
		 adjtimex.c mat.o -lm

mat.o: mat.c
	$(CC) $(CFLAGS) -c mat.c

install: all
	$(INSTALL) -g bin -m 755 -o root adjtimex $(bindir)/adjtimex
	$(INSTALL) -d -g root -m 755 -o root $(mandir)
	-$(INSTALL) -g root -m 644 -o root $(srcdir)/adjtimex.8 \
		$(mandir)/adjtimex.$(manext)

uninstall:
	rm -f $(bindir)/adjtimex $(mandir)/adjtimex.$(manext)

clean: 
	rm -f core *.o
veryclean: clean
	rm -f adjtimex

adjtimex.lsm: adjtimex.lsm.in Makefile
	sed -e 's/@VERSION@/$(VERSION)/'	\
	  -e "s/@DATE@/`date +%Y-%m-%d`/"	\
	  adjtimex.lsm.in >adjtimex.lsm

shar: $(SRC)
	distname=adjtimex-$(VERSION);		\
	shar $(SRC) >$$distname.shar 

dist: $(SRC)
	distname=adjtimex-$(VERSION);					      \
	rm -fr $$distname;						      \
	mkdir $$distname;						      \
	for file in $(SRC); do						      \
	  ln $$file $$distname/$$file					      \
	  || { echo copying $$file instead; cp -p $$file $$distname/$$file;}; \
	done;								      \
	chmod -R a+rX $$distname;					      \
	tar -chz -f $$distname.tar.gz $$distname;			      \
	rm -fr $$distname

