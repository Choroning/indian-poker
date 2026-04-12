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

enum class DecodeStatus { NeedMore, MessageReady, Error };

struct DecodeResult {
    DecodeStatus status = DecodeStatus::NeedMore;
    Opcode opcode = Opcode::Text;
    std::string message;
    std::size_t consumed = 0;
    std::string error_reason;
};

// Reassembles fragmented data messages and exposes control frames separately.
// Control frames (ping/pong/close) are never fragmented per RFC; they may appear
// in between fragments of a data message.
class FrameDecoder {
public:
    DecodeResult feed(const char* data, std::size_t len);

private:
    std::vector<uint8_t> buf_;
    std::string pending_msg_;   // bytes of an in-progress fragmented message
    Opcode pending_opcode_ = Opcode::Text;
    bool in_fragment_ = false;
};

// Encode helpers: single frame, unmasked (server->client).
std::vector<char> encode_text(const std::string& payload);

// Encodes a control frame with the given opcode (Close, Ping, or Pong).
std::vector<char> encode_control(Opcode opcode, const std::string& payload);

// Splits `payload` into N server frames of up to `chunk_size` bytes each.
// The first frame carries opcode=Text with FIN=0, middle frames carry
// Continuation with FIN=0, final frame carries Continuation with FIN=1.
// Single-chunk payloads return a single frame with FIN=1, opcode=Text.
std::vector<std::vector<char>> encode_text_fragmented(const std::string& payload, std::size_t chunk_size);

} // namespace ip::net
