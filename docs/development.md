# 验证与开发记录

## 验证方法

`ctest --test-dir build --output-on-failure` 运行两个测试目标：

1. `unit`：C++ 测试程序。检查二进制负载往返、大端字段、每一个切分位置、单字节分片、多帧合并、最大负载/最大 ID、空帧、坏头、校验和、截断/超长消息、固定种子的随机分片，以及日志严格解析、会话隔离、重复/孤立事件、分位数和空输入。
2. `integration`：Python 标准库驱动实际可执行程序。覆盖多连接回显、空负载、延迟成功、延迟超时、响应抑制、每两条损坏一次、主动断开、64 KiB 二进制帧、多帧同连接、超长头立即拒绝、中途 EOF、错误消息类型、单帧截止时间、错误 response ID/负载、慢速字节流、严格日志检查与 CLI 参数验证。

测试不依赖固定端口。服务端绑定端口 0，打印 LISTENING 后测试才连接；子进程和网络读取有超时，失败后清理子进程。Python 使用独立编码逻辑验证线上字节。C++ 测试使用显式检查，在 Release 构建关闭 assert 时仍有效。

本地 Windows MinGW GCC 16.1.0 / CMake 4.4.1 / Python 3.12.14 的 Debug 和 Release 验证已通过；最终 TCP 集成测试包含 18 个测试方法。CI 配置覆盖 Linux Debug（ASan+UBSan）、Linux Release 和 Windows MSVC Release，实际远端结果以 Actions 页面为准。

最终本机验证：Debug 和 Release 两个构建的 CTest 均为 2/2 目标通过；C++ 测试程序报告 `PASS 349 checks`；Python 集成测试为 18/18。六种真实 TCP 演示见 [transcript](demo-transcript.txt) 和 [JSON](demo-summary.json)。演示耗时只是该次运行的观测值，没有性能承诺。

Linux 本地启用 sanitizer 的命令：

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DNETTRACE_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## 真实处理的工程问题

### TCP 不保留消息边界

头和 payload 使用精确读取循环，每次只请求当前部分剩余的字节数。完整帧只有通过头字段检查、完整长度验证与校验和验证后才交给业务处理。测试覆盖每个切分点和真实 Socket 上的合并帧。

### 相同消息 ID 在多次运行中复用

客户端每次从 1 开始编号，日志又是追加写入。使用随机 session 加 id 关联事务，防止独立运行的结果互相覆盖；重复终态和孤立结果不会悄悄污染统计。

### Windows 非阻塞 connect 的错误通知

Windows `select` 可能通过异常集合报告连接失败。等待函数同时监控异常集合，connect 后检查 SO_ERROR；所有部分读写都维持同一绝对截止时间。

演示中还观察到 Windows 的 select 定时器可能略早于指定微秒数返回。等待函数在 select 超时后重新检查单调时钟，防止过早报告截止时间；慢速响应测试同时检查不会在截止时间前退出。

### 本机验证环境限制

第一轮集成测试遇到 Windows 沙箱临时目录对子进程不可写，随后指定工作区内的 TEMP/TMP。第二轮发现沙箱禁止回环 TCP（Socket 错误 10013）；在正常权限环境运行同一测试后全部通过。没有为通过测试跳过网络断言或改成模拟成功。

如在限制网络/临时目录的沙箱运行，应允许受控本机回环访问并提供可写的临时目录；常规开发机和 GitHub runner 无需这些环境调整。

## 后续方向

尚未实现的扩展：并发连接、公平调度、trace ID 跨角色关联、持久连接客户端、TLS、自动重试策略、流式大日志统计和独立吞吐量基准。当前没有发布吞吐量或覆盖率百分比，不从少量本机演示推导生产性能。

本仓库从完整实现开始发布，未将同一次生成伪装成数周开发经历。后续学习可以从修改协议、增加故障模式和运行测试开始。
