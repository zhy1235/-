# NetTrace

[![NetTrace CI](https://github.com/zhy1235/-/actions/workflows/ci.yml/badge.svg)](https://github.com/zhy1235/-/actions/workflows/ci.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![MIT](https://img.shields.io/badge/license-MIT-green)

**轻量级 TCP 网络通信与故障诊断工具。** C++17 / Linux / Windows / CMake。

NetTrace 通过自定义二进制协议发送和回显消息，记录每次通信结果，并离线计算成功率、超时数和延迟分布。可以确定性地注入延迟、抑制响应、损坏响应及主动断开，帮助复现通信异常。

**English:** A small C++17 TCP diagnostic CLI with bounded length-prefixed framing, transaction deadlines, deterministic application fault injection, structured logs, and offline latency/outcome analysis. No third-party C++ dependencies. See the build commands and [protocol specification](docs/protocol.md).

## 功能

- TCP 客户端和串行服务端，支持多条消息、分段接收与连续帧。
- 24 字节协议头、网络字节序、64 KiB 负载上限、FNV-1a 校验和；校验响应类型、ID 和回显内容。
- 非阻塞 Socket + `select`，处理部分读写、EOF、错误和绝对截止时间。
- `delay` / `drop` / `corrupt` / `disconnect` 四种故障，每 N 条有效请求触发一次。
- 结构化 TSV 日志，记录时间、会话、角色、消息 ID、类型、事件和耗时。
- JSON 统计：成功率、超时、协议错误、断开、I/O 错误、未完成事务，以及成功事务的 min / mean / p50 / p95 / max 延迟。
- C++ 单元测试、Python 黑盒 TCP 集成测试和 Linux / Windows GitHub Actions。

`drop` 是应用程序不发送响应，**不模拟链路丢包或 TCP 重传**。这是开发与学习工具，未实现抓包、LTE 协议栈或生产级网络监控。

## 编译和测试

需要 C++17 编译器、CMake 3.16+；运行测试或演示还需要 Python 3.9+。C++ 程序没有第三方库依赖。

### Linux

Ubuntu / Debian 可安装 `build-essential cmake python3`，然后运行：

```bash
git clone https://github.com/zhy1235/-.git nettrace-cpp
cd nettrace-cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Windows

Visual Studio 2022 的 C++ 开发工具、CMake 和 Python：

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\nettrace.exe --help
```

MinGW + Ninja 可用 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`，程序位于 `build/nettrace.exe`。仅编译程序时可加 `-DBUILD_TESTING=OFF`。安装命令：`cmake --install build --prefix install`（多配置生成器加 `--config Release`）。

## 五分钟体验

以下 Linux 命令中的 `./build/nettrace`，在 Windows 按构建方式替换为对应的 `.exe` 路径。

终端 1 启动服务端：

```bash
./build/nettrace server --host 127.0.0.1 --port 9000 --log server.log
```

终端 2 发出 5 次请求并分析：

```bash
./build/nettrace client --port 9000 --count 5 --message "hello NetTrace" --log client.log
./build/nettrace analyze --input client.log --strict
```

使用 `Ctrl+C` 停止无限运行的服务端。也可设置 `--max-connections 5`，在处理 5 个连接后退出。`--port 0` 让系统选择端口，实际地址通过 `LISTENING 127.0.0.1:<port>` 输出。

### 一键故障演示

```bash
python3 scripts/demo.py --exe ./build/nettrace --output demo-output
```

Windows 示例：`python scripts/demo.py --exe build/Release/nettrace.exe --output demo-output`。

脚本依次运行正常通信、可接受延迟、延迟超时、响应抑制、响应损坏和断开，检查退出码，并生成独立日志、`summary.json` 和 `transcript.txt`。输出目录必须不存在，防止误覆盖历史演示。

查看 [实际演示记录](docs/demo-transcript.txt) 与 [示例日志](examples/sample.log)。示例日志为手工构造的固定测试数据，不是性能测试结果：

```bash
./build/nettrace analyze --input examples/sample.log --strict
```

预期 5 次尝试、2 次成功、1 次超时、1 次协议错误、1 次断开；成功率 `0.4`，成功事务平均耗时 `2000 us`。

### 手动复现故障

| 服务端选项 | 客户端选项 | 预期结果 |
|---|---|---|
| `--fault delay --delay-ms 50` | `--timeout-ms 1000` | 成功，耗时包含注入延迟 |
| `--fault delay --delay-ms 300` | `--timeout-ms 100` | 超时 |
| `--fault drop` | `--timeout-ms 100` | 超时；服务端记录 dropped |
| `--fault corrupt --fault-every 2` | `--count 4` | 第 2、4 次校验和错误 |
| `--fault disconnect` | 默认 | 对端关闭；通常记录 disconnected |

每次演示先停掉已有服务端，再启动带相应选项的新服务端。服务端默认单帧超时 1000 ms，演示响应抑制时客户端超时应更短。操作系统报告连接重置时会记录 `io_error`；正常 EOF 记录 `disconnected`。

## 命令行约定

完整参数见 `nettrace --help`，版本见 `nettrace --version`。只接受数字 IPv4 地址，不做 DNS 解析。

| 参数 | 默认值 / 范围 |
|---|---|
| `--host` | `127.0.0.1` |
| `--port` | `9000`；服务端 0–65535，客户端 1–65535 |
| `--timeout-ms` | `1000`；1–60000 |
| `--count` | 客户端 `1`；1–1000000 |
| `--message` | `hello NetTrace`；最多 65536 字节，允许空字符串 |
| `--log` | `client.log` / `server.log`；追加写入 |
| `--max-connections` | 服务端 `0`；0 为无限，最多 1000000 |
| `--fault` | `none`；可选 delay、drop、corrupt、disconnect |
| `--fault-every` | `1`；1–1000000；按有效请求的全局序号计数 |
| `--delay-ms` | `0`；delay 模式必须为 1–60000 |
| `analyze --input` | 必填日志路径 |
| `analyze --strict` | 发现坏行或不一致事务时返回 1，仍输出 JSON |

退出码：`0` 成功；`1` 参数、运行或日志验证错误；`2` 客户端至少一次事务失败。脚本调用故障案例时应显式接受退出码 2。

## 统计的含义

`sent` 表示一次**尝试**，包含连接建立失败，不能解释为对端已收到。只有服务端 `received` 事件可以证明应用层解码成功。客户端 `ok` 则表示收到并验证了完整回显响应。

分析器按 `(session, id)` 关联客户端事件，跨多次运行追加日志也不会因 ID 从 1 开始而混淆。每个有效事务必须有一条 `sent`，并有零或一条终态：

- 没有终态：`pending`，例如进程被中止；不会自动推断为超时。
- 重复 `sent`、重复终态、没有 `sent` 的孤立终态：计入 `inconsistent_requests`，整组从事务统计中排除。
- 坏行：报告行号并计入 `malformed_lines`，继续分析。
- 服务端事件：只计入 `events`，不会重复计入客户端成功率。

成功率为 `ok / sent`（范围 0–1，空日志为 0）。延迟只统计成功事务，包含连接、发送、服务端处理和响应接收；用单调时钟计时，单位为微秒。p50 / p95 使用 nearest-rank；没有成功样本时各延迟字段为 0。它不是专门测量持久连接 RTT 的基准程序。

## 结构与设计

```text
include/nettrace/      协议、Socket、日志、分析器接口
src/client/           事务发送、响应验证和结果日志
src/server/           串行服务端与确定性故障注入
src/protocol.cpp      字节序、长度限制、校验和、增量组帧
src/socket.cpp        跨平台 RAII Socket、截止时间与部分读写
src/log.cpp           日志编码与严格解析
src/analyzer.cpp      会话关联、异常计数和延迟分位数
tests/                C++ 单元测试及 Python 黑盒测试
scripts/demo.py       可重复演示
examples/             固定示例数据
docs/                 协议、架构、开发记录和实际演示输出
.github/workflows/    Linux / Windows CI
```

[架构与取舍](docs/architecture.md) · [协议与日志格式](docs/protocol.md) · [测试和开发记录](docs/development.md)

## 范围与限制

服务端一次处理一个连接，支持该流中的多条消息；CLI 客户端为每次事务新建连接，避免超时后的迟到响应被下一条请求误用。延迟注入会阻塞串行服务端，可能影响后续连接的等待时间。

仅支持 IPv4 TCP；未实现并发服务端、TLS、身份认证、自动重试、抓包、真实链路丢包、持久客户端连接、吞吐量基准和在线可视化。FNV-1a 用来检测示例中的内容损坏，不提供密码学认证。默认只监听回环地址，适合本机开发和受控实验。

日志不记录原始消息负载，只记录负载长度和错误说明；错误说明采用十六进制编码避免换行/制表符破坏格式。日志追加写入并逐条刷新，不支持多个进程同时写同一个文件；分析器在内存保存事务和成功延迟样本，不适合无界日志流。

本项目基于需求生成并通过自动化测试，未伪造分阶段的历史开发提交；如用于作品集，建议阅读实现、亲手复现测试并记录自己的后续改进。

## License

[MIT](LICENSE)
