.PHONY : clean all test

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

test : len64-test
	./len64-test

len64-test : len64.c len64_test.c len.h
	gcc -g -Wall -Wextra -o $@ len64.c len64_test.c

clean :
	rm -f *.o lin*.xpl asm*.addr asm*.bin asm*.ref dis*.addr dis*.ref len64 len64-test
	rm -rf len64-test.dSYM
