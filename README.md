# Course Timetable Inquiry System

## Project Description

This project implements a client-server Course Timetable Inquiry System for Data Communications and Networking Assignment 2. It uses C++17 and Windows Socket Programming (Winsock). Students can query course timetable records through a network client. Administrators can log in and add, update, or delete records. Timetable data is stored in a CSV file.

The system is designed to run on one Windows computer for demonstration: start one server and then open several client console windows connected to `127.0.0.1`.

## System Architecture

- Client: provides a simple console menu, sends protocol commands to the server, and displays responses.
- Server: listens for TCP connections, creates one `std::thread` per client, processes requests, and returns responses.
- Database: uses `courses.csv` as file-based storage and keeps records in memory while the server is running.
- Synchronization: uses `std::mutex` in `CourseDatabase` to protect course records and CSV file writes.
- Monitoring: the server logs client connect/disconnect events and active client count.

## File Structure

```text
CourseTimetableSystem/
├── server.cpp              # Winsock server, request handling, multithreading
├── client.cpp              # Console menu client and raw command client
├── CourseDatabase.h        # Course data structure and database interface
├── CourseDatabase.cpp      # CSV loading, saving, querying, add/update/delete
├── Protocol.h              # Line-based protocol helpers and socket send/receive helpers
├── courses.csv             # Timetable database
├── README.md               # Build, run, protocol, and requirement mapping
└── presentation_outline.md # 10-minute English presentation outline
```

## How to Compile on Windows

Run commands inside the `CourseTimetableSystem` folder.

### MinGW g++

```powershell
g++ -std=c++17 server.cpp CourseDatabase.cpp -o server.exe -lws2_32
g++ -std=c++17 client.cpp -o client.exe -lws2_32
```

Optional wxWidgets GUI client, using an MSYS2/MinGW environment where
`wx-config` is available:

```powershell
$wxCflags = wx-config --cxxflags
$wxLibs = wx-config --libs
g++ -std=c++17 wx_client.cpp -o wx_client.exe $wxCflags $wxLibs -lws2_32 -mwindows
```

### Visual Studio Developer Command Prompt

```cmd
cl /EHsc /std:c++17 server.cpp CourseDatabase.cpp Ws2_32.lib
cl /EHsc /std:c++17 client.cpp Ws2_32.lib
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
build_wx_client_vs.bat
```

This project script loads the local VS 18 x64 compiler environment when it is
available, because the wxWidgets libraries in `E:\wxWidgets\lib\vc_x64_lib`
were built with that toolset.

## How to Run the Server

```powershell
.\server.exe
```

Optional custom port:

```powershell
.\server.exe 54000
```

Keep the server window open. The server reads and writes `courses.csv` in the same folder.

## How to Run the Client

Open another terminal window:

```powershell
.\client.exe
```

Optional server IP and port:

```powershell
.\client.exe 127.0.0.1 54000
```

For concurrency testing, open at least five client windows and connect them to the same server.

If you double-click `client.exe` before the server is running, the client may show:

```text
connect failed: 10061
```

This means the connection was refused because no server is listening on `127.0.0.1:54000`. Start `server.exe` first, keep the server window open, and then start one or more clients.

## How to Run the GUI Client

Start the server first:

```powershell
.\server.exe
```

Then start the wxWidgets client:

```powershell
.\wx_client.exe
```

The GUI client connects to `127.0.0.1:54000` by default. It provides tabs for
student queries, administrator login and update operations, and raw protocol
commands. Administrator credentials are still `admin / 1234`.

## Communication Protocol

The protocol is line-based. Each request is one line ending with `\n`.

### Requests

```text
LOGIN admin 1234
QUERY_CODE COMP3003
QUERY_INSTRUCTOR John
QUERY_SEMESTER 2026S
QUERY_TIME Mon 09:00 12:00
QUERY_TIME Mon 09:00-12:00
ADD CourseCode|CourseTitle|Section|Instructor|Day|StartTime|EndTime|Classroom|Semester
UPDATE CourseCode Section Field NewValue
UPDATE CourseCode Field NewValue
DELETE CourseCode Section
EXIT
```

For `ADD`, fields are separated by `|` so that titles and instructor names can contain spaces.

Supported update fields:

```text
CourseTitle, Instructor, Day, StartTime, EndTime, Time, Classroom, Semester
```

Examples:

```text
UPDATE COMP3003 A Classroom B203
UPDATE COMP3003 A StartTime 10:00
UPDATE COMP3003 A Time Mon 10:00 13:00
UPDATE COMP1010 Classroom A110
```

The second update format works only when the course code matches exactly one record. If several sections exist, the server returns an error and asks for the section.

### Responses

```text
SUCCESS Administrator login accepted.
FAILURE Invalid administrator username or password.
RESULT <number of records>
<readable table>
END
ERROR <reason>
OK <message>
```

## Example Commands

Student query:

```text
QUERY_CODE COMP3003
QUERY_INSTRUCTOR John
QUERY_SEMESTER 2026S
QUERY_TIME Mon 09:00 12:00
```

Administrator operations:

```text
LOGIN admin 1234
ADD COMP4999|Special Topics in Networking|A|Henry Yu|Mon|16:00|18:00|E401|2026S
UPDATE COMP4999 A Classroom E402
DELETE COMP4999 A
```

## How This Project Satisfies the 6 Functional Requirements

### 1. Database Module

- Stores timetable data in `courses.csv`.
- Uses required fields: `CourseCode, CourseTitle, Section, Instructor, Day, StartTime, EndTime, Classroom, Semester`.
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
- Every modification is saved to `courses.csv` immediately, so later queries from other clients see the latest data.

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

Implemented in `client.cpp`. The client provides a menu for student queries and administrator operations. It also provides a raw protocol command option for demonstration.

### Bonus 3: wxWidgets GUI Client

Implemented in `wx_client.cpp`. The GUI client keeps the same line-based
protocol and provides:

- connection controls for server IP and port;
- query tabs for course code, instructor, semester, and time slot;
- administrator login, add, update, and delete controls;
- a result table plus raw server response view.

No server-side change is required because the GUI client sends the same
protocol commands as the console client.

## Demo Steps for Presentation

1. Start `server.exe` and show the listening port.
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
