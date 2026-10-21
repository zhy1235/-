#pragma once
#include <cstdint>
#include <fstream>
#include <string>

namespace nettrace {
std::uint64_t parse_uint(const std::string& value);
std::string hex_encode(const std::string& value);
std::string hex_decode(const std::string& value);
std::string new_session();
struct LogEntry {
    std::uint64_t timestamp_ms = 0;
    std::string session;
    std::string role;
    std::uint64_t id = 0;
    std::string event;
    unsigned type = 0;
    std::uint64_t bytes = 0;
    std::uint64_t elapsed_us = 0;
    std::string detail;
};
std::string format_log(const LogEntry& entry);
LogEntry parse_log(const std::string& line);
class Logger {
public:
    Logger(const std::string& path, std::string role);
    void write(std::uint64_t id, const std::string& event, unsigned type,
               std::uint64_t bytes = 0, std::uint64_t elapsed_us = 0, const std::string& detail = "");
private:
    std::ofstream stream_;
    std::string role_;
    std::string session_;
};
}
