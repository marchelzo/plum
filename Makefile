CC ?= gcc-5
CFLAGS = -std=c11
CFLAGS += -Wall
CFLAGS += -pedantic
CFLAGS += -Iinclude
CFLAGS += -g3
CFLAGS += -ltickit
CFLAGS += -lncurses

ifndef RELEASE
        CFLAGS += -fsanitize=undefined
        CFLAGS += -fsanitize=leak
        CFLAGS += -O0
else
        CFLAGS += -O3
        CFLAGS += -DPLUM_RELEASE
endif

SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))

plum: $(OBJECTS) plum.c
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ -DFILENAME=$(patsubst src/%.c,%,$<) $<
clean:
	rm -rf src/*.o

.PHONY: test.c
test.c: $(OBJECTS)
	./test.sh

.PHONY: test
test: $(OBJECTS) test.c
	$(CC) $(CFLAGS) -o $@ $^
	time ./test

run: plum
	./plum
