# GCC defines
CC=gcc
STRIP=strip
RM=del
CAT=type
O=o
RES=o

%.$(RES): %.rc
	windres -i $< -o $@


OPENSSL_V=0.9.7m
CFLAGS=-Wall -pedantic -I dlglib -I openssl-$(OPENSSL_V)/outinc
TARGET=ntpwedit.exe
OBJS=main.$(O) ntpw.$(O) ntreg.$(O) message.$(O) dlgpass.$(O) version.$(O) \
     dlgabout.$(O) log.$(O)
DEBUG_OBJS=
LIBFILES=dlglib/libdlglib.a openssl-$(OPENSSL_V)/out/libcrypto.a
LIBS=-L dlglib -ldlglib -L openssl-$(OPENSSL_V)/out -lcrypto -lcomctl32
LDFLAGS=-mwindows -Wl,-Map,$(TARGET).map
RESS=resource.$(RES)

.PHONY: build release dep clean rebuild perldep gccdep dlglib debug

debug: OBJS+= $(DEBUG_OBJS)
debug: CFLAGS+= -DDEBUG -g
debug: $(DEBUG_OBJS) build

rebuild: clean debug

release: CFLAGS+= -DRELEASE
release: clean build
	$(STRIP) $(TARGET)

build: dlglib $(TARGET)

$(TARGET): $(LIBFILES) $(OBJS) $(RESS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(RESS) $(LIBS)
	-perl map.pl $(TARGET).map > $(TARGET).map.txt

resource.$(RES): resource.rc app.ico $(TARGET).manifest

ntpw.$(O): openssl-$(OPENSSL_V)/outinc/openssl

openssl-$(OPENSSL_V)/out/libcrypto.a openssl-$(OPENSSL_V)/outinc/openssl:
	openssl.bat openssl-$(OPENSSL_V)

dlglib:
	$(MAKE) -C $@ $(MAKECMDGOALS)

dep: dlglib openssl-$(OPENSSL_V)/outinc/openssl gccdep

perldep:
	perl makedep.pl $(O) > Makefile.dep

gccdep:
	gcc $(CFLAGS) -MM *.c > Makefile.dep

include Makefile.dep

clean: dlglib
	-$(RM) $(TARGET)
	-$(RM) *.$(O)
	-$(RM) *.$(RES)
	-$(RM) *.bak
	-$(RM) *.map
	-$(RM) *.mem
	-$(RM) *.map.txt

