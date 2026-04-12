#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace ip::net {

enum class HandshakeStatus {
    NeedMore, // Request headers not yet complete (no \r\n\r\n seen).
    Complete, // Valid Upgrade request fully parsed.
    Invalid,  // Syntactically malformed or missing required headers.
};

struct HandshakeResult {
    HandshakeStatus status = HandshakeStatus::NeedMore;
    std::string key;     // Sec-WebSocket-Key (base64-encoded).
    std::string origin;  // Origin header (may be empty).
    std::size_t consumed = 0; // Bytes of the input that belonged to the request.
};

// Parses an in-flight HTTP Upgrade request. Idempotent; the caller may accumulate
// bytes and call repeatedly until the status becomes Complete or Invalid.
HandshakeResult parse_handshake(std::string_view input);

// Computes base64(SHA1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")) per RFC 6455.
std::string compute_accept(std::string_view key);

// Builds a complete HTTP 101 response including the Sec-WebSocket-Accept header.
std::string build_response(std::string_view key);

} // namespace ip::net
