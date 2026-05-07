#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "CourseDatabase.h"
#include "Protocol.h"

#include <atomic>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace {

CourseDatabase g_database("courses.csv");
std::mutex g_consoleMutex;
std::atomic<int> g_activeClients{0};

void logMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cout << message << std::endl;
}

std::string clientEndpoint(const sockaddr_in& address) {
    std::ostringstream output;
    output << inet_ntoa(address.sin_addr) << ':' << ntohs(address.sin_port);
    return output.str();
}

void sendSimple(SOCKET clientSocket, const std::string& response) {
    Protocol::sendLine(clientSocket, response);
}

void sendResult(SOCKET clientSocket, const std::vector<Course>& courses) {
    std::ostringstream response;
    response << "RESULT " << courses.size() << '\n'
             << g_database.formatCourses(courses)
             << "END\n";
    Protocol::sendAll(clientSocket, response.str());
}

void sendHelp(SOCKET clientSocket) {
    std::ostringstream response;
    response << "RESULT 1\n" << Protocol::protocolHelp() << "\nEND\n";
    Protocol::sendAll(clientSocket, response.str());
}

bool requireAdmin(SOCKET clientSocket, bool isAdmin) {
    if (!isAdmin) {
        sendSimple(clientSocket, "FAILURE Administrator login required.");
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

bool handleLogin(SOCKET clientSocket, const std::vector<std::string>& tokens, bool& isAdmin) {
    if (tokens.size() != 3) {
        sendSimple(clientSocket, "ERROR Usage: LOGIN username password.");
        return true;
    }

    if (tokens[1] == "admin" && tokens[2] == "1234") {
        isAdmin = true;
        sendSimple(clientSocket, "SUCCESS Administrator login accepted.");
    } else {
        sendSimple(clientSocket, "FAILURE Invalid administrator username or password.");
    }
    return true;
}

bool handleAdd(SOCKET clientSocket, const std::string& line, bool isAdmin) {
    if (!requireAdmin(clientSocket, isAdmin)) {
        return true;
    }

    const std::string payload = Protocol::removeCommand(line);
    Course course;
    std::string message;
    if (!CourseDatabase::parseCoursePayload(payload, course, message)) {
        sendSimple(clientSocket, message);
        return true;
    }

    g_database.addCourse(course, message);
    sendSimple(clientSocket, message);
    return true;
}

bool handleUpdate(SOCKET clientSocket, const std::string& line, bool isAdmin) {
    if (!requireAdmin(clientSocket, isAdmin)) {
        return true;
    }

    const std::string rest = Protocol::removeCommand(line);
    std::istringstream input(rest);
    std::string courseCode;
    std::string secondToken;

    if (!(input >> courseCode >> secondToken)) {
        sendSimple(clientSocket, "ERROR Usage: UPDATE CourseCode [Section] Field NewValue.");
        return true;
    }

    std::string message;

    if (CourseDatabase::isUpdatableField(secondToken)) {
        std::string newValue;
        std::getline(input, newValue);
        newValue = Protocol::trim(newValue);

        if (newValue.empty()) {
            sendSimple(clientSocket, "ERROR New value cannot be empty.");
            return true;
        }

        g_database.updateCourseByCodeIfUnique(courseCode, secondToken, newValue, message);
        sendSimple(clientSocket, message);
        return true;
    }

    std::string fieldName;
    if (!(input >> fieldName)) {
        sendSimple(clientSocket, "ERROR Usage: UPDATE CourseCode Section Field NewValue.");
        return true;
    }

    std::string newValue;
    std::getline(input, newValue);
    newValue = Protocol::trim(newValue);
    if (newValue.empty()) {
        sendSimple(clientSocket, "ERROR New value cannot be empty.");
        return true;
    }

    g_database.updateCourse(courseCode, secondToken, fieldName, newValue, message);
    sendSimple(clientSocket, message);
    return true;
}

bool handleDelete(SOCKET clientSocket, const std::vector<std::string>& tokens, bool isAdmin) {
    if (!requireAdmin(clientSocket, isAdmin)) {
        return true;
    }

    if (tokens.size() != 3) {
        sendSimple(clientSocket, "ERROR Usage: DELETE CourseCode Section.");
        return true;
    }

    std::string message;
    g_database.deleteCourse(tokens[1], tokens[2], message);
    sendSimple(clientSocket, message);
    return true;
}

bool handleClientCommand(SOCKET clientSocket, const std::string& rawLine, bool& isAdmin) {
    const std::string line = Protocol::trim(rawLine);
    if (line.empty()) {
        sendSimple(clientSocket, "ERROR Empty request.");
        return true;
    }

    if (line == "__ERROR_LINE_TOO_LONG__") {
        sendSimple(clientSocket, "ERROR Request line is too long.");
        return true;
    }

    const std::vector<std::string> tokens = Protocol::splitWhitespace(line);
    if (tokens.empty()) {
        sendSimple(clientSocket, "ERROR Empty request.");
        return true;
    }

    const std::string command = Protocol::toUpper(tokens[0]);

    if (command == "EXIT") {
        sendSimple(clientSocket, "OK Goodbye.");
        return false;
    }

    if (command == "HELP") {
        sendHelp(clientSocket);
        return true;
    }

    if (command == "LOGIN") {
        return handleLogin(clientSocket, tokens, isAdmin);
    }

    if (command == "QUERY_CODE") {
        if (tokens.size() != 2) {
            sendSimple(clientSocket, "ERROR Usage: QUERY_CODE CourseCode.");
            return true;
        }
        sendResult(clientSocket, g_database.queryByCode(tokens[1]));
        return true;
    }

    if (command == "QUERY_INSTRUCTOR") {
        const std::string instructor = Protocol::removeCommand(line);
        if (instructor.empty()) {
            sendSimple(clientSocket, "ERROR Usage: QUERY_INSTRUCTOR InstructorName.");
            return true;
        }
        sendResult(clientSocket, g_database.queryByInstructor(instructor));
        return true;
    }

    if (command == "QUERY_SEMESTER") {
        if (tokens.size() != 2) {
            sendSimple(clientSocket, "ERROR Usage: QUERY_SEMESTER Semester.");
            return true;
        }
        sendResult(clientSocket, g_database.queryBySemester(tokens[1]));
        return true;
    }

    if (command == "QUERY_TIME") {
        std::string day;
        std::string start;
        std::string end;
        std::string error;
        if (!parseTimeQuery(Protocol::removeCommand(line), day, start, end, error)) {
            sendSimple(clientSocket, error);
            return true;
        }
        sendResult(clientSocket, g_database.queryByTimeSlot(day, start, end));
        return true;
    }

    if (command == "ADD") {
        return handleAdd(clientSocket, line, isAdmin);
    }

    if (command == "UPDATE") {
        return handleUpdate(clientSocket, line, isAdmin);
    }

    if (command == "DELETE") {
        return handleDelete(clientSocket, tokens, isAdmin);
    }

    sendSimple(clientSocket, "ERROR Unknown command. Type HELP for command list.");
    return true;
}

void handleClient(SOCKET clientSocket, sockaddr_in clientAddress) {
    const std::string endpoint = clientEndpoint(clientAddress);
    ++g_activeClients;
    logMessage("[CONNECT] " + endpoint + " active=" + std::to_string(g_activeClients.load()));

    bool isAdmin = false;
    Protocol::sendLine(clientSocket, "OK Course Timetable Server Ready. Type HELP for commands.");

    std::string line;
    while (Protocol::recvLine(clientSocket, line)) {
        if (!handleClientCommand(clientSocket, line, isAdmin)) {
            break;
        }
    }

    closesocket(clientSocket);
    --g_activeClients;
    logMessage("[DISCONNECT] " + endpoint + " active=" + std::to_string(g_activeClients.load()));
}

}  // namespace

int main(int argc, char* argv[]) {
    int port = Protocol::DEFAULT_PORT;
    if (argc >= 2) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Invalid port number." << std::endl;
            return 1;
        }
        if (port <= 0 || port > 65535) {
            std::cerr << "Port must be between 1 and 65535." << std::endl;
            return 1;
        }
    }

    std::string databaseMessage;
    if (!g_database.load(databaseMessage)) {
        std::cerr << databaseMessage << std::endl;
        return 1;
    }
    std::cout << databaseMessage << std::endl;

    WSADATA wsaData;
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        std::cerr << "WSAStartup failed: " << startupResult << std::endl;
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "socket failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress = {};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(static_cast<u_short>(port));

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server listening on port " << port << std::endl;
    std::cout << "Minimum concurrent clients supported: "
              << Protocol::MIN_CONCURRENT_CLIENTS << std::endl;

    while (true) {
        sockaddr_in clientAddress = {};
        int clientAddressLength = sizeof(clientAddress);
        SOCKET clientSocket = accept(listenSocket,
                                     reinterpret_cast<sockaddr*>(&clientAddress),
                                     &clientAddressLength);

        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        std::thread clientThread(handleClient, clientSocket, clientAddress);
        clientThread.detach();
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
