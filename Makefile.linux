# $Id$

CFLAGS+=	-Wall
PROG=		tekilda
SRCS=		tekilda.c tekplot.c
OBJS=		$(SRCS:.c=.o)

$(PROG): $(OBJS)

clean:
	@rm -f $(PROG) *.core core *.o
