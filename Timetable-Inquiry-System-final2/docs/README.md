# Course Timetable Inquiry System

## Project Description

This project implements a client-server Course Timetable Inquiry System for Data Communications and Networking Assignment 2. It uses C++17 and Windows Socket Programming (Winsock). Students can query course timetable records through a network client. Administrators can log in and add, update, or delete records. Timetable data is stored in a CSV file.

The system is designed to run on one Windows computer for demonstration: start one server and then open several client console windows connected to `127.0.0.1`.

## System Architecture

- Client: provides a simple console menu, sends protocol commands to the server, and displays responses.
- Server: listens for TCP connections, creates one `std::thread` per client, processes requests, and returns responses.
- Database: uses `data/courses.csv` as file-based storage and keeps records in memory while the server is running.
- Synchronization: uses `std::mutex` in `CourseDatabase` to protect course records and CSV file writes.
- Monitoring: the server logs client connect/disconnect events and active client count.

## File Structure

```text
CourseTimetableSystem/
|-- src/
|   |-- server.cpp              # Winsock server, request handling, multithreading
|   |-- client.cpp              # Console menu client and raw command client
|   |-- CourseDatabase.cpp      # CSV loading, saving, querying, add/update/delete
|   |-- SecureChannel.cpp       # Optional AES-256-GCM session (BCrypt)
|   `-- wx_client.cpp           # wxWidgets GUI client
|-- include/
|   |-- CourseDatabase.h        # Course data structure and database interface
|   |-- Protocol.h              # Line-based protocol helpers and socket helpers
|   `-- SecureChannel.h         # Encrypted transport helpers
|-- data/
|   `-- courses.csv             # Timetable database
|-- scripts/
|   `-- build_wx_client_vs.bat  # Visual Studio wxWidgets build script
|-- docs/
|   `-- README.md               # Build, run, protocol, and requirement mapping
|-- bin/                        # Compiled executables
`-- build/                      # Intermediate build files
```

## How to Compile on Windows

Run commands inside the `CourseTimetableSystem` folder.

### MinGW g++

```powershell
g++ -std=c++17 -Iinclude src/server.cpp src/CourseDatabase.cpp src/SecureChannel.cpp -o bin/server.exe -lws2_32 -lbcrypt
g++ -std=c++17 -Iinclude src/client.cpp src/SecureChannel.cpp -o bin/client.exe -lws2_32 -lbcrypt
```

Optional wxWidgets GUI client, using an MSYS2/MinGW environment where
`wx-config` is available:

```powershell
$wxCflags = wx-config --cxxflags
$wxLibs = wx-config --libs
g++ -std=c++17 -Iinclude src/wx_client.cpp src/SecureChannel.cpp -o bin/wx_client.exe $wxCflags $wxLibs -lws2_32 -lbcrypt -mwindows
```

### Visual Studio Developer Command Prompt

```cmd
cl /EHsc /std:c++17 /Iinclude src\server.cpp src\CourseDatabase.cpp src\SecureChannel.cpp /Fe:bin\server.exe Ws2_32.lib bcrypt.lib
cl /EHsc /std:c++17 /Iinclude src\client.cpp src\SecureChannel.cpp /Fe:bin\client.exe Ws2_32.lib bcrypt.lib
```

For the local Visual Studio wxWidgets installation in `E:\wxWidgets`, open
the **x64 Native Tools Command Prompt for VS** and build wxWidgets first:

```cmd
set WXWIN=E:\wxWidgets
cd /d %WXWIN%\build\msw
nmake /f makefile.vc BUILD=release SHARED=0 UNICODE=1 TARGET_CPU=X64 RUNTIME_LIBS=dynamic
```

If the build reports source encoding errors such as `C4819` or fails inside
`LexJulia.cxx`, use this command instead. The GUI client does not need the STC
component:

```cmd
nmake /f makefile.vc BUILD=release SHARED=0 UNICODE=1 TARGET_CPU=X64 RUNTIME_LIBS=dynamic USE_STC=0 CXXFLAGS="/utf-8 /std:c++17"
```

Then build the GUI client from this project folder:

```cmd
scripts\build_wx_client_vs.bat
```

This project script loads the local VS 18 x64 compiler environment when it is
available, because the wxWidgets libraries in `E:\wxWidgets\lib\vc_x64_lib`
were built with that toolset.

## How to Run the Server

```powershell
.\bin\server.exe
```

Optional custom port:

```powershell
.\bin\server.exe 54000
```

Optional **encrypted sessions** (AES-256-GCM with a pre-shared key). Clients that support it negotiate after the greeting; plain-text clients are still allowed.

```powershell
.\bin\server.exe --secure YourSharedSecret 54000
```

Keep the server window open. Run it from the project root so the server reads and writes `data/courses.csv`.

## How to Run the Client

Open another terminal window:

```powershell
.\bin\client.exe
```

Optional server IP and port:

```powershell
.\bin\client.exe 127.0.0.1 54000
```

Encrypted transport (must match server `--secure` password):

```powershell
.\bin\client.exe 127.0.0.1 54000 --secure YourSharedSecret
```

For concurrency testing, open at least five client windows and connect them to the same server.

If you double-click `bin\client.exe` before the server is running, the client may show:

```text
connect failed: 10061
```

This means the connection was refused because no server is listening on `127.0.0.1:54000`. Start `bin\server.exe` first, keep the server window open, and then start one or more clients.

## How to Run the GUI Client

Start the server first:

```powershell
.\bin\server.exe
```

Then start the wxWidgets client:

```powershell
.\bin\wx_client.exe
```

The GUI client connects to `127.0.0.1:54000` by default. It provides tabs for
student queries, administrator login and update operations, and raw protocol
commands. Administrator credentials are still `admin / 1234`.

Enter the same **PSK** in the **PSK** field when connecting if the server was started with `--secure`.

## Communication Protocol

The protocol is line-based. Each request is one line ending with `\n`.

### Requests

```text
LOGIN admin 1234
QUERY_FILTER CourseCode|Instructor|Semester|Day|StartTime|EndTime
QUERY_CODE COMP3003
QUERY_INSTRUCTOR John
QUERY_SEMESTER 2026S
QUERY_TIME Mon 09:00 12:00
QUERY_TIME Mon 09:00-12:00
ADD CourseCode|CourseTitle|Section|Instructor|Day|StartTime|EndTime|Classroom|Semester
UPDATE CourseCode Section Semester Field NewValue
UPDATE CourseCode Section Field NewValue
UPDATE CourseCode Field NewValue
DELETE CourseCode Section Semester
DELETE CourseCode Section
EXIT
```

For `ADD`, fields are separated by `|` so that titles and instructor names can contain spaces.
Course uniqueness is based on `CourseCode + Section + Semester`, so the same
course and section can be offered again in another semester.

Supported update fields:

```text
CourseTitle, Instructor, Day, StartTime, EndTime, Time, Classroom, Semester
```

Examples:

```text
UPDATE COMP3003 A 2026S Classroom B203
UPDATE COMP3003 A Classroom B203
UPDATE COMP3003 A StartTime 10:00
UPDATE COMP3003 A Time Mon 10:00 13:00
UPDATE COMP1010 Classroom A110
DELETE COMP3003 A 2026S
```

`UPDATE CourseCode Section Field NewValue` and `DELETE CourseCode Section`
remain available for older clients. If that course code and section match
records in more than one semester, the server asks for the semester-specific
format. `UPDATE CourseCode Field NewValue` works only when the course code
matches exactly one record.

### Responses

```text
SUCCESS Administrator login accepted.
FAILURE Invalid administrator username or password.
RESULT <number of records>
CourseCode|CourseTitle|Section|Instructor|Day|StartTime|EndTime|Classroom|Semester
END
ERROR <reason>
OK <message>
```

Query responses now return pipe-separated course rows. The field order is:
`CourseCode|CourseTitle|Section|Instructor|Day|StartTime|EndTime|Classroom|Semester`.

## Example Commands

Student query:

```text
QUERY_CODE COMP3003
QUERY_INSTRUCTOR John
QUERY_SEMESTER 2026S
QUERY_TIME Mon 09:00 12:00
QUERY_FILTER COMP|John|2026S|Mon||
```

Administrator operations:

```text
LOGIN admin 1234
ADD COMP4999|Special Topics in Networking|A|Henry Yu|Mon|16:00|18:00|E401|2026S
UPDATE COMP4999 A 2026S Classroom E402
DELETE COMP4999 A 2026S
```

## How This Project Satisfies the 6 Functional Requirements

### 1. Database Module

- Stores timetable data in `data/courses.csv`.
- Uses required fields: `CourseCode, CourseTitle, Section, Instructor, Day, StartTime, EndTime, Classroom, Semester`.
- Treats `CourseCode + Section + Semester` as the unique course identity.
- Loads CSV records when the server starts and writes changes immediately.

### 2. Query Module

- Supports search by course code: `QUERY_CODE`.
- Supports search by instructor: `QUERY_INSTRUCTOR`.
- Supports viewing all courses in a semester: `QUERY_SEMESTER`.
- Displays results in a clear table.

### 3. User Management Module

- Supports two roles:
  - Student: default role, query only.
  - Administrator: full access after login.
- Administrator account:
  - Username: `admin`
  - Password: `1234`

### 4. Information Update Module

- Administrator can add records with `ADD`.
- Administrator can update fields with `UPDATE`.
- Administrator can delete records with `DELETE`.
- Every modification is saved to `data/courses.csv` immediately, so later queries from other clients see the latest data.

### 5. Networking and Concurrency Module

- Uses Windows Winsock TCP sockets in C++.
- Uses `std::thread` to handle every connected client.
- Supports at least 5 concurrent clients on the same server.
- Uses safe parsing and error responses for invalid input.

### 6. Communication Protocol

- Defines clear request formats, response formats, and error messages.
- Uses `SUCCESS`, `FAILURE`, `RESULT`, `ERROR`, and `OK`.
- Includes all required operations: login, query, add, update, delete, and exit.

## Non-Functional Requirements

- Handles invalid or malformed requests without crashing the server.
- Uses a modular structure: protocol helpers, database module, server, and client are separated.
- Uses a mutex to avoid CSV corruption during concurrent updates.
- Provides reasonable response time because data is held in memory and saved only after modifications.
- Server logs client connections and disconnections for basic monitoring.

## Bonus Features

### Bonus 1: Advanced Search

Implemented with `QUERY_TIME Day StartTime EndTime`. It returns courses whose class time overlaps the requested time slot.

Example:

```text
QUERY_TIME Mon 09:00 12:00
```

### Bonus 2: Simple Client Menu Interface

Implemented in `src/client.cpp`. The client provides a menu for student queries and administrator operations. It also provides a raw protocol command option for demonstration.

### Bonus 3: wxWidgets GUI Client

Implemented in `src/wx_client.cpp`. The GUI client keeps the same line-based
protocol and provides:

- connection controls for server IP and port;
- query tabs for course code, instructor, semester, and time slot;
- administrator login, add, update, and delete controls;
- a result table plus raw server response view.

No server-side change is required because the GUI client sends the same
protocol commands as the console client.

### Bonus 4: Secure communication (optional)

Implemented with Windows **BCrypt** (`src/SecureChannel.cpp`): after the usual
plaintext greeting, the client may send `SECURE_V1 <32-hex nonce>` and receive
`SECURE_OK <server-nonce>`; both sides derive an AES-256 key with SHA-256 over
the PSK and nonces, then wrap each application message in AES-256-GCM frames
(length-prefixed). Start the server with `--secure <psk>`; use `client.exe ...
--secure <psk>` or the GUI **PSK** field. Legacy clients without this step keep
working on the same port.

## Demo Steps for Presentation

1. Start `bin\server.exe` and show the listening port.
2. Start one client and query `COMP3003`.
3. Query by instructor using `John`.
4. Query by semester using `2026S`.
5. Use advanced search: `QUERY_TIME Mon 09:00 12:00`.
6. Open at least five clients to show concurrent connections.
7. In one client, log in with `admin / 1234`.
8. Add a new course.
9. Query the new course from another client to show immediate reflection.
10. Update the classroom or time.
11. Query again from another client to prove the update is visible.
12. Delete the course and query again to show removal.
