# Makefile for pxcp

CFLAGS=-Wall -O -g
PXCPOBJS=pxcp.o

all: pxcp

pxcp: $(PXCPOBJS)
	$(CC) -g -o pxcp $(PXCPOBJS)

clean distclean:
	-rm -f core *.o *~ \#* pxcp


# Git targets
push: 	distclean
	git add -A && git commit -a && git push

pull:
	git pull
