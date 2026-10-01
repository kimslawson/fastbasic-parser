#
#  fbp - The missing parser for FastBasic
#
#  Simple makefile, the only requirement is a C++17 compiler.
#
#  To cross-compile (for example to Windows), set CXX to the cross compiler
#  and EXT to the executable extension, the embed tool is built with HOSTCXX:
#
#    make CXX=x86_64-w64-mingw32-g++ EXT=.exe
#

CXX ?= c++
HOSTCXX ?= $(CXX)
EXT ?=
CXXFLAGS ?= -O2 -Wall
CXXFLAGS += -std=c++17
LDFLAGS ?=

B = build
VENDOR = vendor/fastbasic

# fbp sources
SRC = \
 src/engine.cc \
 src/grammar.cc \
 src/listing.cc \
 src/listlong.cc \
 src/listshort.cc \
 src/main.cc \
 src/optimize.cc \
 src/program.cc \
 src/rename.cc \
 src/verify.cc \

# Unmodified FastBasic sources
FBSRC = \
 $(VENDOR)/compiler/atarifp.cc \
 $(VENDOR)/compiler/ifile.cc \
 $(VENDOR)/compiler/looptype.cc \
 $(VENDOR)/compiler/peephole.cc \
 $(VENDOR)/compiler/synt-optimize.cc \
 $(VENDOR)/compiler/synt-parser.cc \
 $(VENDOR)/compiler/synt-preproc.cc \
 $(VENDOR)/compiler/synt-pstate.cc \
 $(VENDOR)/compiler/synt-sm.cc \
 $(VENDOR)/compiler/synt-symlist.cc \
 $(VENDOR)/compiler/synt-wlist.cc \
 $(VENDOR)/compiler/vartype.cc \

# Embedded grammar and target files
EMBED_FILES = $(wildcard $(VENDOR)/syntax/*.syn) $(wildcard $(VENDOR)/targets/*.tgt) \
              $(VENDOR)/VERSION
EMBED_SRC = $(B)/gen/embedded.cc

OBJ = $(patsubst %.cc,$(B)/obj/%.o,$(SRC) $(FBSRC)) $(B)/obj/embedded.o

TARGET = $(B)/fbp$(EXT)

INCLUDES = -Isrc -I$(VENDOR)/compiler

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJ)

$(B)/obj/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c -o $@ $<

$(B)/obj/embedded.o: $(EMBED_SRC)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(EMBED_SRC): $(B)/embed $(EMBED_FILES)
	@mkdir -p $(dir $@)
	$(B)/embed $@ $(EMBED_FILES)

$(B)/embed: tools/embed.cc
	@mkdir -p $(dir $@)
	$(HOSTCXX) -O1 -std=c++17 -o $@ $<

test: $(TARGET)
	sh tests/run-tests.sh $(TARGET)

clean:
	rm -rf $(B)

.PHONY: all test clean

-include $(OBJ:.o=.d)
