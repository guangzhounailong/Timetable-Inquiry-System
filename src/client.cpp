#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "Protocol.h"
#include "SecureChannel.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace {

constexpr WORD DEFAULT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
constexpr WORD RED_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY;
constexpr WORD YELLOW_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;

bool g_actionOutputYellow = false;

struct Session {
    SOCKET socket = INVALID_SOCKET;
    bool secure = false;
    std::vector<std::uint8_t> aesKey;
};

HANDLE consoleOutput() {
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

void setConsoleColor(WORD attributes) {
    SetConsoleTextAttribute(consoleOutput(), attributes);
}

void resetConsoleColor() {
    setConsoleColor(DEFAULT_COLOR);
}

void beginActionOutput() {
    g_actionOutputYellow = true;
    setConsoleColor(YELLOW_COLOR);
}

void endActionOutput() {
    g_actionOutputYellow = false;
    resetConsoleColor();
}

void printColor(const std::string& text, WORD color) {
    setConsoleColor(color);
    std::cout << text;
    resetConsoleColor();
}

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

std::string prompt(const std::string& label, bool highlight = false) {
    if (highlight || g_actionOutputYellow) {
        setConsoleColor(YELLOW_COLOR);
        std::cout << label;
        if (!g_actionOutputYellow) {
            resetConsoleColor();
        }
    } else {
        std::cout << label;
    }
    std::string value;
    std::getline(std::cin, value);
    return Protocol::trim(value);
}

std::string promptRequired(const std::string& label, bool highlight = false) {
    while (true) {
        std::string value = prompt(label, highlight);
        if (!value.empty()) {
            return value;
        }
        if (g_actionOutputYellow) {
            setConsoleColor(YELLOW_COLOR);
        }
        std::cout << "Value cannot be empty." << std::endl;
    }
}

void printStartupTitle() {
    std::cout << u8"██████╗  ██████╗███╗   ██╗\n"
              << u8"██╔══██╗██╔════╝████╗  ██║\n"
              << u8"██║  ██║██║     ██╔██╗ ██║\n"
              << u8"██║  ██║██║     ██║╚██╗██║\n"
              << u8"██████╔╝╚██████╗██║ ╚████║\n"
              << u8"╚═════╝  ╚═════╝╚═╝  ╚═══╝\n\n"
              << "Course Timetable Inquiry System\n\n";
}

void printStartupGreeting(const std::string& greeting) {
    const std::string readyPrefix = "OK Course Timetable Server Ready.";
    if (Protocol::startsWithIgnoreCase(greeting, readyPrefix)) {
        std::string helpText = Protocol::trim(greeting.substr(readyPrefix.size()));
        if (helpText.empty()) {
            helpText = "Type HELP for commands.";
        }
        std::cout << helpText << std::endl;
        return;
    }

    std::cout << greeting << std::endl;
}

bool readStartupGreeting(Session& session, std::string& greeting) {
    greeting.clear();
    if (!Protocol::recvLine(session.socket, greeting)) {
        std::cout << "Server disconnected." << std::endl;
        return false;
    }

    printStartupTitle();
    printStartupGreeting(greeting);
    return true;
}

bool readResponse(Session& session, std::string& firstLine) {
    firstLine.clear();

    if (session.secure) {
        std::string block;
        if (!SecureChannel::recvSecureFrame(session.socket, session.aesKey, block)) {
            if (g_actionOutputYellow) {
                setConsoleColor(YELLOW_COLOR);
            }
            std::cout << "Server disconnected." << std::endl;
            return false;
        }
        std::istringstream input(block);
        std::getline(input, firstLine);
        if (!firstLine.empty() && firstLine.back() == '\r') {
            firstLine.pop_back();
        }
        const bool isResult = Protocol::startsWithIgnoreCase(firstLine, "RESULT");
        if (isResult || g_actionOutputYellow) {
            setConsoleColor(YELLOW_COLOR);
        }
        std::cout << firstLine << std::endl;

        if (isResult) {
            std::string line;
            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line == "END") {
                    break;
                }
                std::cout << line << std::endl;
            }
        }
        if (!g_actionOutputYellow) {
            resetConsoleColor();
        }
        return true;
    }

    std::string line;
    if (!Protocol::recvLine(session.socket, line)) {
        if (g_actionOutputYellow) {
            setConsoleColor(YELLOW_COLOR);
        }
        std::cout << "Server disconnected." << std::endl;
        return false;
    }

    firstLine = line;
    const bool isResult = Protocol::startsWithIgnoreCase(line, "RESULT");
    if (isResult || g_actionOutputYellow) {
        setConsoleColor(YELLOW_COLOR);
    }

    std::cout << line << std::endl;

    if (isResult) {
        while (Protocol::recvLine(session.socket, line)) {
            if (line == "END") {
                break;
            }
            std::cout << line << std::endl;
        }
    }

    if (!g_actionOutputYellow) {
        resetConsoleColor();
    }

    return true;
}

bool sendCommand(Session& session, const std::string& command, std::string& firstLine) {
    if (session.secure) {
        if (!SecureChannel::sendSecureFrame(session.socket, session.aesKey, command + "\n")) {
            if (g_actionOutputYellow) {
                setConsoleColor(YELLOW_COLOR);
            }
            std::cout << "Failed to send request to server." << std::endl;
            return false;
        }
    } else {
        if (!Protocol::sendLine(session.socket, command)) {
            if (g_actionOutputYellow) {
                setConsoleColor(YELLOW_COLOR);
            }
            std::cout << "Failed to send request to server." << std::endl;
            return false;
        }
    }
    return readResponse(session, firstLine);
}

bool hasNonEmptyResult(const std::string& firstLine) {
    if (!Protocol::startsWithIgnoreCase(firstLine, "RESULT")) {
        return false;
    }

    std::istringstream input(firstLine);
    std::string marker;
    int count = 0;
    if (!(input >> marker >> count)) {
        return false;
    }
    return count > 0;
}

void printQueryTime(double elapsedMs) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3)
           << "Query time: " << elapsedMs << " ms";
    std::cout << output.str() << std::endl;
}

bool sendTimedQuery(Session& session, const std::string& command) {
    std::string response;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = sendCommand(session, command, response);
    const auto end = std::chrono::steady_clock::now();

    if (ok && hasNonEmptyResult(response)) {
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(end - start).count();
        printQueryTime(elapsedMs);
    }

    return ok;
}

void printMenu(bool isAdmin) {
    resetConsoleColor();
    std::cout << "\n===== ";
    printColor("Inquiry Options", RED_COLOR);
    std::cout << " =====\n"
              << "1. Query by course code\n"
              << "2. Query by instructor\n"
              << "3. Query by semester\n"
              << "4. Query by time slot\n"
              << "5. Administrator login\n";

    if (isAdmin) {
        std::cout << "6. Add course\n"
                  << "7. Update course\n"
                  << "8. Delete course\n";
    }

    std::cout << "9. Send raw protocol command\n"
              << "0. Exit\n"
              << "===========================\n";
    printColor("Choose: ", YELLOW_COLOR);
}

bool queryByCode(Session& session) {
    const std::string code = promptRequired("Course code (example COMP3003): ", true);
    return sendTimedQuery(session, "QUERY_CODE " + code);
}

bool queryByInstructor(Session& session) {
    const std::string instructor = promptRequired("Instructor name or keyword: ", true);
    return sendTimedQuery(session, "QUERY_INSTRUCTOR " + instructor);
}

bool queryBySemester(Session& session) {
    const std::string semester = promptRequired("Semester (example 2026S): ", true);
    return sendTimedQuery(session, "QUERY_SEMESTER " + semester);
}

bool queryByTimeSlot(Session& session) {
    const std::string day = promptRequired("Day (example Mon): ", true);
    const std::string start = promptRequired("Start time HH:MM: ", true);
    const std::string end = promptRequired("End time HH:MM: ", true);
    return sendTimedQuery(session, "QUERY_TIME " + day + " " + start + " " + end);
}

bool adminLogin(Session& session, bool& isAdmin) {
    const std::string username = promptRequired("Username: ");
    const std::string password = promptRequired("Password: ");
    std::string response;
    if (!sendCommand(session, "LOGIN " + username + " " + password, response)) {
        return false;
    }

    if (Protocol::startsWithIgnoreCase(response, "SUCCESS")) {
        isAdmin = true;
    }
    return true;
}

bool addCourse(Session& session) {
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
    return sendCommand(session, command, response);
}

bool updateCourse(Session& session) {
    const std::string code = promptRequired("CourseCode: ");
    const std::string section = promptRequired("Section: ");
    std::cout << "Fields: CourseTitle, Instructor, Day, StartTime, EndTime, Time, Classroom, Semester" << std::endl;
    const std::string field = promptRequired("Field: ");
    const std::string value = promptRequired("New value: ");

    const std::string command = "UPDATE " + code + " " + section + " " + field + " " + value;
    std::string response;
    return sendCommand(session, command, response);
}

bool deleteCourse(Session& session) {
    const std::string code = promptRequired("CourseCode: ");
    const std::string section = promptRequired("Section: ");
    std::string response;
    return sendCommand(session, "DELETE " + code + " " + section, response);
}

bool rawCommand(Session& session) {
    const std::string command = promptRequired("Protocol command: ");
    std::string response;
    return sendCommand(session, command, response);
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
    SetConsoleOutputCP(CP_UTF8);

    int exitCode = 0;
    std::string host = "127.0.0.1";
    int port = Protocol::DEFAULT_PORT;
    std::string securePsk;

    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--secure") {
            if (i + 1 >= argc) {
                std::cout << "Missing shared secret after --secure." << std::endl;
                pauseBeforeExitIfNeeded();
                return 1;
            }
            securePsk = argv[++i];
            continue;
        }
        positional.push_back(arg);
    }

    if (positional.size() >= 1) {
        host = positional[0];
    }
    if (positional.size() >= 2) {
        try {
            port = std::stoi(positional[1]);
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

    Session session;
    session.socket = connectToServer(host, port);
    if (session.socket == INVALID_SOCKET) {
        WSACleanup();
        pauseBeforeExitIfNeeded();
        return 1;
    }

    std::string greeting;
    if (!readStartupGreeting(session, greeting)) {
        closesocket(session.socket);
        WSACleanup();
        pauseBeforeExitIfNeeded();
        return 1;
    }

    if (!securePsk.empty()) {
        std::string error;
        if (!SecureChannel::clientHandshake(session.socket, securePsk, session.aesKey, error)) {
            std::cout << error << std::endl;
            closesocket(session.socket);
            WSACleanup();
            pauseBeforeExitIfNeeded();
            return 1;
        }
        session.secure = true;
        std::cout << "Encrypted session active (AES-256-GCM)." << std::endl;
    }

    bool isAdmin = false;
    bool running = true;
    while (running) {
        printMenu(isAdmin);

        std::string choice;
        std::getline(std::cin, choice);
        choice = Protocol::trim(choice);

        beginActionOutput();
        bool ok = true;
        if (choice == "1") {
            ok = queryByCode(session);
        } else if (choice == "2") {
            ok = queryByInstructor(session);
        } else if (choice == "3") {
            ok = queryBySemester(session);
        } else if (choice == "4") {
            ok = queryByTimeSlot(session);
        } else if (choice == "5") {
            ok = adminLogin(session, isAdmin);
        } else if (choice == "6" && isAdmin) {
            ok = addCourse(session);
        } else if (choice == "7" && isAdmin) {
            ok = updateCourse(session);
        } else if (choice == "8" && isAdmin) {
            ok = deleteCourse(session);
        } else if (choice == "9") {
            ok = rawCommand(session);
        } else if (choice == "0") {
            std::string response;
            ok = sendCommand(session, "EXIT", response);
            running = false;
        } else {
            std::cout << "Invalid menu choice." << std::endl;
        }

        endActionOutput();
        if (!ok) {
            running = false;
        }
    }

    closesocket(session.socket);
    WSACleanup();
    pauseBeforeExitIfNeeded();
    return exitCode;
}
