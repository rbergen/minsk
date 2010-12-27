CC=gcc
LD=gcc
CFLAGS=-O2 -Wall -W -Wno-parentheses -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wredundant-decls -std=gnu99

all: minsk

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name core -or -name .depend -or -name .#*`
