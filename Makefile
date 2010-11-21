CC=gcc
LD=gcc
CFLAGS=-O2 -Wall -W -Wno-parentheses -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wredundant-decls -std=gnu99

all: minsk

examples:
	rm -rf examples
	mkdir examples
	cp ex-* examples/
	tar czvvf examples.tar.gz examples
	rm -rf examples

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name core -or -name .depend -or -name .#*`
	rm -f minsk
