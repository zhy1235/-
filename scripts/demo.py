"""Run six real TCP scenarios and retain logs/JSON/transcript; Python stdlib only."""
import argparse
import concurrent.futures
import json
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("demo-output"))
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=False)
    transcript = ["NetTrace - real loopback TCP demo (timings depend on this machine)"]
    summaries = {}
    cases = [
        ("normal", [], 1000, "ok"),
        ("delay-success", ["--fault", "delay", "--delay-ms", "40"], 1000, "ok"),
        ("delay-timeout", ["--fault", "delay", "--delay-ms", "250"], 80, "timeout"),
        ("drop", ["--fault", "drop"], 80, "timeout"),
        ("corrupt", ["--fault", "corrupt"], 1000, "protocol_error"),
        ("disconnect", ["--fault", "disconnect"], 1000, "disconnected"),
    ]
    for name, faults, timeout, expected in cases:
        folder = args.output / name
        folder.mkdir()
        server_args = [str(exe), "server", "--port", "0", "--max-connections", "1", "--timeout-ms", "1500", "--log", str(folder / "server.log"), *faults]
        proc = subprocess.Popen(server_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
                future = pool.submit(proc.stdout.readline)
                try:
                    ready = future.result(timeout=5).strip()
                except concurrent.futures.TimeoutError:
                    proc.kill()
                    raise RuntimeError("server startup deadline expired")
            if not ready.startswith("LISTENING 127.0.0.1:"):
                raise RuntimeError(ready + proc.stderr.read())
            port = ready.rsplit(":", 1)[1]
            result = subprocess.run([str(exe), "client", "--port", port, "--message", "hello NetTrace", "--timeout-ms", str(timeout), "--log", str(folder / "client.log")], capture_output=True, text=True, timeout=5)
            if result.returncode != (0 if expected == "ok" else 2) or f"status={expected} " not in result.stdout:
                raise RuntimeError(result.stdout + result.stderr)
            if proc.wait(timeout=5) != 0:
                raise RuntimeError(proc.stderr.read())
            analysis = subprocess.run([str(exe), "analyze", "--input", str(folder / "client.log"), "--strict"], capture_output=True, text=True, timeout=5, check=True)
            summaries[name] = json.loads(analysis.stdout)
            transcript += ["", f"[{name}]", "server options: " + " ".join(faults or ["--fault", "none"]), ready, result.stdout.strip(), analysis.stdout.strip()]
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.communicate(timeout=3)
    (args.output / "summary.json").write_text(json.dumps(summaries, indent=2) + "\n", encoding="utf-8")
    text = "\n".join(transcript) + "\n"
    (args.output / "transcript.txt").write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
