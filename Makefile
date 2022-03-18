PROJECT_ROOT = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

LDFLAGS=-pthread

OBJSC= client.o
OBJSS= server.o

ifeq ($(BUILD_MODE),debug)
	CFLAGS += -std=c17 -g3 -Wall -Werror -Wextra -m64 -fsanitize=address
else ifeq ($(BUILD_MODE),run)
	CFLAGS += -O2 -std=c17 -Wall -Werror -Wextra -m64 -fsanitize=address
else
    $(error Build mode $(BUILD_MODE) not supported by this Makefile)
endif

all:	server.o client.o

server:	$(OBJSS)
	$(CC) $(LDFLAGS) -o $@ $^

client: $(OBJSC)
	$(CC) $(LDFLAGS) -o $@ $^

%.o:	$(PROJECT_ROOT)%.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -fr build