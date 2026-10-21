# NetTrace v1 协议与日志格式

## 二进制帧

所有多字节整数使用大端（网络字节序）。头部固定 24 字节，无结构体内存直接序列化，无填充对齐依赖。负载是任意字节，UTF-8 文本只是 CLI 的一种用法。

| 偏移 | 字节数 | 字段 | 约束 |
|---|---|---|---|
| 0 | 4 | magic | ASCII `NTRC` |
| 4 | 1 | version | `1` |
| 5 | 1 | type | `1` request，`2` response |
| 6 | 2 | reserved | 必须为零 |
| 8 | 4 | payload_length | 0–65536，不包含头部 |
| 12 | 8 | message_id | 无符号 64 位整数 |
| 20 | 4 | checksum | FNV-1a 32 位校验和 |
| 24 | N | payload | N 必须等于 payload_length |

校验和的输入是**头部字节 0–19，随后连接 payload**，跳过字节 20–23。初值 `2166136261`，对每个字节执行 `hash = (hash XOR byte) * 16777619`，每步模 2^32；结果按大端存入 checksum。

服务端只接收 request，回显 response 必须携带相同 ID 和原始负载。客户端验证 response 类型、ID、负载、校验和以及完整长度。最大帧为 65560 字节，包含空负载的帧仍有完整头部。

接收方先验证头，再分配 payload。坏 magic、版本不支持、非法类型、非零保留位、超长负载或校验和不匹配都视为协议错误，关闭当前连接。接收流中途关闭属于 EOF，而非可恢复的短消息。无自动重传、协商、压缩、认证或错误响应帧。

## 独立参考实现

`tests/integration.py` 使用 Python `struct.pack('!4sBBHIQ', ...)` 和独立的 FNV 循环构造帧，不调用 C++ 编码器；将真实发送与 Python 预期字节比较，减少只测试同一实现自洽性的风险。

## TSV 日志

每行严格包含 9 列，由单个 TAB 分隔，没有表头。行尾允许 LF 或 CRLF。

```text
timestamp_ms<TAB>session<TAB>role<TAB>id<TAB>event<TAB>type<TAB>bytes<TAB>elapsed_us<TAB>detail_hex
```

| 列 | 格式 / 含义 |
|---|---|
| timestamp_ms | UNIX epoch 毫秒，无符号十进制 |
| session | 1–64 个 ASCII 字母、数字、下划线或连字符；生成器使用随机十六进制字符串 |
| role | client 或 server |
| id | 消息 ID，无符号十进制；读帧失败无法确定 ID 时为 0 |
| event | 见下表 |
| type | 0 未知；1 request；2 response |
| bytes | 逻辑负载字节数，0–65536；非线速字节计数 |
| elapsed_us | 客户端终态记录的事务耗时，其他事件通常为 0 |
| detail_hex | 错误/注入说明的逐字节小写十六进制；可以为空，因此行尾 TAB 不可删除 |

客户端事件：`sent`, `ok`, `timeout`, `protocol_error`, `disconnected`, `io_error`。

服务端事件：`received`, `responded`, `dropped`, `delayed`, `corrupted`, `disconnected`, `protocol_error`, `timeout`, `io_error`。

客户端 `sent.bytes` 为计划发送的负载长度；成功终态的 bytes 目前为 0。服务端 received/responded/corrupted 的 bytes 为负载长度。上述字段不用于计算吞吐量。

数字解析拒绝负数、正号、空格、后缀字符和 uint64 溢出。分析器只对客户端按 `(session,id)` 汇总事务；服务端正常连接关闭也会记录 disconnected，因此不能把所有角色的 disconnected 事件直接当成失败事务数。
