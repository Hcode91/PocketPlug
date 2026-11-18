# Desktop build of the cores + headless runner (used by CI as a compile check,
# and for testing against real ROMs on a PC):
#   make -C tools && ./tools/headless 110dance 110songdancemat.bin 500 out/
CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -O2 -std=c++17 -Wall
CFLAGS ?= -O2 -std=gnu99 -w
DEFS = -DPOCKETPLUG '-DMUSASHI_CNF="../m68kconf.h"'
CPP = ../app/src/main/cpp
SRC = headless.cpp $(CPP)/core_registry.cpp \
      $(wildcard $(CPP)/common/*.cpp) \
      $(wildcard $(CPP)/cores/*/*.cpp)
# the UNIX PC core (FreeBee + Musashi) is C
FB = $(CPP)/cores/unixpc/freebee
CSRC = $(wildcard $(FB)/*.c) $(FB)/musashi/m68kcpu.c $(FB)/musashi/m68kops.c \
       $(FB)/musashi/m68kdasm.c $(FB)/musashi/softfloat/softfloat.c
COBJ = $(patsubst $(CPP)/%.c,obj/%.o,$(CSRC))

headless: $(SRC) $(COBJ)
	$(CXX) $(CXXFLAGS) $(DEFS) -o $@ $(SRC) $(COBJ)

obj/%.o: $(CPP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEFS) -c $< -o $@

clean:
	rm -rf headless obj
