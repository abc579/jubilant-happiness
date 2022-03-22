# jubilant-happiness

Small C chat application using sockets.

# Dependencies

- gcc
- pthread
- Unix-based OS.

# Description

This is a small C chat application that uses sockets to communicate
between the server and its clients.

I have no experience with C whatsoever so please take this code with
a grain of salt.

# Features

- Up to seven clients. You can change it. If you do, remember to add
  colours as well.
- Signal handling.
- Multi-threading.
- Logging public messages to file. The file `log.txt` gets created
  while chatting. It only logs public messages.
- Private messages (whispers). Shown as italic text.
- Listing users in chatroom.
- IPv6.
- Up to seven unique client name colours.
- Trimmed and truncated messages. (trying to avoid buffer overflows)

# Usage

Clone the repository.

	git clone https://github.com/abc579/jubilant-happiness.git

Compile.

    cd jubilant-happiness
    make

Run the server.

    cd build
    ./server

Run N clients and chat.

    cd build
    ./client

# Screenshots

![Example](example/sample.png?raw=true "Chat example")

![Whispers](example/whispers.png?raw=true "Whispers example")
