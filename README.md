# jubilant-happiness

Small C chat application using sockets.

# Dependencies

- gcc
- pthread
- Unix-based OS.

# Description

This is a small C chat application that uses sockets to communicate
between the server and its clients.

I have no experience with C whatsoever so please take this code with a grain of salt.

# Features

- Multiple clients.
- Signal handling.
- Multi-threading.
- Logging public messages to file.
- Private messages (whispers).
- Listing users in chatroom.
- IPv6.
- Client name colours.
- Trimmed messages.

# Usage

1) Clone the repository.

	git clone https://github.com/abc579/jubilant-happiness.git

2) Compile.

	cd jubilant-happiness
	make

3) Run the server.

	cd build
	./server

4) Run N clients and chat.

	cd build
	./client

# Screenshots

TODO

# TODO

- Leak analysis.
