include config.mak

LIBNAME = libvstream-client.a

SRCS = mfs.c object.c schema.c query.c util.c io.c partition.c crc.c vstream.c

INSTALL = install

OBJS	= $(SRCS:.c=.o)
INCLUDE = -I..
LDFLAGS += $(SOCKLIB)
CFLAGS  += $(INCLUDE)

all:	$(LIBNAME) vstream-client

install:	$(LIBNAME) vstream-client
	$(INSTALL) -m 755 vstream-client $(BINDIR)/vstream-client
	$(INSTALL) -m 644 $(LIBNAME) $(LIBDIR)/$(LIBNAME)
	$(INSTALL) -m 644 vstream-client.h $(INCDIR)/vstream-client.h

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

$(LIBNAME):	$(OBJS)
	$(AR) r $(LIBNAME) $(OBJS)

vstream-client: $(LIBNAME) test-client.c
	$(CC) $(CFLAGS) test-client.c $(LIBNAME) -o vstream-client $(LDFLAGS)

clean:
	rm -f *.o *.a *~ vstream-client vstream-client.exe

