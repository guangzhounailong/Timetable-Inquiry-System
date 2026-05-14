# Course Timetable Inquiry System

A C++ client-server course timetable inquiry system for querying and managing course schedules.

## Tech Stack

- C++17
- Winsock TCP sockets
- `std::thread` per client connection
- CSV storage
- wxWidgets GUI client
- Optional AES-256-GCM secure channel

## Core Features

- Student course query
- Administrator login
- Add, update, and delete courses
- CSV persistence through `data/courses.csv`
- Multiple concurrent clients
- Optional secure communication with a pre-shared key

## Build

From the repository root on Windows:

```powershell
g++ -std=c++17 -Iinclude src/server.cpp src/CourseDatabase.cpp src/SecureChannel.cpp -o bin/server.exe -lws2_32 -lbcrypt
g++ -std=c++17 -Iinclude src/client.cpp src/SecureChannel.cpp -o bin/client.exe -lws2_32 -lbcrypt
```

## Run

Start the server:

```powershell
.\bin\server.exe
```

Start a console client in another terminal:

```powershell
.\bin\client.exe
```

Optional secure mode:

```powershell
.\bin\server.exe --secure YourSharedSecret
.\bin\client.exe 127.0.0.1 54000 --secure YourSharedSecret
```

See [docs/README.md](docs/README.md) for full build instructions, protocol details, GUI usage, and demo steps.
