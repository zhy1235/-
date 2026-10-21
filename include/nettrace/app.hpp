#pragma once
#include <cstdint>
#include <string>

namespace nettrace {
struct Options {
    std::string host = "127.0.0.1", log = "nettrace.log", message = "hello NetTrace", input;
    std::uint16_t port = 9000;
    int count = 1, timeout_ms = 1000, max_connections = 0, delay_ms = 0, fault_every = 1;
    std::string fault = "none";
    bool strict = false;
};
int run_client(const Options& options);
int run_server(const Options& options);
}
