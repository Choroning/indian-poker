#include "net/ws_frame.h"

#include <algorithm>
#include <cstring>

namespace ip::net {

namespace {

// Attempts to decode exactly one frame from `buf`. If not enough bytes, returns
// NeedMore. Fills `consumed`, `opcode`, and the unmasked payload into `out`.
DecodeStatus decode_one(const std::vector<uint8_t>& buf, DecodeResult& out) {
    if (buf.size() < 2) return DecodeStatus::NeedMore;

    uint8_t b0 = buf[0];
    uint8_t b1 = buf[1];
    bool fin = (b0 & 0x80) != 0;
    uint8_t rsv = (b0 & 0x70);
    uint8_t opcode = b0 & 0x0F;
    bool mask = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;

    if (rsv != 0) {
        out.error_reason = "reserved bits set";
        return DecodeStatus::Error;
    }
    if (!mask) {
        out.error_reason = "client frame not masked";
        return DecodeStatus::Error;
    }
    if (!fin) {
        // M3 does not support fragmentation yet; M4 will.
        out.error_reason = "fragmentation not supported in M3";
        return DecodeStatus::Error;
    }
    if (opcode != static_cast<uint8_t>(Opcode::Text)) {
        out.error_reason = "unsupported opcode in M3";
        return DecodeStatus::Error;
    }

    std::size_t header = 2;
    if (payload_len == 126) {
        if (buf.size() < header + 2) return DecodeStatus::NeedMore;
        payload_len = (uint64_t(buf[header]) << 8) | uint64_t(buf[header + 1]);
        header += 2;
    } else if (payload_len == 127) {
        if (buf.size() < header + 8) return DecodeStatus::NeedMore;
        payload_len = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | uint64_t(buf[header + i]);
        }
        header += 8;
    }

    if (buf.size() < header + 4) return DecodeStatus::NeedMore;
    uint8_t key[4] = {buf[header], buf[header + 1], buf[header + 2], buf[header + 3]};
    header += 4;

    if (buf.size() < header + payload_len) return DecodeStatus::NeedMore;

    out.opcode = Opcode::Text;
    out.message.assign(payload_len, '\0');
    for (uint64_t i = 0; i < payload_len; ++i) {
        out.message[i] = static_cast<char>(buf[header + i] ^ key[i % 4]);
    }
    out.consumed = header + payload_len;
    return DecodeStatus::MessageReady;
}

} // namespace

DecodeResult FrameDecoder::feed(const char* data, std::size_t len) {
    buf_.insert(buf_.end(),
                reinterpret_cast<const uint8_t*>(data),
                reinterpret_cast<const uint8_t*>(data) + len);
    DecodeResult result;
    auto status = decode_one(buf_, result);
    result.status = status;
    if (status == DecodeStatus::MessageReady) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(result.consumed));
    }
    return result;
}

std::vector<char> encode_text(const std::string& payload) {
    std::vector<char> out;
    out.push_back(static_cast<char>(0x81)); // FIN=1, opcode=1
    std::size_t n = payload.size();
    if (n < 126) {
        out.push_back(static_cast<char>(n));
    } else if (n <= 0xFFFF) {
        out.push_back(static_cast<char>(126));
        out.push_back(static_cast<char>((n >> 8) & 0xFF));
        out.push_back(static_cast<char>(n & 0xFF));
    } else {
        out.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
        }
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

} // namespace ip::net
