CC ?= gcc-5
CFLAGS = -std=c11
CFLAGS += -Wall
CFLAGS += -pedantic
CFLAGS += -Iinclude
CFLAGS += -g3
CFLAGS += -ltickit
CFLAGS += -lncurses
CFLAGS += -Wno-switch

TEST_FILTER ?= "."

BINARIES = plum repl interpreter

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

all: plum interpreter repl test

plum: $(OBJECTS) plum.c
	$(CC) $(CFLAGS) -o $@ $^

interpreter: $(OBJECTS) interpreter.c
	$(CC) $(CFLAGS) -o $@ $^

repl: $(OBJECTS) repl.c
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ -DFILENAME=$(patsubst src/%.c,%,$<) $<

clean:
	rm -rf $(BINARIES) src/*.o

.PHONY: test.c
test.c: $(OBJECTS)
	./test.sh $(TEST_FILTER)

.PHONY: test
test: $(OBJECTS) test.c
	$(CC) $(CFLAGS) -o $@ $^
	time ./test

run: plum
	./plum
