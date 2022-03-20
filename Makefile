LDFLAGS=-pthread
BUILD_DIR=build/
CC=gcc
CFLAGS=-O2 -std=c17 -Wall -Werror -Wextra -Wpedantic

all: build
	$(CC) $(CFLAGS) client.c utils.c -o $(BUILD_DIR)client $(LDFLAGS)
	$(CC) $(CFLAGS) server.c utils.c -o $(BUILD_DIR)server $(LDFLAGS)

build:
	mkdir -p build

clean:
	rm *.o
