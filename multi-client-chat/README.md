# Multi-Client Chat Server in C++

A TCP chat room with a central server and several clients connected at the same time, written for the Systems Programming course of my B.Sc. in Computer Science at SCE (Sami Shamoon College of Engineering).

## Features

- **Concurrent clients:** the server creates one thread per client
- **Thread safety:** shared state (clients, nicknames, rooms, log file, per-socket sends) is protected by mutexes
- **Nicknames, rooms, private messages and broadcast** to all users
- **System messages:** users are notified automatically when someone connects, disconnects, joins or leaves a room
- **Logging:** connections, room changes and messages are written to `chat.log`
- **Robustness:** the server keeps running when a client disconnects or a network error occurs
- **Client:** a colored menu, with a separate listener thread so incoming messages appear in real time

## Build and run

Requires Linux (or WSL) and g++.

```bash
g++ server.cpp -o server -pthread
g++ client.cpp -o client -pthread

./server 8080                 # terminal 1
./client 127.0.0.1 8080       # terminals 2, 3, ...
```

## Protocol

Line-based text over TCP, each message ends with `\n`. The client menu sends commands such as `/nick <name>`, `/join <room>`, `/room <message>`.

## Client menu

```
1. Join room
2. Send message to room
3. Send private message
4. Send message to all
5. Show connected users
6. Exit
```

## Author

Shirel Azeroual · Computer Science student, SCE. Written in a pair.
