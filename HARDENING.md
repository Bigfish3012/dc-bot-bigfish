# 本地稳定性改动（审查项目 1–7）

本次只修改桌面项目源码，没有连接、部署或修改树莓派。

- `KeyedExecutor`：2 个工作线程，全局最多 32 个排队或执行任务；每个会话最多 4 个，按接收顺序串行执行。过载立即提示，等待超过 2 分钟的任务过期。停止时取消排队任务，等待在途任务收尾。
- 会话键：服务器、频道、用户。历史容器只在复制或提交时短暂加锁，网络请求期间不持锁。最多保留 128 个会话，每会话 15 轮且不超过 256 KiB；容量满时淘汰最近最少使用的会话。
- HTTP：连接 10 秒，下载 30 秒，审核 20 秒，聊天 90 秒；附件下载低于 32 字节/秒持续 15 秒会中止；非流式聊天和审核请求不使用低速中止，仅使用连接和总超时。检查 HTTP 状态码，只接受 HTTPS，不跟随重定向。每个请求独立持有 CURL 句柄，启动工作线程前初始化 libcurl。
- 输入：最多 5 个附件，单文件 2 MiB，下载总计 4 MiB，合并文本 64 KiB。真实接收字节受回调限制，不依赖附件声明大小或 Content-Length。API 响应最多 1 MiB，审核响应最多 128 KiB。
- PDF：`mkstemp` 独立临时文件，0600 权限，自动清理；用 `posix_spawn` 启动 `/usr/bin/prlimit`，再执行 `/usr/bin/pdftotext`，没有 shell。墙钟 15 秒、CPU 10 秒、地址空间 256 MiB，禁止 core dump，提取文本最多 64 KiB。超时、超限、异常时终止并回收子进程。
- 审核：使用 `omni-moderation-latest`，审核包括附件的合并文本；区分通过、违规、服务错误。服务错误时提示并停止本次请求，不删除消息、不调用聊天模型。
- 搜索：停用已退役的 Bing Search API；原搜索频道返回停用说明。`BING_API_KEY` 不再是必需配置。
- 长回复直接从内存发送附件，正文仍保存到历史；HTTP/解析失败不会作为模型回答保存。

默认参数集中在 `include/runtime_limits.h`，调整后需要重新编译。聊天模型保留 `gpt-6-luna`，移除 `temperature`，设置 `max_completion_tokens=4096`。

## 构建要求

目标仍为 Linux / C++17。使用 CMake；新增 `src/pdf_extract.cpp` 已加入构建，线程依赖为 `Threads::Threads`。要求 libcurl >= 7.85，且具备异步 DNS 功能，否则启动时报告错误，避免多线程请求无法限制同步 DNS 等待。PDF 功能依赖 Debian 的 `poppler-utils` 和 `util-linux`，程序路径分别为 `/usr/bin/pdftotext`、`/usr/bin/prlimit`。任一工具不可用时返回解析失败，自动清理文件。

## 本地验证

Windows 上以 GCC 13.2、C++17、`-Wall -Wextra -Wpedantic -Werror -pthread` 编译并运行 `tests/runtime_tests.cpp`，通过：接收上限、乘法溢出、同会话 FIFO、跨会话并行、队列容量、停止取消、异常恢复、800 任务多生产者并发检查。

当前 Windows 没有完整 Linux/DPP/CURL 开发依赖，未完成整个机器人构建、PDF 集成测试或在线 Discord/OpenAI 测试；没有使用真实密钥发请求。

以下为目标兼容的本地 Linux 环境中的验证命令（本次未在树莓派执行）：

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread tests/runtime_tests.cpp -o runtime_tests
./runtime_tests
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread tests/pdf_tests.cpp src/pdf_extract.cpp -o pdf_tests
./pdf_tests
cmake -S . -B build-check -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-check -j2
```

本次没有新增 SIGTERM 处理或服务配置；这些属于原审查第 15 项。进程被强制终止、断电时无法保证执行 C++ 清理代码。

## 后续修复

- `src/Makefile` 编译三个源文件，链接 DPP、CURL 和 pthread；生成并包含 `.d` 头文件依赖，使用 LF 换行和真实 Tab。依赖生成参数独立放在 `DEPFLAGS`，覆盖优化参数时仍保留头文件跟踪。可在项目根目录运行 `make -C src`，仍要求已安装 DPP/CURL/nlohmann JSON 开发头文件。
- 回复不再按 UTF-8 字节数决定是否发送附件。中文计 1 个单位，非 BMP 字符保守计 2 个单位，超过 1900 单位发送附件。
- 扩展名统一转 ASCII 小写，支持 `REPORT.PDF`、`a.TXT` 等。
- 历史的用户部分只存原消息正文；纯附件消息存简短占位说明。附件原文仍参与当次审核和生成，但后续不再原样附带重发。模型回答照常保留，因此回答中引用的附件片段仍可能出现在上下文中。
- 审核拦截提示作为普通频道消息发送，不引用随后会被删除的原消息。
- 新增独立文本边界测试 `tests/text_tests.cpp`，可用 `g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror tests/text_tests.cpp -o text_tests` 编译。

## 超时修复

此前把下载用低速保护也应用于非流式 API 请求，可能在等待模型生成时提前中止。现改为仅附件下载启用 15 秒低速保护，聊天总超时仍为 90 秒、审核总超时仍为 20 秒、连接超时仍为 10 秒。

HTTP 失败日志增加 operation、curl_code 及 total_s / dns_s / connect_s / tls_s / first_byte_s，以便区分耗时阶段；不记录凭据、正文或附件 URL。时间为请求开始后的累计耗时，零值可能表示该阶段未完成，复用连接等情况也需要结合上下文判断。该修复只经过本地差异检查，未在真实 API 或树莓派复现验证。
