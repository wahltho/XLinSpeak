.PHONY : clean all test test64

all : lin.xpl

CFLAGS ?= -g -Wall -Wextra -fvisibility=hidden -fPIC
LDFLAGS ?= -pthread -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$$ORIGIN/../liblinux' -Wl,--enable-new-dtags
LIBS ?=
LIN_XPL_SRCS = xpl.c hook.c sec.c len64.c utils.c hook_asm64.o
LIN_XPL_DEPS = $(LIN_XPL_SRCS) hook.h sec.h len.h utils.h version.h

ifdef USE_SPEECHD
  CFLAGS += -DUSE_SPEECHD
  LIBS += -lspeechd
endif

ifdef USE_PULSE
  CFLAGS += -DUSE_PULSE
  LIBS += -lpulse-simple -lpulse
endif

lin.xpl : $(LIN_XPL_DEPS)
	gcc $(CFLAGS) -shared -o $@ \
            -I SDK/CHeaders/XPLM $(LIN_XPL_SRCS) $(LDFLAGS) $(LIBS)

hook_asm64.o : hook_asm64.asm
	nasm -f elf64 -o $@ $^

test : test64

test64 : asm64.ref len64
	./len64 asm64.bin > dis64.ref
	sed '/^[[:blank:]]/d;s/[[:blank:]].*$$//' dis64.ref > dis64.addr
	sed '/^[[:blank:]]/d;s/[[:blank:]].*$$//' asm64.ref > asm64.addr
	diff dis64.addr asm64.addr

asm64.bin : asm64.asm
	nasm -f bin -o $@ $^

asm64.ref : asm64.bin
	ndisasm -b64 $^ > $@

len64 : len64.c
	gcc -g -Wall -Wextra -o $@ -DTEST_LEN $^

clean :
	rm -f *.o lin*.xpl asm*.addr asm*.bin asm*.ref dis*.addr dis*.ref len64
