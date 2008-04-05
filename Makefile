# Generated automatically from Makefile.in by configure.
#### Start of system configuration section.             -*-makefile-*- ####
 srcdir = .

VERSION=1.23

CFLAGS = -g -O2 -Wall
prefix = /usr
man1dir=${prefix}/share/man/man1
exec_prefix = ${prefix}
bindir=/sbin

INSTALL=/usr/bin/install -c

# Extension (not including `.') for the manual page filenames.
manext = 8
# Where to put the manual pages.
mandir = $(prefix)/share/man/man$(manext)

#### End of system configuration section. ####

SHELL = /bin/sh

SRC = adjtimex.c adjtimex.8 mat.c mat.h install-sh configure.in		\
 configure Makefile.in README README.ru adjtimex.lsm adjtimex.lsm.in	\
 COPYING COPYRIGHT ChangeLog

all: adjtimex adjtimex.lsm

adjtimex: adjtimex.c mat.o
	$(CC) $(CFLAGS) -I. -DVERSION=\"$(VERSION)\" -o adjtimex adjtimex.c  \
		mat.o -lm

adjtimex.lsm: adjtimex.lsm.in Makefile.in
	sed -e 's/@VERSION@/$(VERSION)/'		\
	  -e "s/@DATE@/`date +%d%b%y|tr [a-z] [A-Z]`/"	\
	  adjtimex.lsm.in >adjtimex.lsm

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
