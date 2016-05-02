CC ?= gcc-5
CFLAGS = -std=gnu11
CFLAGS += -Wall
CFLAGS += -pedantic
CFLAGS += -Iinclude
CFLAGS += -isystem/usr/local/include
CFLAGS += $(shell pcre-config --cflags)
CFLAGS += $(shell pcre-config --libs)
CFLAGS += $(shell pkg-config termkey --libs)
CFLAGS += -lncurses
CFLAGS += -lpthread
CFLAGS += -lm
CFLAGS += -Wno-switch
CFLAGS += -Wno-unused-value

CFLAGS += -DINSERT_BEGIN_STRING="\"$(PLUM_INSERT_ENTER)\""
CFLAGS += -DINSERT_END_STRING="\"$(PLUM_INSERT_LEAVE)\""

TEST_FILTER ?= "."

BINARIES = plum repl interpreter

ifdef NOLOG
        CFLAGS += -DPLUM_NO_LOG
endif

ifndef RELEASE
        CFLAGS += -fsanitize=undefined
        CFLAGS += -fsanitize=leak
        CFLAGS += -O0
else
        CFLAGS += -Ofast
        CFLAGS += -DPLUM_RELEASE
        CFLAGS += -DPLUM_NO_LOG
endif

ifdef GENPROF
        CFLAGS += -fprofile-generate
endif

ifdef USEPROF
        CFLAGS += -fprofile-use
endif

ifdef LTO
        CFLAGS += -flto
        CFLAGS += -fomit-frame-pointer
        CFLAGS += -fwhole-program
else
        CFLAGS += -ggdb3
endif

SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))

all: plum

plum: $(OBJECTS) plum.c
	$(CC) $(CFLAGS) -o $@ $^

interpreter: $(OBJECTS) interpreter.c
	$(CC) $(CFLAGS) -o $@ $^

repl: $(OBJECTS) repl.c
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ -DFILENAME=$(patsubst src/%.c,%,$<) $<

clean:
	rm -rf $(BINARIES) *.gcda src/*.o src/*.gcda

.PHONY: test.c
test.c: $(OBJECTS)
	./test.sh $(TEST_FILTER)

.PHONY: test
test: $(OBJECTS) test.c
	$(CC) $(CFLAGS) -o $@ $^
	time ./test

run: plum
	./plum
