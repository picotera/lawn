
# find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

CC:=$(shell sh -c 'type $(CC) >/dev/null 2>/dev/null && echo $(CC) || echo gcc')
export CC

# if DEBUG env var is set, we compile with "debug" cflags
DEBUGFLAGS = -g -ggdb -O3
ifeq ($(DEBUG), 1)
	DEBUGFLAGS = -g -ggdb -O0
endif

# Default CFLAGS
CFLAGS= -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-result -fPIC \
 	-D_GNU_SOURCE -std=gnu99 -I"$(shell pwd)" 
CFLAGS += $(DEBUGFLAGS)

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
	SHOBJ_LDFLAGS ?= -shared -Bsymbolic -Bsymbolic-functions -ldl -lpthread
else
	CFLAGS += -mmacosx-version-min=10.6
	SHOBJ_LDFLAGS ?= -macosx_version_min 10.6 -exported_symbol _RedisModule_OnLoad -bundle -undefined dynamic_lookup -ldl -lpthread
endif
export CFLAGS



# Sources
SOURCEDIR=$(shell pwd -P)
CC_SOURCES = $(wildcard $(SOURCEDIR)/*.c)
CC_SOURCES += $(wildcard $(SOURCEDIR)/utils/*.c)
CC_SOURCES += $(wildcard $(SOURCEDIR)/trie/*.c)

# Convert all sources to .o files
CC_OBJECTS = $(patsubst %.c, %.o, $(CC_SOURCES) )

# .d files for each c file. These make sure that changing a header file
# will also change the dependent .c files of it
CC_DEPS = $(patsubst %.c, %.d, $(CC_SOURCES) )

# Library dependencies
LIBTRIEMAP=trie/libtriemap.a

# Compilation deps for the module
LIBS= $(LIBTRIEMAP)
ALLDEPS=$(CC_OBJECTS) $(LIBS)

%.c: %.y

# Compile C file while generating a .d file for it
%.o: %.c
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@ -MMD -MF $(@:.o=.d)

all: lawn.so

# Include all dependency files for C files
-include $(CC_DEPS)

# Library compile rules
$(LIBTRIEMAP):
	$(MAKE) -C trie 
.PHONY: $(LIBTRIEMAP)


build_tests: lawn.so 
	$(MAKE) -C tests build

test: $(ALLDEPS) build_tests
	# low level redis-independant tests
	# ./tests/test
	$(MAKE) -C ./tests test
	# high level python integration tests
	# $(MAKE) -C pytest test

buildall:  lawn.so build_tests

# Build the lib...
lawn.so: $(ALLDEPS)
	# Just to make sure old versions of the lib are deleted
	rm -f lawn.so
	$(LD) -o $@ $(ALLDEPS) $(SHOBJ_LDFLAGS) -lc -lm 

# Build a stand-alone static library without the module entry point.
# This is used to include the module's functionality in other modules
lawn.a: CFLAGS+=-DRS_CMD_PREFIX='"_FT"' -DRS_STATIC
lawn.a: $(CC_OBJECTS) $(LIBS) # version.h
	ar rcs $@ $(CC_OBJECTS)

staticlib: lawn.a

clean:
	rm -fv *.[oad] utils/*.[oad] trie/*.[oad] tests/*.[oad] tests/*.run 
	$(MAKE) -C tests clean

distclean:
	find . -type f \( -name '*.[oad]' -o -name '*.so' \) -print | grep -v timerwheel | rm -f

package: lawn.so
	mkdir -p ../build
	module_packer -v -o "../build/lawn-oss.{os}-{architecture}.latest.zip" "`pwd`/lawn.so"

# print_version: version.h print_version.c
# 	@$(CC) -o $@ -DPRINT_VERSION_TARGET $@.c

rebuild: clean all
