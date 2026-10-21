#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace nettrace {
constexpr std::size_t header_size = 24;
constexpr std::size_t max_payload = 65536;
using Bytes = std::vector<std::uint8_t>;
enum class MessageType : std::uint8_t { request = 1, response = 2 };
struct Message {
    MessageType type;
    std::uint64_t id;
    std::string payload;
};
class ProtocolError : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
Bytes encode(const Message& message);
std::size_t payload_length(const Bytes& header);
Message decode(const Bytes& frame);
// One decoder per connection. A malformed frame is terminal: discard the decoder.
class FrameDecoder {
public:
    std::vector<Message> feed(const std::uint8_t* data, std::size_t size);
    bool incomplete() const { return !buffer_.empty(); }
private:
    Bytes buffer_;
};
}
