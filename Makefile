# flash - simple flashcard tool
# See LICENSE file for copyright and license details.

include config.mk

SRC = flash.c
OBJ = ${SRC:.c=.o}

all: options flash

options:
	@echo flash build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

config.h:
	cp config.def.h config.h

prompt.h: PROMPT.txt
	{ printf 'static const char prompt[] =\n'; \
	  sed 's/\\/\\\\/g; s/"/\\"/g; s/.*/"&\\n"/' PROMPT.txt; \
	  printf ';\n'; } > prompt.h

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk prompt.h

flash: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

cscope: ${SRC} config.h
	cscope -R -b || echo cScope not installed

clean:
	rm -f flash ${OBJ} flash-${VERSION}.tar.gz prompt.h

dist: clean
	mkdir -p flash-${VERSION}
	cp -R LICENSE Makefile config.mk config.def.h flash.1 PROMPT.txt ${SRC} flash-${VERSION}
	tar -cf flash-${VERSION}.tar flash-${VERSION}
	gzip flash-${VERSION}.tar
	rm -rf flash-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f flash ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/flash
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	cp flash.1 ${DESTDIR}${MANPREFIX}/man1/flash.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/flash.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/flash

.PHONY: all options clean dist install uninstall cscope
