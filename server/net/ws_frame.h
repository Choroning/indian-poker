#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ip::net {

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

enum class DecodeStatus {
    NeedMore,     // not enough bytes yet
    MessageReady, // one complete logical message decoded
    Error,        // protocol error (unmasked client frame, bad opcode, etc.)
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::NeedMore;
    Opcode opcode = Opcode::Text;
    std::string message;
    std::size_t consumed = 0;
    std::string error_reason;
};

// Decodes a stream of WebSocket frames into messages. For milestone M3 only
// non-fragmented frames are supported; any continuation frame or FIN=0 returns
// an error. Milestone M4 extends this class to reassemble fragments.
class FrameDecoder {
public:
    DecodeResult feed(const char* data, std::size_t len);

private:
    std::vector<uint8_t> buf_;
};

// Encodes `payload` as a single unmasked server text frame.
std::vector<char> encode_text(const std::string& payload);

} // namespace ip::net
