"""Black-box TCP tests; standard library only; no fixed ports or startup sleeps."""
import concurrent.futures
import contextlib
import json
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

EXE = str(pathlib.Path(sys.argv.pop(1)).resolve())


def run(*args, expected=0):
    result = subprocess.run([EXE, *map(str, args)], capture_output=True, text=True, timeout=10)
    if result.returncode != expected:
        raise AssertionError(f"{args}: exit {result.returncode}, expected {expected}\n{result.stdout}\n{result.stderr}")
    return result


@contextlib.contextmanager
def server(folder, connections=1, **options):
    args = [EXE, "server", "--port", "0", "--max-connections", str(connections),
            "--timeout-ms", "1500", "--log", str(folder / "server.log")]
    for key, value in options.items():
        args += ["--" + key.replace("_", "-"), str(value)]
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(proc.stdout.readline)
            try:
                line = future.result(timeout=5)
            except concurrent.futures.TimeoutError:
                proc.kill()
                raise AssertionError("server readiness timed out")
        if not line.startswith("LISTENING 127.0.0.1:"):
            raise AssertionError(f"server failed: {line} {proc.stderr.read()}")
        yield int(line.rsplit(":", 1)[1])
        code = proc.wait(timeout=8)
        if code:
            raise AssertionError(proc.stderr.read())
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.communicate(timeout=3)


def frame(payload=b"hello", ident=1, kind=1):
    prefix = struct.pack("!4sBBHIQ", b"NTRC", 1, kind, 0, len(payload), ident)
    checksum = 2166136261
    for byte in prefix + payload:
        checksum = ((checksum ^ byte) * 16777619) & 0xFFFFFFFF
    return prefix + struct.pack("!I", checksum) + payload


def exact(sock, length):
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise AssertionError("unexpected EOF")
        data += chunk
    return data


class Integration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = pathlib.Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def client(self, port, expected=0, **options):
        args = ["client", "--port", port, "--log", self.folder / "client.log"]
        for key, value in options.items():
            args += ["--" + key.replace("_", "-"), value]
        return run(*args, expected=expected)

    def summary(self, name="client.log", strict=False, expected=0):
        args = ["analyze", "--input", self.folder / name]
        if strict:
            args += ["--strict"]
        return json.loads(run(*args, expected=expected).stdout)

    def test_echo_reconnect_and_statistics(self):
        with server(self.folder, connections=4) as port:
            self.client(port, count=3, message="hello\tNetTrace\n")
            self.client(port, message="")
        s = self.summary(strict=True)
        self.assertEqual((s["sent"], s["ok"], s["success_rate"]), (4, 4, 1.0))
        self.assertGreaterEqual(s["latency_us"]["p95"], s["latency_us"]["p50"])

    def test_delay_success(self):
        with server(self.folder, fault="delay", delay_ms=30) as port:
            self.client(port)
        self.assertGreaterEqual(self.summary()["latency_us"]["min"], 25000)

    def test_delay_timeout(self):
        with server(self.folder, fault="delay", delay_ms=200) as port:
            self.client(port, expected=2, timeout_ms=60)
        self.assertEqual(self.summary()["timeout"], 1)

    def test_drop(self):
        with server(self.folder, fault="drop") as port:
            self.client(port, expected=2, timeout_ms=80)
        self.assertEqual(self.summary()["timeout"], 1)
        self.assertEqual(self.summary("server.log")["events"]["server.dropped"], 1)

    def test_corrupt_every_second(self):
        with server(self.folder, connections=4, fault="corrupt", fault_every=2) as port:
            self.client(port, count=4, expected=2)
        s = self.summary()
        self.assertEqual((s["ok"], s["protocol_errors"]), (2, 2))

    def test_disconnect(self):
        with server(self.folder, fault="disconnect") as port:
            self.client(port, expected=2)
        self.assertEqual(self.summary()["disconnected"], 1)

    def test_fragmented_and_coalesced_binary_frames(self):
        payload = bytes(range(256)) * 256
        a, b = frame(payload, 11), frame(b"", 12)
        with server(self.folder) as port:
            with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
                for byte in a[:24]:
                    sock.sendall(bytes([byte]))
                sock.sendall(a[24:] + b)
                self.assertEqual(exact(sock, len(a)), frame(payload, 11, 2))
                self.assertEqual(exact(sock, len(b)), frame(b"", 12, 2))

    def test_oversize_header_rejected_without_payload(self):
        bad = bytearray(frame())
        bad[8:12] = struct.pack("!I", 65537)
        with server(self.folder) as port:
            with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
                sock.sendall(bad[:24])
                self.assertEqual(sock.recv(1), b"")
        self.assertEqual(self.summary("server.log")["events"]["server.protocol_error"], 1)

    def test_partial_frame_eof(self):
        with server(self.folder) as port:
            with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
                sock.sendall(frame()[:-2])
        self.assertEqual(self.summary("server.log")["events"]["server.disconnected"], 1)

    def test_bad_request_type(self):
        with server(self.folder) as port:
            with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
                sock.sendall(frame(kind=2))
                self.assertEqual(sock.recv(1), b"")
        self.assertEqual(self.summary("server.log")["events"]["server.protocol_error"], 1)

    def test_server_frame_deadline(self):
        with server(self.folder) as port:
            with socket.create_connection(("127.0.0.1", port), timeout=4) as sock:
                sock.sendall(b"N")
                self.assertEqual(sock.recv(1), b"")
        self.assertEqual(self.summary("server.log")["events"]["server.timeout"], 1)

    def fake_peer(self, respond):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(5)
            errors = []

            def peer():
                try:
                    with listener.accept()[0] as sock:
                        sock.settimeout(3)
                        header = exact(sock, 24)
                        body = exact(sock, struct.unpack("!I", header[8:12])[0])
                        respond(sock, header, body)
                except Exception as exc:
                    errors.append(exc)

            thread = threading.Thread(target=peer, daemon=True)
            thread.start()
            try:
                result = self.client(listener.getsockname()[1], expected=2, timeout_ms=150)
            finally:
                thread.join(timeout=4)
            self.assertFalse(thread.is_alive())
            if errors:
                raise errors[0]
            return result

    def test_wrong_response_id(self):
        self.fake_peer(lambda sock, _h, body: sock.sendall(frame(body, 999, 2)))
        self.assertEqual(self.summary()["protocol_errors"], 1)

    def test_partial_response_eof(self):
        self.fake_peer(lambda sock, _h, body: sock.sendall(frame(body, 1, 2)[:-1]))
        self.assertEqual(self.summary()["disconnected"], 1)

    def test_connection_refused(self):
        # Bound but not listening: reserve the port without a free-port race.
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            self.client(reserved.getsockname()[1], expected=2, timeout_ms=5000)
        self.assertEqual(self.summary()["io_errors"], 1)

    def test_wrong_echo_payload(self):
        self.fake_peer(lambda sock, _h, _b: sock.sendall(frame(b"wrong", 1, 2)))
        self.assertEqual(self.summary()["protocol_errors"], 1)

    def test_slow_drip_does_not_reset_deadline(self):
        def drip(sock, _h, body):
            for byte in frame(body, 1, 2):
                try:
                    sock.sendall(bytes([byte]))
                except OSError:
                    break
                time.sleep(0.025)
        start = time.monotonic()
        result = self.fake_peer(drip)
        self.assertLess(time.monotonic() - start, 1.5)
        elapsed = int(result.stdout.split("elapsed_us=", 1)[1].split()[0])
        self.assertGreaterEqual(elapsed, 150000)
        self.assertEqual(self.summary()["timeout"], 1)

    def test_invalid_log_and_strict_mode(self):
        (self.folder / "bad.log").write_text("broken\n", encoding="utf-8")
        self.assertEqual(self.summary("bad.log")["malformed_lines"], 1)
        self.summary("bad.log", strict=True, expected=1)

    def test_cli_validation(self):
        for args in [("server", "--port", "-1"), ("client", "--count", "0"),
                     ("client", "--port", "65536"), ("client", "--port", "0"),
                     ("client", "--count", "1junk"), ("server", "--fault", "oops"),
                     ("server", "--fault", "delay"), ("analyze",),
                     ("client", "--log"), ("analyze", "--wat"),
                     ("client", "--count", "1", "--count", "1")]:
            run(*args, expected=1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
