VERSION=1.0
ARCHIVE=minsk-$(VERSION).tar.gz

CC=gcc
LD=gcc
CFLAGS=-O2 -Wall -W -Wno-parentheses -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wredundant-decls -std=gnu99

all: minsk

web: minsk
	rsync -avzP . jw:www/ext/minsk/ --exclude=.git --exclude=.*.swp --delete

release:
	git tag v$(VERSION)
	git push --tags
	git archive --format=tar --prefix=minsk-$(VERSION)/ HEAD | gzip >$(ARCHIVE)
	scp $(ARCHIVE) jw:~ftp/pub/mj/minsk/
	ssh jw "cd web && bin/release-prog minsk $(VERSION)"
	mv $(ARCHIVE) ~/archives/sw/minsk/

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name core -or -name .depend -or -name .#*`
	rm -f minsk
