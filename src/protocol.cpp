#include "nettrace/protocol.hpp"
#include <algorithm>

namespace nettrace {
namespace {
void put(Bytes& b, std::uint64_t n, std::size_t count) {
    for (std::size_t i = count; i > 0; --i) b.push_back(static_cast<std::uint8_t>(n >> ((i - 1) * 8)));
}
std::uint64_t get(const Bytes& b, std::size_t pos, std::size_t count) {
    std::uint64_t n = 0;
    for (std::size_t i = 0; i < count; ++i) n = (n << 8) | b.at(pos + i);
    return n;
}
std::uint32_t checksum(const Bytes& b) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (i >= 20 && i < 24) continue;
        hash = (hash ^ b[i]) * 16777619U;
    }
    return hash;
}
}
Bytes encode(const Message& m) {
    if (m.payload.size() > max_payload) throw ProtocolError("payload exceeds 65536 bytes");
    if (m.type != MessageType::request && m.type != MessageType::response) throw ProtocolError("unknown type");
    Bytes b{'N', 'T', 'R', 'C', 1, static_cast<std::uint8_t>(m.type), 0, 0};
    put(b, m.payload.size(), 4);
    put(b, m.id, 8);
    put(b, 0, 4);
    b.insert(b.end(), m.payload.begin(), m.payload.end());
    const auto sum = checksum(b);
    for (std::size_t i = 0; i < 4; ++i) b[20 + i] = static_cast<std::uint8_t>(sum >> ((3 - i) * 8));
    return b;
}
std::size_t payload_length(const Bytes& h) {
    if (h.size() < header_size) throw ProtocolError("truncated header");
    if (h[0] != 'N' || h[1] != 'T' || h[2] != 'R' || h[3] != 'C') throw ProtocolError("bad magic");
    if (h[4] != 1) throw ProtocolError("unsupported version");
    if (h[5] != 1 && h[5] != 2) throw ProtocolError("unknown type");
    if (h[6] || h[7]) throw ProtocolError("reserved bits must be zero");
    const auto n = static_cast<std::size_t>(get(h, 8, 4));
    if (n > max_payload) throw ProtocolError("payload exceeds 65536 bytes");
    return n;
}
Message decode(const Bytes& b) {
    const auto size = payload_length(b);
    if (b.size() != header_size + size) throw ProtocolError("frame length mismatch");
    if (get(b, 20, 4) != checksum(b)) throw ProtocolError("checksum mismatch");
    return {static_cast<MessageType>(b[5]), get(b, 12, 8), std::string(b.begin() + header_size, b.end())};
}
std::vector<Message> FrameDecoder::feed(const std::uint8_t* data, std::size_t size) {
    std::vector<Message> result;
    while (size) {
        const auto target = buffer_.size() < header_size ? header_size : header_size + payload_length(buffer_);
        const auto count = std::min(size, target - buffer_.size());
        buffer_.insert(buffer_.end(), data, data + count);
        data += count;
        size -= count;
        if (buffer_.size() >= header_size && buffer_.size() == header_size + payload_length(buffer_)) {
            result.push_back(decode(buffer_));
            buffer_.clear();
        }
    }
    return result;
}
}
