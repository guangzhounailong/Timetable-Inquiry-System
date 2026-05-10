#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace Protocol {

constexpr int DEFAULT_PORT = 54000;
constexpr int MIN_CONCURRENT_CLIENTS = 5;
constexpr int MAX_LINE_LENGTH = 16384;

inline std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

inline std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

inline bool iequals(const std::string& left, const std::string& right) {
    return toUpper(left) == toUpper(right);
}

inline bool startsWithIgnoreCase(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    return iequals(value.substr(0, prefix.size()), prefix);
}

inline std::vector<std::string> splitWhitespace(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream input(line);
    std::string token;
    while (input >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

inline std::vector<std::string> splitByChar(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    std::istringstream input(text);
    while (std::getline(input, current, delimiter)) {
        parts.push_back(trim(current));
    }

    if (!text.empty() && text.back() == delimiter) {
        parts.push_back("");
    }

    return parts;
}

inline std::string removeCommand(const std::string& line) {
    const std::string trimmed = trim(line);
    const std::size_t firstSpace = trimmed.find_first_of(" \t");
    if (firstSpace == std::string::npos) {
        return "";
    }
    return trim(trimmed.substr(firstSpace + 1));
}

inline bool sendAll(SOCKET socketHandle, const std::string& data) {
    std::size_t totalSent = 0;
    while (totalSent < data.size()) {
        const int remaining = static_cast<int>(data.size() - totalSent);
        const int sent = send(socketHandle, data.c_str() + totalSent, remaining, 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            return false;
        }
        totalSent += static_cast<std::size_t>(sent);
    }
    return true;
}

inline bool sendLine(SOCKET socketHandle, const std::string& line) {
    return sendAll(socketHandle, line + "\n");
}

// The protocol is line based. A malformed long line is returned to the caller
// as text so the server can send an ERROR response instead of crashing.
inline bool recvLine(SOCKET socketHandle, std::string& line) {
    line.clear();
    char ch = '\0';

    while (true) {
        const int received = recv(socketHandle, &ch, 1, 0);
        if (received == SOCKET_ERROR || received == 0) {
            return !line.empty();
        }

        if (ch == '\n') {
            return true;
        }

        if (ch == '\r') {
            continue;
        }

        line.push_back(ch);
        if (line.size() > MAX_LINE_LENGTH) {
            line = "__ERROR_LINE_TOO_LONG__";
            return true;
        }
    }
}

inline std::string protocolHelp() {
    return
        "Commands:\n"
        "SECURE_V1 <32-hex nonce>  (optional; after greeting if server uses --secure <psk>)\n"
        "LOGIN admin 1234\n"
        "GET_COURSE CourseCode|Section|Semester\n"
        "QUERY_FILTER CourseCode|Instructor|Semester|Day|StartTime|EndTime\n"
        "QUERY_CODE COMP3003\n"
        "QUERY_INSTRUCTOR John Smith\n"
        "QUERY_SEMESTER 2026S\n"
        "QUERY_TIME Mon 09:00 12:00\n"
        "ADD CourseCode|CourseTitle|Section|Instructor|Day|StartTime|EndTime|Classroom|Semester\n"
        "UPDATE CourseCode Section Field NewValue\n"
        "UPDATE CourseCode Field NewValue\n"
        "DELETE CourseCode Section\n"
        "EXIT";
}

}  // namespace Protocol

#endif  // PROTOCOL_H
