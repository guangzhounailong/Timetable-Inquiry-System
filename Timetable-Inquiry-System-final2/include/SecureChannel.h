#ifndef SECURE_CHANNEL_H
#define SECURE_CHANNEL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// MSVC: bcrypt.lib   MinGW: link with -lbcrypt
#pragma comment(lib, "bcrypt.lib")

namespace SecureChannel {

constexpr std::size_t MAX_SECURE_FRAME = 512 * 1024;

bool recvExact(SOCKET socketHandle, void* buffer, std::size_t length);

bool sendSecureFrame(SOCKET socketHandle,
                     const std::vector<std::uint8_t>& key256,
                     const std::string& plaintext);

bool recvSecureFrame(SOCKET socketHandle,
                     const std::vector<std::uint8_t>& key256,
                     std::string& plaintext);

bool clientHandshake(SOCKET socketHandle,
                     const std::string& psk,
                     std::vector<std::uint8_t>& key256,
                     std::string& error);

bool serverHandshakeFromLine(SOCKET socketHandle,
                             const std::string& requestLine,
                             const std::string& pskConfigured,
                             std::vector<std::uint8_t>& key256,
                             std::string& error);

}  // namespace SecureChannel

#endif  // SECURE_CHANNEL_H
