# Presentation Outline: Course Timetable Inquiry System

## 1. Introduction

- Introduce the project topic: Course Timetable Inquiry System.
- Explain the real-world need: students need quick timetable access, and administrators need to maintain up-to-date course data.
- State that the system is implemented using C++ Windows Socket Programming.

## 2. Objectives

- Build a networked client-server application.
- Store and manage structured course timetable data.
- Support student timetable queries.
- Support administrator login and data modification.
- Support at least five concurrent clients.
- Handle invalid requests safely.

## 3. System Architecture

- Client sends requests and displays results.
- Server receives requests, processes business logic, accesses the database, and returns responses.
- CSV database stores timetable records.
- The demonstration runs on one Windows computer using `127.0.0.1`.

## 4. Database Design

- The database file is `courses.csv`.
- Fields:
  - CourseCode
  - CourseTitle
  - Section
  - Instructor
  - Day
  - StartTime
  - EndTime
  - Classroom
  - Semester
- The server loads the CSV file at startup.
- Add, update, and delete operations are saved immediately.

## 5. Communication Protocol

- The system uses a simple line-based application protocol.
- Example student requests:
  - `QUERY_CODE COMP3003`
  - `QUERY_INSTRUCTOR John`
  - `QUERY_SEMESTER 2026S`
  - `QUERY_TIME Mon 09:00 12:00`
- Example administrator requests:
  - `LOGIN admin 1234`
  - `ADD CourseCode|CourseTitle|Section|Instructor|Day|StartTime|EndTime|Classroom|Semester`
  - `UPDATE COMP3003 A Classroom B203`
  - `DELETE COMP3003 A`
- Response types:
  - `SUCCESS`
  - `FAILURE`
  - `RESULT`
  - `ERROR`
  - `OK`

## 6. Concurrency Design

- The server uses Winsock TCP sockets.
- The server calls `accept()` to receive new clients.
- Each client is handled by one detached `std::thread`.
- The database module uses `std::mutex` to protect records and CSV writes.
- This supports at least five concurrent client connections.

## 7. Student Functions

- Students do not need to log in.
- They can query by course code.
- They can query by instructor.
- They can view all courses in a semester.
- They can use advanced time slot search.

## 8. Administrator Functions

- Administrators log in with username `admin` and password `1234`.
- Administrators can add a new course record.
- Administrators can update course fields such as time or classroom.
- Administrators can delete outdated course records.
- Changes are immediately visible to other connected clients.

## 9. Bonus Features

- Advanced search by time slot with `QUERY_TIME`.
- Simple menu-based client interface.
- Optional future GUI extension using Win32 controls while keeping the same server protocol.

## 10. Demo Plan

- Start the server.
- Open one client and perform student queries.
- Open multiple clients to show concurrency.
- Log in as administrator.
- Add a course.
- Query from another client to show the new record.
- Update the course classroom or time.
- Query again to show immediate update.
- Delete the course.
- Query again to show removal.

## 11. Conclusion

- The system satisfies the required six modules.
- It uses C++ Winsock and a client-server architecture.
- It supports concurrent clients with `std::thread`.
- It stores data in CSV and protects updates with `std::mutex`.
- It is stable, clear, and suitable for a 10-minute demonstration.
