#pragma once
#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>

namespace nettrace {
struct Summary {
    std::size_t valid_lines = 0, malformed_lines = 0, inconsistent_requests = 0;
    std::size_t sent = 0, ok = 0, timeout = 0, protocol_errors = 0, disconnected = 0, io_errors = 0, pending = 0;
    double success_rate = 0, min_us = 0, mean_us = 0, p50_us = 0, p95_us = 0, max_us = 0;
    std::map<std::string, std::size_t> events;
    std::string json() const;
};
Summary analyze(std::istream& input, std::ostream* diagnostics = nullptr);
}
