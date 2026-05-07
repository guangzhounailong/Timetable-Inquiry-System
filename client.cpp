#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "Protocol.h"

#include <iostream>
#include <sstream>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

namespace {

bool shouldPauseBeforeExit() {
    DWORD processIds[4] = {};
    const DWORD count = GetConsoleProcessList(processIds, 4);
    return count <= 1;
}

void pauseBeforeExitIfNeeded() {
    if (!shouldPauseBeforeExit()) {
        return;
    }

    std::cout << "\nPress Enter to exit...";
    std::string ignored;
    std::getline(std::cin, ignored);
}

std::string prompt(const std::string& label) {
    std::cout << label;
    std::string value;
    std::getline(std::cin, value);
    return Protocol::trim(value);
}

std::string promptRequired(const std::string& label) {
    while (true) {
        std::string value = prompt(label);
        if (!value.empty()) {
            return value;
        }
        std::cout << "Value cannot be empty." << std::endl;
    }
}

bool readResponse(SOCKET serverSocket, std::string& firstLine) {
    firstLine.clear();
    std::string line;
    if (!Protocol::recvLine(serverSocket, line)) {
        std::cout << "Server disconnected." << std::endl;
        return false;
    }

    firstLine = line;
    std::cout << line << std::endl;

    if (Protocol::startsWithIgnoreCase(line, "RESULT ")) {
        while (Protocol::recvLine(serverSocket, line)) {
            if (line == "END") {
                break;
            }
            std::cout << line << std::endl;
        }
    }

    return true;
}

bool sendCommand(SOCKET serverSocket, const std::string& command, std::string& firstLine) {
    if (!Protocol::sendLine(serverSocket, command)) {
        std::cout << "Failed to send request to server." << std::endl;
        return false;
    }
    return readResponse(serverSocket, firstLine);
}

void printMenu(bool isAdmin) {
    std::cout << "\n===== Course Timetable Inquiry System =====\n"
              << "1. Query by course code\n"
              << "2. Query by instructor\n"
              << "3. Query by semester\n"
              << "4. Advanced search by time slot\n"
              << "5. Administrator login\n";

    if (isAdmin) {
        std::cout << "6. Add course\n"
                  << "7. Update course\n"
                  << "8. Delete course\n";
    }

    std::cout << "9. Send raw protocol command\n"
              << "0. Exit\n"
              << "Choose: ";
}

bool queryByCode(SOCKET serverSocket) {
    const std::string code = promptRequired("Course code (example COMP3003): ");
    std::string response;
    return sendCommand(serverSocket, "QUERY_CODE " + code, response);
}

bool queryByInstructor(SOCKET serverSocket) {
    const std::string instructor = promptRequired("Instructor name or keyword: ");
    std::string response;
    return sendCommand(serverSocket, "QUERY_INSTRUCTOR " + instructor, response);
}

bool queryBySemester(SOCKET serverSocket) {
    const std::string semester = promptRequired("Semester (example 2026S): ");
    std::string response;
    return sendCommand(serverSocket, "QUERY_SEMESTER " + semester, response);
}

bool queryByTimeSlot(SOCKET serverSocket) {
    const std::string day = promptRequired("Day (example Mon): ");
    const std::string start = promptRequired("Start time HH:MM: ");
    const std::string end = promptRequired("End time HH:MM: ");
    std::string response;
    return sendCommand(serverSocket, "QUERY_TIME " + day + " " + start + " " + end, response);
}

bool adminLogin(SOCKET serverSocket, bool& isAdmin) {
    const std::string username = promptRequired("Username: ");
    const std::string password = promptRequired("Password: ");
    std::string response;
    if (!sendCommand(serverSocket, "LOGIN " + username + " " + password, response)) {
        return false;
    }

    if (Protocol::startsWithIgnoreCase(response, "SUCCESS")) {
        isAdmin = true;
    }
    return true;
}

bool addCourse(SOCKET serverSocket) {
    std::cout << "Enter new course information." << std::endl;
    const std::string code = promptRequired("CourseCode: ");
    const std::string title = promptRequired("CourseTitle: ");
    const std::string section = promptRequired("Section: ");
    const std::string instructor = promptRequired("Instructor: ");
    const std::string day = promptRequired("Day: ");
    const std::string start = promptRequired("StartTime HH:MM: ");
    const std::string end = promptRequired("EndTime HH:MM: ");
    const std::string classroom = promptRequired("Classroom: ");
    const std::string semester = promptRequired("Semester: ");

    const std::string command = "ADD " + code + "|" + title + "|" + section + "|" +
                                instructor + "|" + day + "|" + start + "|" + end + "|" +
                                classroom + "|" + semester;
    std::string response;
    return sendCommand(serverSocket, command, response);
}

bool updateCourse(SOCKET serverSocket) {
    const std::string code = promptRequired("CourseCode: ");
    const std::string section = promptRequired("Section: ");
    std::cout << "Fields: CourseTitle, Instructor, Day, StartTime, EndTime, Time, Classroom, Semester" << std::endl;
    const std::string field = promptRequired("Field: ");
    const std::string value = promptRequired("New value: ");

    const std::string command = "UPDATE " + code + " " + section + " " + field + " " + value;
    std::string response;
    return sendCommand(serverSocket, command, response);
}

bool deleteCourse(SOCKET serverSocket) {
    const std::string code = promptRequired("CourseCode: ");
    const std::string section = promptRequired("Section: ");
    std::string response;
    return sendCommand(serverSocket, "DELETE " + code + " " + section, response);
}

bool rawCommand(SOCKET serverSocket) {
    const std::string command = promptRequired("Protocol command: ");
    std::string response;
    return sendCommand(serverSocket, command, response);
}

SOCKET connectToServer(const std::string& host, int port) {
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cout << "socket failed: " << WSAGetLastError() << std::endl;
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddress = {};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(static_cast<u_short>(port));

    serverAddress.sin_addr.s_addr = inet_addr(host.c_str());
    if (serverAddress.sin_addr.s_addr == INADDR_NONE && host != "255.255.255.255") {
        std::cout << "Invalid IPv4 address: " << host << std::endl;
        closesocket(serverSocket);
        return INVALID_SOCKET;
    }

    if (connect(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cout << "connect failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        return INVALID_SOCKET;
    }

    return serverSocket;
}

}  // namespace

int main(int argc, char* argv[]) {
    int exitCode = 0;
    std::string host = "127.0.0.1";
    int port = Protocol::DEFAULT_PORT;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        try {
            port = std::stoi(argv[2]);
        } catch (...) {
            std::cout << "Invalid port number." << std::endl;
            pauseBeforeExitIfNeeded();
            return 1;
        }
        if (port <= 0 || port > 65535) {
            std::cout << "Port must be between 1 and 65535." << std::endl;
            pauseBeforeExitIfNeeded();
            return 1;
        }
    }

    WSADATA wsaData;
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        std::cout << "WSAStartup failed: " << startupResult << std::endl;
        pauseBeforeExitIfNeeded();
        return 1;
    }

    SOCKET serverSocket = connectToServer(host, port);
    if (serverSocket == INVALID_SOCKET) {
        WSACleanup();
        pauseBeforeExitIfNeeded();
        return 1;
    }

    std::string greeting;
    if (!readResponse(serverSocket, greeting)) {
        closesocket(serverSocket);
        WSACleanup();
        pauseBeforeExitIfNeeded();
        return 1;
    }

    bool isAdmin = false;
    bool running = true;
    while (running) {
        printMenu(isAdmin);

        std::string choice;
        std::getline(std::cin, choice);
        choice = Protocol::trim(choice);

        bool ok = true;
        if (choice == "1") {
            ok = queryByCode(serverSocket);
        } else if (choice == "2") {
            ok = queryByInstructor(serverSocket);
        } else if (choice == "3") {
            ok = queryBySemester(serverSocket);
        } else if (choice == "4") {
            ok = queryByTimeSlot(serverSocket);
        } else if (choice == "5") {
            ok = adminLogin(serverSocket, isAdmin);
        } else if (choice == "6" && isAdmin) {
            ok = addCourse(serverSocket);
        } else if (choice == "7" && isAdmin) {
            ok = updateCourse(serverSocket);
        } else if (choice == "8" && isAdmin) {
            ok = deleteCourse(serverSocket);
        } else if (choice == "9") {
            ok = rawCommand(serverSocket);
        } else if (choice == "0") {
            std::string response;
            ok = sendCommand(serverSocket, "EXIT", response);
            running = false;
        } else {
            std::cout << "Invalid menu choice." << std::endl;
        }

        if (!ok) {
            running = false;
        }
    }

    closesocket(serverSocket);
    WSACleanup();
    pauseBeforeExitIfNeeded();
    return exitCode;
}
