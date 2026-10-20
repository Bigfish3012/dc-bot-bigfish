# dc-bot-bigfish

一个使用 C++17 和 [DPP](https://github.com/brainboxdotcc/DPP) 编写的 Discord 聊天机器人，通过 OpenAI API 生成回复，并支持文本、代码附件和可提取文字的 PDF。

目前已在 Raspberry Pi 5、Debian 13 上完成构建和核心测试。

## 功能

- 在指定频道进行聊天，按服务器、频道、用户隔离会话历史。
- 长回复通过 `response.txt` 附件发送；普通回复引用原消息。
- 支持 TXT、PDF，以及常见代码文件，扩展名不区分大小写。
- 对正文与附件提取文本一起进行内容审核；审核服务异常时暂停该次请求。
- 固定工作线程、有界队列、同会话串行处理，避免并发修改历史。
- 限制附件、合并文本、API 响应和历史内存占用；网络请求具有超时。
- PDF 提取使用独立临时文件与受限子进程，避免文件冲突及长时间解析。
- 已停用退役的 Bing Search API，原搜索频道会返回停用说明。

当前聊天模型为 `gpt-6-luna`，审核模型为 `omni-moderation-latest`。程序不显式设置 `max_completion_tokens` 或 `max_tokens`，不添加应用层 token 上限。

## 环境与依赖

目标平台为 Linux，要求：

- C++17 编译器和 CMake 3.22 或更新版本。
- DPP 开发库：包含 `dpp/dpp.h` 和 `libdpp`，请按 [DPP 官方文档](https://dpp.dev/) 安装。
- libcurl 7.85 或更新版本，且启用异步 DNS。
- `pdftotext`：由 `poppler-utils` 提供，路径 `/usr/bin/pdftotext`。
- `prlimit`：由 `util-linux` 提供，路径 `/usr/bin/prlimit`。
- nlohmann/json：CMake 首次配置时通过 FetchContent 获取 v3.11.2，需要网络。

Debian / Raspberry Pi OS 可先安装以下通用依赖，再安装 DPP：

```bash
sudo apt update
sudo apt install build-essential cmake git pkg-config libcurl4-openssl-dev poppler-utils util-linux
```

使用 Makefile 时，还需让编译器能找到 nlohmann/json 头文件，例如安装 `nlohmann-json3-dev`。

## 配置

在项目根目录创建 `config.json`：

```json
{
  "BOT_TOKEN": "YOUR_DISCORD_BOT_TOKEN",
  "OPENAI_API_KEY": "YOUR_OPENAI_API_KEY"
}
```

`BING_API_KEY` 已不是必填项。`config.json` 已被 `.gitignore` 排除，不要提交实际密钥。

程序当前从工作目录的 `../config.json` 读取配置，因此运行时应进入 `build/` 或 `src/`，而不是直接在仓库根目录启动。

### Discord 设置

1. 创建 Discord Application 和 Bot，取得 Bot Token 并邀请到服务器。
2. 在 Developer Portal 的 Bot 页面启用 **Message Content Intent**。
3. 为目标频道提供查看频道、发送消息、读取历史及发送附件等权限。删除未通过审核的消息还需要 **Manage Messages**。
4. 修改 `src/main.cpp` 中的频道列表并重新编译：
   - `GPT_CHANNEL_IDS`：聊天频道。
   - `BING_CHANNEL_IDS`：已停用的搜索频道。
   - `STARTUP_CHANNEL_IDS`：上线及交互式退出通知频道。

仓库内的频道 ID 是现有部署的值，部署到自己的服务器时需要替换。机器人忽略其他机器人以及包含用户提及的消息。

源码保留了部分 slash command 处理函数，但自动注册代码当前被注释；默认使用指定频道中的普通消息聊天。

## 编译与启动

### CMake（推荐）

```bash
git clone https://github.com/Bigfish3012/dc-bot-bigfish.git
cd dc-bot-bigfish
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
cd build
./discord-bot
```

首次运行前，请先完成根目录 `config.json` 和频道 ID 配置。使用交互终端时，可输入 `quit` 并确认退出。

### Makefile

在仓库根目录执行：

```bash
make -C src -j2
cd src
./bot
```

Makefile 编译 `main.cpp`、`helper.cpp`、`pdf_extract.cpp`，链接 DPP、CURL 和 pthread，自动生成 `.d` 头文件依赖。

## 后台运行与更新

可通过 systemd 管理。仓库没有自动安装或修改系统服务；现有树莓派部署使用 `dcbot.service`，关键设置为：

```ini
WorkingDirectory=/home/taotao/dc-bot-bigfish/build
ExecStart=/home/taotao/dc-bot-bigfish/build/discord-bot
```

手动更新并重新编译后，再重启已有服务：

```bash
cd ~/dc-bot-bigfish
git pull --ff-only
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
sudo systemctl restart dcbot.service
systemctl status dcbot.service --no-pager
```

**只执行 `git pull` 或重启服务不会自动编译新程序。** 使用 Makefile 生成的 `src/bot` 也不会更新 systemd 当前使用的 `build/discord-bot`。

查看日志：

```bash
journalctl -u dcbot.service -n 100 --no-pager
journalctl -u dcbot.service -f
```

## 当前默认限制

参数集中在 `include/runtime_limits.h`，修改后重新编译。

| 项目 | 默认值 |
| --- | --- |
| 工作线程 | 2 |
| 执行中与排队任务总量 | 32 |
| 每个会话的执行中与排队任务 | 4 |
| 最长排队时间 | 2 分钟（定义在 `main.cpp`） |
| 附件数量 | 5 |
| 单附件 / 下载总量 | 2 MiB / 4 MiB |
| 正文与附件合并文本 | 64 KiB |
| 聊天 / 审核响应体 | 1 MiB / 128 KiB |
| 会话历史 | 最多 128 个会话，每会话 15 轮且不超过 256 KiB |
| 连接 / 下载 / 审核 / 聊天超时 | 10 / 30 / 20 / 180 秒 |
| PDF 解析 | 墙钟 15 秒，CPU 10 秒，地址空间 256 MiB |

仅附件下载启用低速保护：低于 32 字节/秒持续 15 秒会中止。非流式聊天和审核请求等待期间可能没有响应数据，使用各自总超时。

回复超过 1900 个长度单位时转为附件：中文计 1，非 BMP 字符保守计 2。附件原文仅用于当次请求；历史保存用户正文或纯附件占位说明，以及模型回复。模型回复中引用的附件片段仍会随历史保留。

## 故障排查

### “处理消息时发生错误，请稍后重试。”

这是 `src/main.cpp` 异常捕获分支中的通用提示，不是模型生成的回答。查看同一时间的 `[ERROR] Message task failed` 获取具体原因。

已知的通信失败、空正文、模型长度限制、过滤或拒绝会返回各自的具体提示。

### 长请求失败，短聊天正常

先核对正在运行的二进制是否重新编译。此前 4096 token 预算可能全部被推理消耗，返回 `finish_reason=length` 且正文为空；当前版本已移除这个显式预算。

响应日志记录 `finish_reason`、`completion_tokens`、`reasoning_tokens` 和 `text_bytes`，不输出模型正文或推理内容。`finish_reason=length` 的不完整输出不会作为完整代码交付或加入历史。

### HTTP 超时

`HTTP request failed` 日志包含操作类型、HTTP 状态、CURL 错误码及 DNS、连接、TLS、首字节、总耗时。`status=0` 代表未取得 HTTP 状态码，需结合其余字段判断原因。

### PDF 提取失败

确认上述两个工具存在，PDF 不是纯扫描图片，且没有超过大小、时间、内存或文本长度限制。当前没有 OCR 功能。

## 测试

在项目根目录运行基础测试：

```bash
mkdir -p build/local-checks
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread tests/runtime_tests.cpp -o build/local-checks/runtime_tests
./build/local-checks/runtime_tests
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror tests/text_tests.cpp -o build/local-checks/text_tests
./build/local-checks/text_tests
```

覆盖接收上限、整数溢出、同会话顺序、跨会话并行、过载拒绝、异常恢复、800 任务并发检查及中英文/emoji 边界。

完成 CMake 构建后，可运行离线 API 响应解析测试，不调用 OpenAI：

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread \
  -Ibuild/_deps/nlohmann_json-src/include \
  tests/chat_response_tests.cpp \
  build/CMakeFiles/discord-bot.dir/src/helper.cpp.o \
  -ldpp -lcurl -o build/local-checks/chat_response_tests
./build/local-checks/chat_response_tests
```

另有 `tests/pdf_tests.cpp`，用于 Linux 上的 PDF 无效输入、大小限制和并发隔离测试。

## 目录

```text
include/             接口、队列、文本工具与资源限制
src/main.cpp         Discord 事件、任务调度、审核和会话管理
src/helper.cpp       配置读取、HTTP 请求与 API 响应解析
src/pdf_extract.cpp  PDF 子进程与临时文件管理
src/Makefile         独立 Make 构建
CMakeLists.txt       推荐构建入口
tests/               并发、文本、响应解析和 PDF 测试
HARDENING.md         稳定性改动与验证记录
```

会话历史仅存于内存，重启后清空。当前未实现专门的 SIGTERM 收尾处理；相关细节见 `HARDENING.md`。
