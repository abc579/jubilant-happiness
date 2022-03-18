# jubilant-happiness

Small C chat application using sockets.

# Dependencies

- gcc
- pthread
- Unix-based OS.

# Description

This is a small C chat application that uses sockets to communicate
between the server and its clients.

# Features

- Multiple clients.
- Signal handling.
- Multi-threading.
- Logging.
- Private messages.
- Listing users in chatroom.
- IPv6.

# Usage

First compile the project:

	$ make run

Then go to the `build` directory and run the server:

	$ cd build
	$ ./server.o

Finally, run as many clients as you wish:

	$ ./client.o
