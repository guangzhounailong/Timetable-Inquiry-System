#include "SecureChannel.h"

#include "Protocol.h"

#include <bcrypt.h>

#include <cctype>
#include <cstring>
#include <cwchar>
#include <sstream>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace SecureChannel {
namespace {

constexpr ULONG kGcmIvBytes = 12;
constexpr ULONG kGcmTagBytes = 16;
constexpr ULONG kAesKeyBytes = 32;

const char kHexDigits[] = "0123456789abcdef";

bool setAesGcmChainingMode(BCRYPT_ALG_HANDLE hAlg) {
    const wchar_t* mode = BCRYPT_CHAIN_MODE_GCM;
    const ULONG modeBytes = static_cast<ULONG>((std::wcslen(mode) + 1U) * sizeof(wchar_t));
    return NT_SUCCESS(BCryptSetProperty(hAlg,
                                        BCRYPT_CHAINING_MODE,
                                        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(mode)),
                                        modeBytes,
                                        0));
}

std::string hexEncode(const std::uint8_t* data, std::size_t length) {
    std::string out;
    out.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(kHexDigits[(data[i] >> 4) & 0x0F]);
        out.push_back(kHexDigits[data[i] & 0x0F]);
    }
    return out;
}

int hexValue(unsigned char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool hexDecode16Bytes(const std::string& hex, std::uint8_t out[16]) {
    if (hex.size() != 32) {
        return false;
    }
    for (std::size_t i = 0; i < 16; ++i) {
        const int high = hexValue(static_cast<unsigned char>(hex[i * 2]));
        const int low = hexValue(static_cast<unsigned char>(hex[i * 2 + 1]));
        if (high < 0 || low < 0) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

bool randomBytes(std::uint8_t* buffer, std::size_t length) {
    return BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(length), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

bool sha256PskNonces(const std::string& psk,
                     const std::uint8_t clientNonce[16],
                     const std::uint8_t serverNonce[16],
                     std::vector<std::uint8_t>& key256) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return false;
    }

    DWORD objectLength = 0;
    DWORD cbData = 0;
    if (!NT_SUCCESS(BCryptGetProperty(hAlg,
                                      BCRYPT_OBJECT_LENGTH,
                                      reinterpret_cast<PUCHAR>(&objectLength),
                                      sizeof(objectLength),
                                      &cbData,
                                      0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    std::vector<UCHAR> hashObject(objectLength);
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (!NT_SUCCESS(BCryptCreateHash(hAlg,
                                     &hHash,
                                     hashObject.data(),
                                     static_cast<ULONG>(hashObject.size()),
                                     nullptr,
                                     0,
                                     0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    bool ok = NT_SUCCESS(BCryptHashData(hHash,
                                        reinterpret_cast<PUCHAR>(const_cast<char*>(psk.data())),
                                        static_cast<ULONG>(psk.size()),
                                        0)) &&
              NT_SUCCESS(BCryptHashData(hHash, const_cast<PUCHAR>(clientNonce), 16, 0)) &&
              NT_SUCCESS(BCryptHashData(hHash, const_cast<PUCHAR>(serverNonce), 16, 0));

    key256.resize(32);
    if (ok) {
        ok = NT_SUCCESS(
            BCryptFinishHash(hHash, key256.data(), static_cast<ULONG>(key256.size()), 0));
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

bool aesGcmEncrypt(const std::vector<std::uint8_t>& key256,
                   const std::uint8_t iv[kGcmIvBytes],
                   const std::string& plaintext,
                   std::vector<std::uint8_t>& ciphertext,
                   std::uint8_t tag[kGcmTagBytes]) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        return false;
    }

    if (!setAesGcmChainingMode(hAlg)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    DWORD keyObjectLength = 0;
    DWORD cbData = 0;
    if (!NT_SUCCESS(BCryptGetProperty(hAlg,
                                      BCRYPT_OBJECT_LENGTH,
                                      reinterpret_cast<PUCHAR>(&keyObjectLength),
                                      sizeof(keyObjectLength),
                                      &cbData,
                                      0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    std::vector<UCHAR> keyObject(keyObjectLength);
    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (!NT_SUCCESS(BCryptGenerateSymmetricKey(hAlg,
                                               &hKey,
                                               keyObject.data(),
                                               static_cast<ULONG>(keyObject.size()),
                                               const_cast<PUCHAR>(key256.data()),
                                               static_cast<ULONG>(key256.size()),
                                               0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(iv);
    authInfo.cbNonce = kGcmIvBytes;
    authInfo.pbAuthData = nullptr;
    authInfo.cbAuthData = 0;
    authInfo.pbTag = tag;
    authInfo.cbTag = kGcmTagBytes;

    ciphertext.resize(plaintext.size());
    ULONG produced = 0;
    const NTSTATUS status =
        BCryptEncrypt(hKey,
                      reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
                      static_cast<ULONG>(plaintext.size()),
                      &authInfo,
                      nullptr,
                      0,
                      ciphertext.empty() ? nullptr : ciphertext.data(),
                      static_cast<ULONG>(ciphertext.size()),
                      &produced,
                      0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return NT_SUCCESS(status) && produced == ciphertext.size();
}

bool aesGcmDecrypt(const std::vector<std::uint8_t>& key256,
                   const std::uint8_t iv[kGcmIvBytes],
                   const std::uint8_t* ciphertext,
                   ULONG ciphertextLength,
                   const std::uint8_t tag[kGcmTagBytes],
                   std::string& plaintext) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        return false;
    }

    if (!setAesGcmChainingMode(hAlg)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    DWORD keyObjectLength = 0;
    DWORD cbData = 0;
    if (!NT_SUCCESS(BCryptGetProperty(hAlg,
                                      BCRYPT_OBJECT_LENGTH,
                                      reinterpret_cast<PUCHAR>(&keyObjectLength),
                                      sizeof(keyObjectLength),
                                      &cbData,
                                      0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    std::vector<UCHAR> keyObject(keyObjectLength);
    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (!NT_SUCCESS(BCryptGenerateSymmetricKey(hAlg,
                                               &hKey,
                                               keyObject.data(),
                                               static_cast<ULONG>(keyObject.size()),
                                               const_cast<PUCHAR>(key256.data()),
                                               static_cast<ULONG>(key256.size()),
                                               0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    plaintext.assign(ciphertextLength, '\0');

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(iv);
    authInfo.cbNonce = kGcmIvBytes;
    authInfo.pbAuthData = nullptr;
    authInfo.cbAuthData = 0;
    authInfo.pbTag = const_cast<PUCHAR>(tag);
    authInfo.cbTag = kGcmTagBytes;

    ULONG produced = 0;
    const NTSTATUS status =
        BCryptDecrypt(hKey,
                      const_cast<PUCHAR>(ciphertext),
                      ciphertextLength,
                      &authInfo,
                      nullptr,
                      0,
                      reinterpret_cast<PUCHAR>(&plaintext[0]),
                      static_cast<ULONG>(plaintext.size()),
                      &produced,
                      0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (!NT_SUCCESS(status)) {
        plaintext.clear();
        return false;
    }
    plaintext.resize(produced);
    return true;
}

}  // namespace

bool recvExact(SOCKET socketHandle, void* buffer, std::size_t length) {
    auto* bytes = static_cast<char*>(buffer);
    std::size_t total = 0;
    while (total < length) {
        const int chunk = recv(socketHandle, bytes + total, static_cast<int>(length - total), 0);
        if (chunk == SOCKET_ERROR || chunk == 0) {
            return false;
        }
        total += static_cast<std::size_t>(chunk);
    }
    return true;
}

bool sendSecureFrame(SOCKET socketHandle,
                     const std::vector<std::uint8_t>& key256,
                     const std::string& plaintext) {
    if (key256.size() != kAesKeyBytes) {
        return false;
    }
    if (plaintext.size() > MAX_SECURE_FRAME) {
        return false;
    }

    std::uint8_t iv[kGcmIvBytes];
    if (!randomBytes(iv, sizeof(iv))) {
        return false;
    }

    std::vector<std::uint8_t> ciphertext;
    std::uint8_t tag[kGcmTagBytes];
    if (!aesGcmEncrypt(key256, iv, plaintext, ciphertext, tag)) {
        return false;
    }

    const std::uint32_t payload =
        static_cast<std::uint32_t>(kGcmIvBytes + ciphertext.size() + kGcmTagBytes);
    if (payload < kGcmIvBytes + kGcmTagBytes || payload > MAX_SECURE_FRAME) {
        return false;
    }

    const std::uint32_t lengthBE = htonl(payload);
    if (!Protocol::sendAll(socketHandle,
                           std::string(reinterpret_cast<const char*>(&lengthBE), sizeof(lengthBE)))) {
        return false;
    }
    if (!Protocol::sendAll(socketHandle, std::string(reinterpret_cast<const char*>(iv), sizeof(iv)))) {
        return false;
    }
    if (!ciphertext.empty() &&
        !Protocol::sendAll(socketHandle,
                           std::string(reinterpret_cast<const char*>(ciphertext.data()),
                                       ciphertext.size()))) {
        return false;
    }
    return Protocol::sendAll(socketHandle,
                            std::string(reinterpret_cast<const char*>(tag), sizeof(tag)));
}

bool recvSecureFrame(SOCKET socketHandle,
                     const std::vector<std::uint8_t>& key256,
                     std::string& plaintext) {
    if (key256.size() != kAesKeyBytes) {
        return false;
    }

    std::uint32_t lengthBE = 0;
    if (!recvExact(socketHandle, &lengthBE, sizeof(lengthBE))) {
        return false;
    }
    const std::uint32_t payload = ntohl(lengthBE);
    if (payload < kGcmIvBytes + kGcmTagBytes || payload > MAX_SECURE_FRAME) {
        return false;
    }

    std::vector<std::uint8_t> wire(payload);
    if (!recvExact(socketHandle, wire.data(), wire.size())) {
        return false;
    }

    const std::uint8_t* iv = wire.data();
    const std::uint8_t* tag = wire.data() + wire.size() - kGcmTagBytes;
    const std::uint8_t* cipher = wire.data() + kGcmIvBytes;
    const ULONG cipherLength = static_cast<ULONG>(wire.size() - kGcmIvBytes - kGcmTagBytes);

    return aesGcmDecrypt(key256, iv, cipher, cipherLength, tag, plaintext);
}

bool clientHandshake(SOCKET socketHandle,
                     const std::string& psk,
                     std::vector<std::uint8_t>& key256,
                     std::string& error) {
    if (psk.empty()) {
        error = "ERROR Pre-shared key is empty.";
        return false;
    }

    std::uint8_t clientNonce[16];
    if (!randomBytes(clientNonce, sizeof(clientNonce))) {
        error = "ERROR Random generator failed.";
        return false;
    }

    const std::string line = std::string("SECURE_V1 ") + hexEncode(clientNonce, sizeof(clientNonce));
    if (!Protocol::sendLine(socketHandle, line)) {
        error = "ERROR Failed to send SECURE_V1.";
        return false;
    }

    std::string response;
    if (!Protocol::recvLine(socketHandle, response)) {
        error = "ERROR Server closed the connection during secure handshake.";
        return false;
    }

    const auto tokens = Protocol::splitWhitespace(Protocol::trim(response));
    if (tokens.size() == 2 && Protocol::iequals(tokens[0], "SECURE_OK")) {
        std::uint8_t serverNonce[16];
        if (!hexDecode16Bytes(tokens[1], serverNonce)) {
            error = "ERROR Invalid SECURE_OK nonce from server.";
            return false;
        }
        if (!sha256PskNonces(psk, clientNonce, serverNonce, key256)) {
            error = "ERROR Key derivation failed.";
            return false;
        }
        return true;
    }

    error = response.empty() ? "ERROR Secure handshake failed." : response;
    return false;
}

bool serverHandshakeFromLine(SOCKET socketHandle,
                             const std::string& requestLine,
                             const std::string& pskConfigured,
                             std::vector<std::uint8_t>& key256,
                             std::string& error) {
    if (pskConfigured.empty()) {
        error = "ERROR Secure channel is not configured on this server.";
        return false;
    }

    const auto tokens = Protocol::splitWhitespace(Protocol::trim(requestLine));
    if (tokens.size() != 2 || !Protocol::iequals(tokens[0], "SECURE_V1")) {
        error = "ERROR Usage: SECURE_V1 <32-hex-char nonce>.";
        return false;
    }

    std::uint8_t clientNonce[16];
    if (!hexDecode16Bytes(tokens[1], clientNonce)) {
        error = "ERROR Invalid client nonce (expect 32 hex characters).";
        return false;
    }

    std::uint8_t serverNonce[16];
    if (!randomBytes(serverNonce, sizeof(serverNonce))) {
        error = "ERROR Random generator failed.";
        return false;
    }

    const std::string okLine = std::string("SECURE_OK ") + hexEncode(serverNonce, sizeof(serverNonce));
    if (!Protocol::sendLine(socketHandle, okLine)) {
        error = "ERROR Failed to send SECURE_OK.";
        return false;
    }

    if (!sha256PskNonces(pskConfigured, clientNonce, serverNonce, key256)) {
        error = "ERROR Key derivation failed.";
        return false;
    }

    return true;
}

}  // namespace SecureChannel
