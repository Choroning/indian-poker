#include "net/ws_frame.h"

#include <cstring>
#include <stdexcept>

namespace ip::net {

namespace {

struct RawFrame {
    bool fin;
    Opcode opcode;
    std::string payload;
    std::size_t consumed;
};

enum class RawStatus { NeedMore, Ready, Error };

RawStatus parse_one_frame(const std::vector<uint8_t>& buf, RawFrame& out, std::string& err) {
    if (buf.size() < 2) return RawStatus::NeedMore;

    uint8_t b0 = buf[0];
    uint8_t b1 = buf[1];
    bool fin = (b0 & 0x80) != 0;
    uint8_t rsv = (b0 & 0x70);
    uint8_t opcode = b0 & 0x0F;
    bool mask = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;

    if (rsv != 0) { err = "reserved bits set"; return RawStatus::Error; }
    if (!mask) { err = "client frame not masked"; return RawStatus::Error; }

    std::size_t header = 2;
    if (payload_len == 126) {
        if (buf.size() < header + 2) return RawStatus::NeedMore;
        payload_len = (uint64_t(buf[header]) << 8) | uint64_t(buf[header + 1]);
        header += 2;
    } else if (payload_len == 127) {
        if (buf.size() < header + 8) return RawStatus::NeedMore;
        payload_len = 0;
        for (std::size_t i = 0; i < 8; ++i) payload_len = (payload_len << 8) | uint64_t(buf[header + i]);
        header += 8;
    }

    if (payload_len > 4096) { err = "payload too large"; return RawStatus::Error; }

    // Control frames must be <= 125 bytes and FIN=1 per RFC.
    bool is_control = (opcode & 0x08) != 0;
    if (is_control) {
        if (payload_len > 125) { err = "oversized control frame"; return RawStatus::Error; }
        if (!fin) { err = "fragmented control frame"; return RawStatus::Error; }
    }

    if (buf.size() < header + 4) return RawStatus::NeedMore;
    uint8_t key[4] = {buf[header], buf[header + 1], buf[header + 2], buf[header + 3]};
    header += 4;

    if (buf.size() < header + payload_len) return RawStatus::NeedMore;

    out.fin = fin;
    out.opcode = static_cast<Opcode>(opcode);
    out.payload.assign(payload_len, '\0');
    for (uint64_t i = 0; i < payload_len; ++i) {
        out.payload[i] = static_cast<char>(buf[header + i] ^ key[i % 4]);
    }
    out.consumed = header + payload_len;
    return RawStatus::Ready;
}

} // namespace

DecodeResult FrameDecoder::feed(const char* data, std::size_t len) {
    buf_.insert(buf_.end(),
                reinterpret_cast<const uint8_t*>(data),
                reinterpret_cast<const uint8_t*>(data) + len);

    DecodeResult result;
    RawFrame frame;
    std::string err;
    auto status = parse_one_frame(buf_, frame, err);
    if (status == RawStatus::NeedMore) {
        result.status = DecodeStatus::NeedMore;
        return result;
    }
    if (status == RawStatus::Error) {
        result.status = DecodeStatus::Error;
        result.error_reason = err;
        return result;
    }

    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(frame.consumed));

    // Control frames stand alone.
    bool is_control = (static_cast<uint8_t>(frame.opcode) & 0x08) != 0;
    if (is_control) {
        result.status = DecodeStatus::MessageReady;
        result.opcode = frame.opcode;
        result.message = std::move(frame.payload);
        result.consumed = frame.consumed;
        return result;
    }

    // Data frame.
    if (frame.opcode == Opcode::Continuation) {
        if (!in_fragment_) {
            result.status = DecodeStatus::Error;
            result.error_reason = "continuation frame with no start";
            return result;
        }
        pending_msg_.append(frame.payload);
        if (frame.fin) {
            result.status = DecodeStatus::MessageReady;
            result.opcode = pending_opcode_;
            result.message = std::move(pending_msg_);
            pending_msg_.clear();
            in_fragment_ = false;
            return result;
        }
        result.status = DecodeStatus::NeedMore;
        return result;
    }

    // Start of a data message (opcode Text or Binary).
    if (in_fragment_) {
        result.status = DecodeStatus::Error;
        result.error_reason = "new data frame during fragmented message";
        return result;
    }
    if (frame.fin) {
        result.status = DecodeStatus::MessageReady;
        result.opcode = frame.opcode;
        result.message = std::move(frame.payload);
        result.consumed = frame.consumed;
        return result;
    }
    in_fragment_ = true;
    pending_opcode_ = frame.opcode;
    pending_msg_ = std::move(frame.payload);
    result.status = DecodeStatus::NeedMore;
    return result;
}

static void write_header(std::vector<char>& out, uint8_t b0, std::size_t n) {
    out.push_back(static_cast<char>(b0));
    if (n < 126) {
        out.push_back(static_cast<char>(n));
    } else if (n <= 0xFFFF) {
        out.push_back(static_cast<char>(126));
        out.push_back(static_cast<char>((n >> 8) & 0xFF));
        out.push_back(static_cast<char>(n & 0xFF));
    } else {
        out.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
    }
}

std::vector<char> encode_text(const std::string& payload) {
    std::vector<char> out;
    write_header(out, 0x81, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<char> encode_control(Opcode opcode, const std::string& payload) {
    if (payload.size() > 125) throw std::runtime_error("control payload > 125");
    std::vector<char> out;
    uint8_t b0 = 0x80 | static_cast<uint8_t>(opcode);
    out.push_back(static_cast<char>(b0));
    out.push_back(static_cast<char>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::vector<char>> encode_text_fragmented(const std::string& payload, std::size_t chunk_size) {
    if (chunk_size == 0) throw std::runtime_error("chunk_size must be > 0");
    std::vector<std::vector<char>> out;
    if (payload.size() <= chunk_size) {
        out.push_back(encode_text(payload));
        return out;
    }
    std::size_t offset = 0;
    bool first = true;
    while (offset < payload.size()) {
        std::size_t remaining = payload.size() - offset;
        std::size_t take = remaining < chunk_size ? remaining : chunk_size;
        bool last = (offset + take == payload.size());
        uint8_t b0 = static_cast<uint8_t>((last ? 0x80 : 0x00) |
                                          (first ? 0x1 : 0x0));
        std::vector<char> frame;
        write_header(frame, b0, take);
        frame.insert(frame.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset),
                     payload.begin() + static_cast<std::ptrdiff_t>(offset + take));
        out.push_back(std::move(frame));
        offset += take;
        first = false;
    }
    return out;
}

} // namespace ip::net
