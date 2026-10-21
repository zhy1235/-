#include "nettrace/app.hpp"
#include "nettrace/log.hpp"
#include "nettrace/socket.hpp"
#include <iostream>
#include <thread>

namespace nettrace {
int run_server(const Options& o) {
    Logger log(o.log, "server");
    auto listener = listen_tcp(o.host, o.port);
    std::cout << "LISTENING " << o.host << ':' << local_port(listener) << std::endl;
    std::uint64_t requests = 0;
    for (int connections = 0; !o.max_connections || connections < o.max_connections;) {
        Socket socket;
        try { socket = accept_tcp(listener, after_ms(o.timeout_ms)); }
        catch (const Timeout&) { continue; }
        ++connections;
        // Serial server, multiple frames per accepted stream; idle/frame timeout is bounded.
        for (;;) {
            std::uint64_t id = 0;
            try {
                auto request = receive_message(socket, after_ms(o.timeout_ms));
                id = request.id;
                if (request.type != MessageType::request) throw ProtocolError("server expects request type");
                log.write(id, "received", 1, request.payload.size());
                const bool fault = ++requests % static_cast<std::uint64_t>(o.fault_every) == 0;
                if (fault && o.fault == "disconnect") { log.write(id, "disconnected", 1, 0, 0, "injected close"); break; }
                if (fault && o.fault == "drop") { log.write(id, "dropped", 1, 0, 0, "application response suppressed"); continue; }
                if (fault && o.fault == "delay") {
                    log.write(id, "delayed", 2, 0, 0, std::to_string(o.delay_ms));
                    std::this_thread::sleep_for(std::chrono::milliseconds(o.delay_ms));
                }
                auto response = encode({MessageType::response, id, request.payload});
                if (fault && o.fault == "corrupt") { response[20] ^= 1; log.write(id, "corrupted", 2, request.payload.size(), 0, "injected checksum error"); }
                send_all(socket, response, after_ms(o.timeout_ms));
                log.write(id, "responded", 2, request.payload.size());
            } catch (const Timeout& e) { log.write(id, "timeout", 0, 0, 0, e.what()); break; }
              catch (const Disconnected& e) { log.write(id, "disconnected", 0, 0, 0, e.what()); break; }
              catch (const ProtocolError& e) { log.write(id, "protocol_error", 0, 0, 0, e.what()); break; }
              catch (const IoError& e) { log.write(id, "io_error", 0, 0, 0, e.what()); break; }
        }
    }
    return 0;
}
}
