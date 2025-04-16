# find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# Compile flags for linux / osx
CFLAGS ?= -W -fno-common -g -ggdb -fPIC -std=gnu99 -O3

SOURCEDIR = .
CC_SOURCES = $(wildcard $(SOURCEDIR)/*.c)
CC_OBJECTS = $(patsubst $(SOURCEDIR)/%.c, $(SOURCEDIR)/%.o, $(CC_SOURCES))

.SUFFIXES: .c .cc .o

all: millisecond_time.a hash_funcs.a

millisecond_time.a: $(CC_OBJECTS)
	ar rcs $@ $^

hash_funcs.a: $(CC_OBJECTS)
	ar rcs $@ $^

timerwheel.a: $(CC_OBJECTS)
	ar rcs $@ $^

clean:
	rm -rf *.o *.a
