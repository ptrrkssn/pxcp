# Makefile for pxcp

CFLAGS=-Wall -O -g
PXCPOBJS=pxcp.o

all: pxcp

pxcp: $(PXCPOBJS)
	$(CC) -g -o pxcp $(PXCPOBJS)

clean distclean:
	-rm -f core *.o pxcp; find . \( -name '*~' -o -name '#*' \) -print0 | xargs -0 rm -f


# Git targets
push: 	distclean
	git add -A && git commit -a && git push

pull:
	git pull
