LDFLAGS=-pthread

BUILD_DIR=build/

CC=gcc
CFLAGS= -g3 -std=c17 -Wall -Werror -Wextra -Wpedantic

all: build
	$(CC) $(CFLAGS) client.c -o $(BUILD_DIR)client $(LDFLAGS)
	$(CC) $(CFLAGS) server.c -o $(BUILD_DIR)server $(LDFLAGS)

build:
	mkdir -p build

clean:
	rm *.o
