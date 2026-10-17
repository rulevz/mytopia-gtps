# gtproxy - build with the same rootless toolchain as the server
#   PATH=$HOME/opt/miniforge3/envs/gtps/bin:$PATH make
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -I../../include
LDFLAGS  ?= -L../../include/enet/lib -lenet -lpthread

all: gtproxy

gtproxy: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $@ $(LDFLAGS)

clean:
	rm -f gtproxy

.PHONY: all clean
