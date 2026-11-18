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
# the Atari 8-bit core (Atari800 as libatari800) is C too
A8 = $(CPP)/cores/atari800/a800
A8SRC = $(wildcard $(A8)/*.c) $(wildcard $(A8)/libatari800/*.c) $(wildcard $(A8)/roms/*.c)
COBJ = $(patsubst $(CPP)/%.c,obj/%.o,$(CSRC))
A8OBJ = $(patsubst $(CPP)/%.c,obj/%.o,$(A8SRC))

headless: $(SRC) $(COBJ) $(A8OBJ)
	$(CXX) $(CXXFLAGS) $(DEFS) -o $@ $(SRC) $(COBJ) $(A8OBJ)

obj/cores/atari800/a800/%.o: $(A8)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(A8) -c $< -o $@

obj/%.o: $(CPP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEFS) -c $< -o $@

clean:
	rm -rf headless obj
