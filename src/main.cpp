#include "nettrace/analyzer.hpp"
#include "nettrace/app.hpp"
#include "nettrace/log.hpp"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void usage() {
    std::cout << "NetTrace 1.0.0 - TCP communication and diagnostics\n"
        "Usage: nettrace <server|client|analyze> [options]\n"
        "server: --host IPv4 --port 0..65535 --log PATH --timeout-ms N\n"
        "        --max-connections N (0=unlimited)\n"
        "        --fault none|delay|drop|corrupt|disconnect --fault-every N --delay-ms N\n"
        "client: --host IPv4 --port 1..65535 --log PATH --timeout-ms N --count N --message TEXT\n"
        "analyze: --input PATH [--strict]\n"
        "Exit codes: 0=success, 1=usage/runtime/log validation error, 2=client transaction failure\n";
}
int integer(const std::string& s, int min, int max) {
    const auto n = nettrace::parse_uint(s);
    if (n < static_cast<std::uint64_t>(min) || n > static_cast<std::uint64_t>(max)) throw std::invalid_argument("numeric option out of range: " + s);
    return static_cast<int>(n);
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1 || std::string(argv[1]) == "--help") { usage(); return 0; }
        const std::string command = argv[1];
        if (command == "--version") { std::cout << "NetTrace 1.0.0\n"; return 0; }
        std::set<std::string> allowed;
        if (command == "client") allowed = {"--host", "--port", "--log", "--timeout-ms", "--count", "--message"};
        else if (command == "server") allowed = {"--host", "--port", "--log", "--timeout-ms", "--max-connections", "--fault", "--fault-every", "--delay-ms"};
        else if (command == "analyze") allowed = {"--input", "--strict"};
        else throw std::invalid_argument("unknown command: " + command);
        nettrace::Options o;
        o.log = command + ".log";
        std::set<std::string> seen;
        for (int i = 2; i < argc; ++i) {
            const std::string key = argv[i];
            if (!allowed.count(key)) throw std::invalid_argument("unknown option: " + key);
            if (!seen.insert(key).second) throw std::invalid_argument("duplicate option: " + key);
            if (key == "--strict") { o.strict = true; continue; }
            if (++i == argc) throw std::invalid_argument("missing value for " + key);
            const std::string value = argv[i];
            if (key == "--host") o.host = value;
            else if (key == "--port") o.port = static_cast<std::uint16_t>(integer(value, command == "server" ? 0 : 1, 65535));
            else if (key == "--log") o.log = value;
            else if (key == "--timeout-ms") o.timeout_ms = integer(value, 1, 60000);
            else if (key == "--count") o.count = integer(value, 1, 1000000);
            else if (key == "--message") o.message = value;
            else if (key == "--max-connections") o.max_connections = integer(value, 0, 1000000);
            else if (key == "--fault") o.fault = value;
            else if (key == "--fault-every") o.fault_every = integer(value, 1, 1000000);
            else if (key == "--delay-ms") o.delay_ms = integer(value, 0, 60000);
            else if (key == "--input") o.input = value;
        }
        if (command == "client") return nettrace::run_client(o);
        if (command == "server") {
            if (o.fault != "none" && o.fault != "delay" && o.fault != "drop" && o.fault != "corrupt" && o.fault != "disconnect") throw std::invalid_argument("invalid fault mode");
            if (o.fault == "delay" && !o.delay_ms) throw std::invalid_argument("delay mode needs positive --delay-ms");
            if (o.fault != "delay" && o.delay_ms) throw std::invalid_argument("--delay-ms requires --fault delay");
            return nettrace::run_server(o);
        }
        if (o.input.empty()) throw std::invalid_argument("analyze requires --input");
        std::ifstream input(o.input);
        if (!input) throw std::runtime_error("cannot open input: " + o.input);
        const auto summary = nettrace::analyze(input, &std::cerr);
        std::cout << summary.json();
        return o.strict && (summary.malformed_lines || summary.inconsistent_requests) ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 1; }
}
