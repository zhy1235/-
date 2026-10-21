#include "nettrace/log.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace nettrace {
std::uint64_t parse_uint(const std::string& value) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) throw std::invalid_argument("invalid unsigned integer: " + value);
    return result;
}
std::string hex_encode(const std::string& s) {
    const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
std::string hex_decode(const std::string& s) {
    if (s.size() % 2) throw std::invalid_argument("odd hex length");
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        throw std::invalid_argument("invalid hex digit");
    };
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2) out += static_cast<char>((digit(s[i]) << 4) | digit(s[i + 1]));
    return out;
}
std::string new_session() {
    std::random_device rng;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) out << std::setw(8) << static_cast<std::uint32_t>(rng());
    return out.str();
}
std::string format_log(const LogEntry& e) {
    std::ostringstream out;
    out << e.timestamp_ms << '\t' << e.session << '\t' << e.role << '\t' << e.id << '\t'
        << e.event << '\t' << e.type << '\t' << e.bytes << '\t' << e.elapsed_us << '\t' << hex_encode(e.detail);
    return out.str();
}
LogEntry parse_log(const std::string& raw) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> cols;
    std::size_t begin = 0;
    for (;;) {
        auto end = line.find('\t', begin);
        cols.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (cols.size() != 9) throw std::invalid_argument("expected 9 tab-separated columns");
    if (cols[1].empty() || cols[1].size() > 64 || !std::all_of(cols[1].begin(), cols[1].end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_'; })) throw std::invalid_argument("invalid session");
    if (cols[2] != "client" && cols[2] != "server") throw std::invalid_argument("invalid role");
    const std::set<std::string> client_events{"sent", "ok", "timeout", "protocol_error", "disconnected", "io_error"};
    const std::set<std::string> server_events{"received", "responded", "dropped", "delayed", "corrupted", "disconnected", "protocol_error", "timeout", "io_error"};
    const auto& events = cols[2] == "client" ? client_events : server_events;
    if (!events.count(cols[4])) throw std::invalid_argument("invalid event");
    const auto type = parse_uint(cols[5]);
    const auto bytes = parse_uint(cols[6]);
    if (type > 2 || bytes > 65536) throw std::invalid_argument("invalid message type or size");
    return {parse_uint(cols[0]), cols[1], cols[2], parse_uint(cols[3]), cols[4], static_cast<unsigned>(type), bytes, parse_uint(cols[7]), hex_decode(cols[8])};
}
Logger::Logger(const std::string& path, std::string role)
    : stream_(path, std::ios::app), role_(std::move(role)), session_(new_session()) {
    if (!stream_) throw std::runtime_error("cannot open log: " + path);
}
void Logger::write(std::uint64_t id, const std::string& event, unsigned type, std::uint64_t bytes, std::uint64_t elapsed, const std::string& detail) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    stream_ << format_log({static_cast<std::uint64_t>(now), session_, role_, id, event, type, bytes, elapsed, detail}) << '\n';
    stream_.flush();
    if (!stream_) throw std::runtime_error("failed to write log");
}
}
