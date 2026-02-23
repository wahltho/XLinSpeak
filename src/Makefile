.PHONY : clean all test test64

all : lin.xpl

CFLAGS ?= -g -Wall -Wextra -fvisibility=hidden -fPIC
LDFLAGS ?= -pthread
LIBS ?=

ifdef USE_SPEECHD
  CFLAGS += -DUSE_SPEECHD
  LIBS += -lspeechd
endif

lin.xpl : xpl.c hook.c hook.h sec.c sec.h len64.c len.h utils.c utils.h hook_asm64.o
	gcc $(CFLAGS) -shared -o $@ \
            -I SDK/CHeaders/XPLM $^ $(LDFLAGS) $(LIBS)

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
