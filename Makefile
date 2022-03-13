CXX=cc
CXXFLAGS=-std=c17 -Wall -Wextra -Werror -Wpedantic -Wshadow -g3 -fsanitize=address
LDFLAGS=-pthread

all: dir
	$(CXX) $(CXXFLAGS) src/server.c -o build/server.o $(LDFLAGS)
	$(CXX) $(CXXFLAGS) src/client.c -o build/client.o $(LDFLAGS)

dir:
	mkdir -p build
