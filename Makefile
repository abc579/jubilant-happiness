LDFLAGS=-pthread
BUILD_DIR=build/
SRC_DIR=src/
CC=gcc
CFLAGS=-O3 -std=c17 -Wall -Werror -Wextra -Wpedantic

all: build
	$(CC) $(CFLAGS) $(SRC_DIR)client.c $(SRC_DIR)utils.c $(SRC_DIR)user.c -o $(BUILD_DIR)client $(LDFLAGS)
	$(CC) $(CFLAGS) $(SRC_DIR)server.c $(SRC_DIR)utils.c -o $(BUILD_DIR)server $(LDFLAGS)

build:
	mkdir -p build

clean:
	rm -rf build
