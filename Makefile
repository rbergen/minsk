CC=gcc
LD=gcc
CFLAGS=-O2 -Wall -W -Wno-parentheses -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wredundant-decls -std=gnu99

all: minsk

museum:
	rm -rf museum
	mkdir museum
	cp ex-* museum/
	tar czvvf museum.tar.gz museum
	rm -rf museum

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name core -or -name .depend -or -name .#*`
	rm -f minsk museum.tar.gz
