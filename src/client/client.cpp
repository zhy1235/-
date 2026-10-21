#include "nettrace/app.hpp"
#include "nettrace/log.hpp"
#include "nettrace/socket.hpp"
#include <iostream>

namespace nettrace {
int run_client(const Options& o) {
    if (o.message.size() > max_payload) throw std::invalid_argument("message exceeds 65536 bytes");
    Logger log(o.log, "client");
    int failures = 0;
    for (int i = 1; i <= o.count; ++i) {
        const auto id = static_cast<std::uint64_t>(i);
        const auto start = Clock::now();
        const auto deadline = after_ms(o.timeout_ms);
        const auto elapsed = [&]() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count()); };
        std::string event = "ok", detail;
        // 'sent' denotes an attempted transaction, including connect failures.
        log.write(id, "sent", 1, o.message.size());
        try {
            auto socket = connect_tcp(o.host, o.port, deadline);
            send_all(socket, encode({MessageType::request, id, o.message}), deadline);
            const auto reply = receive_message(socket, deadline);
            if (reply.type != MessageType::response || reply.id != id || reply.payload != o.message) throw ProtocolError("response type, id or echo payload mismatch");
        } catch (const Timeout& e) { event = "timeout"; detail = e.what(); }
          catch (const ProtocolError& e) { event = "protocol_error"; detail = e.what(); }
          catch (const Disconnected& e) { event = "disconnected"; detail = e.what(); }
          catch (const IoError& e) { event = "io_error"; detail = e.what(); }
        const auto duration = elapsed();
        log.write(id, event, 2, 0, duration, detail);
        std::cout << "id=" << id << " status=" << event << " elapsed_us=" << duration;
        if (!detail.empty()) std::cout << " detail=" << detail;
        std::cout << '\n';
        if (event != "ok") ++failures;
    }
    return failures ? 2 : 0;
}
}
