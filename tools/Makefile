# Desktop build of the cores + headless runner (used by CI as a compile check,
# and for testing against real ROMs on a PC):
#   make -C tools && ./tools/headless 110dance 110songdancemat.bin 500 out/
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall
SRC = headless.cpp ../app/src/main/cpp/core_registry.cpp \
      $(wildcard ../app/src/main/cpp/common/*.cpp) \
      $(wildcard ../app/src/main/cpp/cores/*/*.cpp)

headless: $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

clean:
	rm -f headless
