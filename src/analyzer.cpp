#include "nettrace/analyzer.hpp"
#include "nettrace/log.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <vector>

namespace nettrace {
Summary analyze(std::istream& input, std::ostream* diagnostics) {
    struct Request { unsigned sent = 0, terminal = 0; std::string event; std::uint64_t elapsed = 0; };
    std::map<std::pair<std::string, std::uint64_t>, Request> requests;
    Summary s;
    std::string line;
    std::size_t number = 0;
    while (std::getline(input, line)) {
        ++number;
        try {
            const auto e = parse_log(line);
            ++s.valid_lines;
            ++s.events[e.role + "." + e.event];
            if (e.role != "client") continue;
            auto& r = requests[{e.session, e.id}];
            if (e.event == "sent") ++r.sent;
            else { ++r.terminal; r.event = e.event; r.elapsed = e.elapsed_us; }
        } catch (const std::invalid_argument& e) {
            ++s.malformed_lines;
            if (diagnostics) *diagnostics << "line " << number << ": " << e.what() << '\n';
        }
    }
    if (input.bad()) throw std::runtime_error("failed reading log");
    std::vector<double> times;
    for (const auto& item : requests) {
        const auto& r = item.second;
        if (r.sent != 1 || r.terminal > 1) { ++s.inconsistent_requests; continue; }
        ++s.sent;
        if (!r.terminal) ++s.pending;
        else if (r.event == "ok") { ++s.ok; times.push_back(static_cast<double>(r.elapsed)); }
        else if (r.event == "timeout") ++s.timeout;
        else if (r.event == "protocol_error") ++s.protocol_errors;
        else if (r.event == "disconnected") ++s.disconnected;
        else if (r.event == "io_error") ++s.io_errors;
    }
    s.success_rate = s.sent ? static_cast<double>(s.ok) / static_cast<double>(s.sent) : 0;
    if (!times.empty()) {
        std::sort(times.begin(), times.end());
        s.min_us = times.front(); s.max_us = times.back();
        double sum = 0;
        for (const auto t : times) sum += t;
        s.mean_us = sum / static_cast<double>(times.size());
        const auto percentile = [&](double p) { return times[static_cast<std::size_t>(std::ceil(p * static_cast<double>(times.size()))) - 1]; };
        s.p50_us = percentile(0.50); s.p95_us = percentile(0.95);
    }
    return s;
}
std::string Summary::json() const {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << "{\n  \"valid_lines\": " << valid_lines << ",\n  \"malformed_lines\": " << malformed_lines
        << ",\n  \"inconsistent_requests\": " << inconsistent_requests
        << ",\n  \"sent\": " << sent << ",\n  \"ok\": " << ok << ",\n  \"timeout\": " << timeout
        << ",\n  \"protocol_errors\": " << protocol_errors << ",\n  \"disconnected\": " << disconnected
        << ",\n  \"io_errors\": " << io_errors << ",\n  \"pending\": " << pending
        << ",\n  \"success_rate\": " << success_rate
        << ",\n  \"latency_us\": {\"min\": " << min_us << ", \"mean\": " << mean_us << ", \"p50\": " << p50_us << ", \"p95\": " << p95_us << ", \"max\": " << max_us << "},\n  \"events\": {";
    bool first = true;
    for (const auto& e : events) { if (!first) out << ", "; first = false; out << '\"' << e.first << "\": " << e.second; }
    out << "}\n}\n";
    return out.str();
}
}
