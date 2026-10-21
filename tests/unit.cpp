#include "nettrace/analyzer.hpp"
#include "nettrace/log.hpp"
#include "nettrace/protocol.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>

using namespace nettrace;
namespace {
int checks = 0;
void check(bool ok, const std::string& name) {
    ++checks;
    if (!ok) throw std::runtime_error("FAILED: " + name);
}
void rejects(const std::function<void()>& f, const std::string& name) {
    bool rejected = false;
    try { f(); } catch (const std::exception&) { rejected = true; }
    check(rejected, name);
}
void protocol_tests() {
    const Message m{MessageType::request, 0x0102030405060708ULL, std::string("a\0b\t\n", 5)};
    const auto frame = encode(m);
    check(frame[8] == 0 && frame[11] == 5 && frame[12] == 1 && frame[19] == 8, "network byte order");
    const auto reply = decode(frame);
    check(reply.id == m.id && reply.payload == m.payload && reply.type == m.type, "binary round trip");
    for (std::size_t split = 0; split <= frame.size(); ++split) {
        FrameDecoder parser;
        auto a = parser.feed(frame.data(), split);
        auto b = parser.feed(frame.data() + split, frame.size() - split);
        check(a.size() + b.size() == 1 && !parser.incomplete(), "every split boundary");
    }
    FrameDecoder parser;
    auto joined = frame;
    joined.insert(joined.end(), frame.begin(), frame.end());
    check(parser.feed(joined.data(), joined.size()).size() == 2, "coalesced frames");
    FrameDecoder bytewise;
    std::size_t total = 0;
    for (const auto& byte : joined) total += bytewise.feed(&byte, 1).size();
    check(total == 2 && !bytewise.incomplete(), "one byte fragments");
    auto maximum = encode({MessageType::response, std::numeric_limits<std::uint64_t>::max(), std::string(max_payload, 'x')});
    check(decode(maximum).payload.size() == max_payload, "maximum payload and id");
    auto empty = encode({MessageType::response, 0, ""});
    FrameDecoder empty_parser;
    check(empty_parser.feed(empty.data(), empty.size()).size() == 1, "empty frame");
    rejects([&] { encode({m.type, 1, std::string(max_payload + 1, 'x')}); }, "oversize encode");
    rejects([&] { encode({static_cast<MessageType>(9), 1, ""}); }, "invalid type encode");
    for (const auto pos : {0U, 4U, 5U, 6U, 7U, 8U, 12U, 20U, 24U}) {
        auto bad = frame;
        bad[pos] ^= 0x80;
        rejects([&] { decode(bad); }, "mutated header or payload");
    }
    for (std::size_t size = 0; size < frame.size(); ++size) {
        const Bytes short_frame(frame.begin(), frame.begin() + size);
        rejects([&] { decode(short_frame); }, "truncated frame");
    }
    auto extra = frame; extra.push_back(0);
    rejects([&] { decode(extra); }, "trailing bytes");
    FrameDecoder partial;
    partial.feed(frame.data(), 10);
    check(partial.incomplete(), "incomplete state");
    // Fixed-seed arbitrary fragmentation of a long stream.
    std::mt19937 rng(42);
    Bytes stream;
    for (int i = 0; i < 250; ++i) {
        const auto b = encode({MessageType::request, static_cast<std::uint64_t>(i), std::string(rng() % 1024, static_cast<char>(i))});
        stream.insert(stream.end(), b.begin(), b.end());
    }
    FrameDecoder random_parser;
    std::size_t offset = 0, seen = 0;
    while (offset < stream.size()) {
        auto n = std::min<std::size_t>(1 + rng() % 3000, stream.size() - offset);
        for (const auto& msg : random_parser.feed(stream.data() + offset, n)) check(msg.id == seen++, "random fragmentation order");
        offset += n;
    }
    check(seen == 250 && !random_parser.incomplete(), "random fragmentation complete");
}
void log_tests() {
    const LogEntry entry{1000, "session-1", "client", 7, "ok", 2, 4, 120, std::string("\0\t\n", 3)};
    const auto parsed = parse_log(format_log(entry));
    check(parsed.id == entry.id && parsed.detail == entry.detail, "log binary escaping");
    check(parse_log(format_log(entry) + "\r").elapsed_us == 120, "CRLF logs");
    for (const auto& value : {"-1", "+1", "", "1junk", "18446744073709551616", " 1"}) rejects([&] { parse_uint(value); }, "strict integer");
    check(parse_uint("18446744073709551615") == std::numeric_limits<std::uint64_t>::max(), "uint64 limit");
    rejects([] { hex_decode("a"); }, "odd hex");
    rejects([] { hex_decode("xz"); }, "invalid hex");
    rejects([] { parse_log("bad log"); }, "missing columns");
    auto bad = entry; bad.role = "unknown";
    rejects([&] { parse_log(format_log(bad)); }, "invalid role");
    bad = entry; bad.event = "unknown";
    rejects([&] { parse_log(format_log(bad)); }, "invalid event");
    bad = entry; bad.session = "x\ny";
    rejects([&] { parse_log(format_log(bad)); }, "invalid session");
    bad = entry; bad.type = 3;
    rejects([&] { parse_log(format_log(bad)); }, "invalid type");
    std::stringstream input;
    const auto add = [&](std::string session, std::uint64_t id, std::string event, std::uint64_t us = 0) {
        input << format_log({100, std::move(session), "client", id, std::move(event), 1, 1, us, ""}) << '\n';
    };
    add("a", 1, "sent"); add("a", 1, "ok", 100);
    add("b", 1, "sent"); add("b", 1, "ok", 300);
    add("a", 2, "sent"); add("a", 2, "timeout");
    add("a", 3, "sent"); add("a", 3, "protocol_error");
    add("a", 4, "sent"); add("a", 4, "disconnected");
    add("a", 5, "sent"); add("a", 5, "io_error");
    add("a", 6, "sent");
    add("a", 7, "sent"); add("a", 7, "ok"); add("a", 7, "ok");
    add("a", 8, "ok");
    add("a", 9, "sent"); add("a", 9, "sent");
    input << "malformed\n";
    input << format_log({100, "srv", "server", 1, "received", 1, 0, 0, ""}) << '\n';
    const auto s = analyze(input);
    check(s.sent == 7 && s.ok == 2 && s.timeout == 1 && s.protocol_errors == 1 && s.disconnected == 1 && s.io_errors == 1 && s.pending == 1, "outcome counters and session correlation");
    check(s.malformed_lines == 1 && s.inconsistent_requests == 3, "duplicate and orphan diagnostics");
    check(s.min_us == 100 && s.mean_us == 200 && s.p50_us == 100 && s.p95_us == 300 && s.max_us == 300, "nearest rank percentiles");
    std::stringstream empty;
    check(analyze(empty).success_rate == 0, "empty summary");
}
}
int main() {
    try { protocol_tests(); log_tests(); std::cout << "PASS " << checks << " checks\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
