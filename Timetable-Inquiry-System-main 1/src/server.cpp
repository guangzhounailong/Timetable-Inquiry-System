#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "CourseDatabase.h"
#include "Protocol.h"
#include "SecureChannel.h"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace {

std::string databasePath() {
    const char* candidates[] = {"data/courses.csv", "../data/courses.csv"};
    for (const char* candidate : candidates) {
        std::ifstream input(candidate);
        if (input.good()) {
            return candidate;
        }
    }
    return "data/courses.csv";
}

CourseDatabase g_database(databasePath());
std::mutex g_consoleMutex;
std::atomic<int> g_activeClients{0};
std::string g_securePsk;

constexpr WORD DEFAULT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
constexpr WORD TITLE_COLOR = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
constexpr WORD OK_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD WARN_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD ERROR_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY;
constexpr WORD INFO_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;

struct ClientConn {
    SOCKET socket = INVALID_SOCKET;
    bool secure = false;
    std::vector<std::uint8_t> aesKey;
    std::string endpoint;

    bool sendLine(const std::string& line) const {
        if (secure) {
            return SecureChannel::sendSecureFrame(socket, aesKey, line + "\n");
        }
        return Protocol::sendLine(socket, line);
    }

    bool sendBlob(const std::string& blob) const {
        if (secure) {
            return SecureChannel::sendSecureFrame(socket, aesKey, blob);
        }
        return Protocol::sendAll(socket, blob);
    }
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

void printColorLine(const std::string& message, WORD color) {
    setConsoleColor(color);
    std::cout << message << std::endl;
    resetConsoleColor();
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

WORD logColor(const std::string& message) {
    if (startsWith(message, "[CONNECT]") || startsWith(message, "[RESPONSE]")) {
        return OK_COLOR;
    }
    if (startsWith(message, "[DISCONNECT]")) {
        return WARN_COLOR;
    }
    if (startsWith(message, "[SECURE]") || startsWith(message, "[REQUEST]") ||
        startsWith(message, "[ADMIN]")) {
        return TITLE_COLOR;
    }
    if (startsWith(message, "[ERROR]")) {
        return ERROR_COLOR;
    }
    return INFO_COLOR;
}

void printServerTitle() {
    printColorLine("==========================================", TITLE_COLOR);
    printColorLine(" Course Timetable Server", TITLE_COLOR);
    printColorLine("==========================================", TITLE_COLOR);
}

void printStartupStatus(const std::string& label, const std::string& detail) {
    setConsoleColor(OK_COLOR);
    std::cout << label;
    resetConsoleColor();
    if (!detail.empty()) {
        std::cout << "  " << detail;
    }
    std::cout << std::endl;
}

void logMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    printColorLine(message, logColor(message));
}

std::string clientEndpoint(const sockaddr_in& address) {
    std::ostringstream output;
    output << inet_ntoa(address.sin_addr) << ':' << ntohs(address.sin_port);
    return output.str();
}

void sendSimple(ClientConn& conn, const std::string& response) {
    logMessage("[RESPONSE] " + conn.endpoint + " " + response);
    conn.sendLine(response);
}

void sendResult(ClientConn& conn, const std::vector<Course>& courses) {
    std::ostringstream response;
    response << "RESULT " << courses.size() << '\n'
             << g_database.formatCourses(courses)
             << "END\n";
    logMessage("[RESPONSE] " + conn.endpoint + " RESULT " + std::to_string(courses.size()));
    conn.sendBlob(response.str());
}

void sendHelp(ClientConn& conn) {
    std::ostringstream response;
    response << "RESULT 1\n" << Protocol::protocolHelp() << "\nEND\n";
    logMessage("[RESPONSE] " + conn.endpoint + " RESULT 1");
    conn.sendBlob(response.str());
}

std::string coursePayload(const Course& course) {
    return course.courseCode + "|" +
           course.courseTitle + "|" +
           course.section + "|" +
           course.instructor + "|" +
           course.day + "|" +
           course.startTime + "|" +
           course.endTime + "|" +
           course.classroom + "|" +
           course.semester;
}

bool requireAdmin(ClientConn& conn, bool isAdmin) {
    if (!isAdmin) {
        sendSimple(conn, "FAILURE Administrator login required.");
        return false;
    }
    return true;
}

bool parseTimeQuery(const std::string& rest,
                    std::string& day,
                    std::string& start,
                    std::string& end,
                    std::string& error) {
    std::istringstream input(rest);
    input >> day >> start >> end;

    if (day.empty() || start.empty()) {
        error = "ERROR Usage: QUERY_TIME Day StartTime EndTime.";
        return false;
    }

    if (end.empty()) {
        const std::size_t dash = start.find('-');
        if (dash == std::string::npos) {
            error = "ERROR Usage: QUERY_TIME Day StartTime EndTime.";
            return false;
        }
        end = start.substr(dash + 1);
        start = start.substr(0, dash);
    }

    start = Protocol::trim(start);
    end = Protocol::trim(end);

    if (!CourseDatabase::isValidTime(start) || !CourseDatabase::isValidTime(end)) {
        error = "ERROR Time must use HH:MM format.";
        return false;
    }

    return true;
}

bool handleLogin(ClientConn& conn, const std::vector<std::string>& tokens, bool& isAdmin) {
    if (tokens.size() != 3) {
        sendSimple(conn, "ERROR Usage: LOGIN username password.");
        return true;
    }

    if (tokens[1] == "admin" && tokens[2] == "1234") {
        isAdmin = true;
        logMessage("[ADMIN] " + conn.endpoint + " login accepted");
        sendSimple(conn, "SUCCESS Administrator login accepted.");
    } else {
        logMessage("[ADMIN] " + conn.endpoint + " login rejected");
        sendSimple(conn, "FAILURE Invalid administrator username or password.");
    }
    return true;
}

bool handleAdd(ClientConn& conn, const std::string& line, bool isAdmin) {
    if (!requireAdmin(conn, isAdmin)) {
        return true;
    }

    const std::string payload = Protocol::removeCommand(line);
    Course course;
    std::string message;
    if (!CourseDatabase::parseCoursePayload(payload, course, message)) {
        sendSimple(conn, message);
        return true;
    }

    g_database.addCourse(course, message);
    logMessage("[ADMIN] " + conn.endpoint + " ADD " + message);
    sendSimple(conn, message);
    return true;
}

bool handleUpdate(ClientConn& conn, const std::string& line, bool isAdmin) {
    if (!requireAdmin(conn, isAdmin)) {
        return true;
    }

    const std::string rest = Protocol::removeCommand(line);
    std::istringstream input(rest);
    std::string courseCode;
    std::string secondToken;

    if (!(input >> courseCode >> secondToken)) {
        sendSimple(conn, "ERROR Usage: UPDATE CourseCode [Section] Field NewValue.");
        return true;
    }

    std::string message;

    if (CourseDatabase::isUpdatableField(secondToken)) {
        std::string newValue;
        std::getline(input, newValue);
        newValue = Protocol::trim(newValue);

        if (newValue.empty()) {
            sendSimple(conn, "ERROR New value cannot be empty.");
            return true;
        }

        g_database.updateCourseByCodeIfUnique(courseCode, secondToken, newValue, message);
        logMessage("[ADMIN] " + conn.endpoint + " UPDATE " + message);
        sendSimple(conn, message);
        return true;
    }

    std::string fieldName;
    if (!(input >> fieldName)) {
        sendSimple(conn, "ERROR Usage: UPDATE CourseCode Section Field NewValue.");
        return true;
    }

    std::string newValue;
    std::getline(input, newValue);
    newValue = Protocol::trim(newValue);
    if (newValue.empty()) {
        sendSimple(conn, "ERROR New value cannot be empty.");
        return true;
    }

    g_database.updateCourse(courseCode, secondToken, fieldName, newValue, message);
    logMessage("[ADMIN] " + conn.endpoint + " UPDATE " + message);
    sendSimple(conn, message);
    return true;
}

bool handleDelete(ClientConn& conn, const std::vector<std::string>& tokens, bool isAdmin) {
    if (!requireAdmin(conn, isAdmin)) {
        return true;
    }

    if (tokens.size() != 3) {
        sendSimple(conn, "ERROR Usage: DELETE CourseCode Section.");
        return true;
    }

    std::string message;
    g_database.deleteCourse(tokens[1], tokens[2], message);
    logMessage("[ADMIN] " + conn.endpoint + " DELETE " + message);
    sendSimple(conn, message);
    return true;
}

bool handleQueryFilter(ClientConn& conn, const std::string& line) {
    std::vector<std::string> parts = Protocol::splitByChar(Protocol::removeCommand(line), '|');
    if (parts.size() > 6) {
        sendSimple(conn, "ERROR Usage: QUERY_FILTER CourseCode|Instructor|Semester|Day|StartTime|EndTime.");
        return true;
    }

    while (parts.size() < 6) {
        parts.push_back("");
    }

    const std::string start = Protocol::trim(parts[4]);
    const std::string end = Protocol::trim(parts[5]);
    if ((!start.empty() && !CourseDatabase::isValidTime(start)) ||
        (!end.empty() && !CourseDatabase::isValidTime(end))) {
        sendSimple(conn, "ERROR Time must use HH:MM format.");
        return true;
    }

    if (!start.empty() && !end.empty() &&
        std::stoi(start.substr(0, 2)) * 60 + std::stoi(start.substr(3, 2)) >=
            std::stoi(end.substr(0, 2)) * 60 + std::stoi(end.substr(3, 2))) {
        sendSimple(conn, "ERROR StartTime must be earlier than EndTime.");
        return true;
    }

    sendResult(conn,
               g_database.queryByFilters(parts[0], parts[1], parts[2], parts[3], start, end));
    return true;
}

bool handleGetCourse(ClientConn& conn, const std::string& line) {
    std::vector<std::string> parts = Protocol::splitByChar(Protocol::removeCommand(line), '|');
    if (parts.size() < 2 || parts.size() > 3) {
        sendSimple(conn, "ERROR Usage: GET_COURSE CourseCode|Section|Semester.");
        return true;
    }

    while (parts.size() < 3) {
        parts.push_back("");
    }

    Course course;
    if (!g_database.findCourse(parts[0], parts[1], parts[2], course)) {
        sendSimple(conn, "ERROR Course not found.");
        return true;
    }

    std::ostringstream response;
    response << "RESULT 1\n" << coursePayload(course) << "\nEND\n";
    logMessage("[RESPONSE] " + conn.endpoint + " RESULT 1");
    conn.sendBlob(response.str());
    return true;
}

bool handleClientCommand(ClientConn& conn, const std::string& rawLine, bool& isAdmin) {
    const std::string line = Protocol::trim(rawLine);
    if (line.empty()) {
        sendSimple(conn, "ERROR Empty request.");
        return true;
    }

    if (line == "__ERROR_LINE_TOO_LONG__") {
        sendSimple(conn, "ERROR Request line is too long.");
        return true;
    }

    const std::vector<std::string> tokens = Protocol::splitWhitespace(line);
    if (tokens.empty()) {
        sendSimple(conn, "ERROR Empty request.");
        return true;
    }

    const std::string command = Protocol::toUpper(tokens[0]);
    logMessage("[REQUEST] " + conn.endpoint + " " + command);

    if (command == "EXIT") {
        sendSimple(conn, "OK Goodbye.");
        return false;
    }

    if (command == "HELP") {
        sendHelp(conn);
        return true;
    }

    if (command == "LOGIN") {
        return handleLogin(conn, tokens, isAdmin);
    }

    if (command == "GET_COURSE") {
        return handleGetCourse(conn, line);
    }

    if (command == "QUERY_FILTER") {
        return handleQueryFilter(conn, line);
    }

    if (command == "QUERY_CODE") {
        if (tokens.size() != 2) {
            sendSimple(conn, "ERROR Usage: QUERY_CODE CourseCode.");
            return true;
        }
        sendResult(conn, g_database.queryByCode(tokens[1]));
        return true;
    }

    if (command == "QUERY_INSTRUCTOR") {
        const std::string instructor = Protocol::removeCommand(line);
        if (instructor.empty()) {
            sendSimple(conn, "ERROR Usage: QUERY_INSTRUCTOR InstructorName.");
            return true;
        }
        sendResult(conn, g_database.queryByInstructor(instructor));
        return true;
    }

    if (command == "QUERY_SEMESTER") {
        if (tokens.size() != 2) {
            sendSimple(conn, "ERROR Usage: QUERY_SEMESTER Semester.");
            return true;
        }
        sendResult(conn, g_database.queryBySemester(tokens[1]));
        return true;
    }

    if (command == "QUERY_TIME") {
        std::string day;
        std::string start;
        std::string end;
        std::string error;
        if (!parseTimeQuery(Protocol::removeCommand(line), day, start, end, error)) {
            sendSimple(conn, error);
            return true;
        }
        sendResult(conn, g_database.queryByTimeSlot(day, start, end));
        return true;
    }

    if (command == "ADD") {
        return handleAdd(conn, line, isAdmin);
    }

    if (command == "UPDATE") {
        return handleUpdate(conn, line, isAdmin);
    }

    if (command == "DELETE") {
        return handleDelete(conn, tokens, isAdmin);
    }

    sendSimple(conn, "ERROR Unknown command. Type HELP for command list.");
    return true;
}

void handleClient(SOCKET clientSocket, sockaddr_in clientAddress) {
    const std::string endpoint = clientEndpoint(clientAddress);
    ++g_activeClients;
    logMessage("[CONNECT] " + endpoint + " active=" + std::to_string(g_activeClients.load()));

    ClientConn conn;
    conn.socket = clientSocket;
    conn.endpoint = endpoint;

    bool isAdmin = false;
    Protocol::sendLine(clientSocket, "OK Course Timetable Server Ready. Type HELP for commands.");

    std::string raw;
    while (true) {
        if (conn.secure) {
            if (!SecureChannel::recvSecureFrame(conn.socket, conn.aesKey, raw)) {
                break;
            }
        } else {
            if (!Protocol::recvLine(conn.socket, raw)) {
                break;
            }
        }

        const std::string trimmed = Protocol::trim(raw);
        if (!conn.secure && Protocol::startsWithIgnoreCase(trimmed, "SECURE_V1")) {
            std::string err;
            if (SecureChannel::serverHandshakeFromLine(conn.socket, trimmed, g_securePsk, conn.aesKey, err)) {
                conn.secure = true;
                logMessage("[SECURE] " + endpoint + " AES-GCM session established.");
                continue;
            }
            Protocol::sendLine(conn.socket, err);
            continue;
        }

        if (!handleClientCommand(conn, raw, isAdmin)) {
            break;
        }
    }

    closesocket(clientSocket);
    --g_activeClients;
    logMessage("[DISCONNECT] " + endpoint + " active=" + std::to_string(g_activeClients.load()));
}

}  // namespace

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    int port = Protocol::DEFAULT_PORT;
    g_securePsk.clear();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--secure") {
            if (i + 1 >= argc) {
                printColorLine("Missing shared secret after --secure.", ERROR_COLOR);
                return 1;
            }
            g_securePsk = argv[++i];
            continue;
        }

        bool digitsOnly = !arg.empty();
        for (char ch : arg) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                digitsOnly = false;
                break;
            }
        }
        if (digitsOnly) {
            try {
                const int parsed = std::stoi(arg);
                if (parsed > 0 && parsed <= 65535) {
                    port = parsed;
                }
            } catch (...) {
                printColorLine("Invalid port number.", ERROR_COLOR);
                return 1;
            }
        }
    }

    if (port <= 0 || port > 65535) {
        printColorLine("Port must be between 1 and 65535.", ERROR_COLOR);
        return 1;
    }

    printServerTitle();

    std::string databaseMessage;
    if (!g_database.load(databaseMessage)) {
        printColorLine(databaseMessage, ERROR_COLOR);
        return 1;
    }
    printStartupStatus("Database", databaseMessage);

    WSADATA wsaData;
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        printColorLine("WSAStartup failed: " + std::to_string(startupResult), ERROR_COLOR);
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        printColorLine("socket failed: " + std::to_string(WSAGetLastError()), ERROR_COLOR);
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress = {};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(static_cast<u_short>(port));

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        printColorLine("bind failed: " + std::to_string(WSAGetLastError()), ERROR_COLOR);
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printColorLine("listen failed: " + std::to_string(WSAGetLastError()), ERROR_COLOR);
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    printStartupStatus("Server", "listening on port " + std::to_string(port));
    printStartupStatus("Concurrency",
                       "minimum clients supported: " +
                           std::to_string(Protocol::MIN_CONCURRENT_CLIENTS));
    if (!g_securePsk.empty()) {
        printStartupStatus("Secure sessions",
                           "enabled (AES-256-GCM + PSK). Clients send SECURE_V1 after the greeting.");
    }

    while (true) {
        sockaddr_in clientAddress = {};
        int clientAddressLength = sizeof(clientAddress);
        SOCKET clientSocket = accept(listenSocket,
                                     reinterpret_cast<sockaddr*>(&clientAddress),
                                     &clientAddressLength);

        if (clientSocket == INVALID_SOCKET) {
            printColorLine("accept failed: " + std::to_string(WSAGetLastError()), ERROR_COLOR);
            continue;
        }

        std::thread clientThread(handleClient, clientSocket, clientAddress);
        clientThread.detach();
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
