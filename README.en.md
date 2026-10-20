# dc-bot-bigfish

[简体中文](README.md) | **English**

A Discord chatbot written in C++17 with [DPP](https://github.com/brainboxdotcc/DPP). It uses the OpenAI API to generate replies and supports text, images, code attachments, and PDFs with extractable text.

The project has been built and its core tests have passed on Raspberry Pi 5 with Debian 13.

## Features

- Chat in configured channels, with conversation history isolated by server, channel, and user.
- Send long replies as `response.txt` attachments; regular replies reference the original message.
- Understand PNG, JPEG, and WebP images, and read TXT, PDF, and common source code files. File extensions are case-insensitive.
- Generate images or edit images attached to the current message, returning the result as a PNG attachment.
- Moderate message text together with text extracted from attachments; stop the request if the moderation service fails.
- Use a fixed worker pool, a bounded queue, and sequential processing within each conversation to prevent concurrent history updates.
- Bound memory usage for attachments, combined input, API responses, and history; apply timeouts to network requests.
- Extract PDF text using separate temporary files and resource-limited child processes to prevent file conflicts and excessively long parsing.
- The retired Bing Search API is disabled. Former search channels return an explanation.

The current chat model is `gpt-6-luna`, and the moderation model is `omni-moderation-latest`. The application does not explicitly set `max_completion_tokens` or `max_tokens`, and does not impose an application-level token cap.

## Environment and dependencies

The target platform is Linux. Requirements:

- A C++17 compiler and CMake 3.22 or later.
- The DPP development library, including `dpp/dpp.h` and `libdpp`. Follow the [official DPP documentation](https://dpp.dev/) to install it.
- libcurl 7.85 or later with asynchronous DNS enabled.
- `pdftotext`, provided by `poppler-utils`, at `/usr/bin/pdftotext`.
- `prlimit`, provided by `util-linux`, at `/usr/bin/prlimit`.
- nlohmann/json: CMake downloads v3.11.2 through FetchContent during the initial configuration, which requires network access.

On Debian / Raspberry Pi OS, install these general dependencies first, then install DPP:

```bash
sudo apt update
sudo apt install build-essential cmake git pkg-config libcurl4-openssl-dev poppler-utils util-linux
```

When building with the Makefile, the compiler must also be able to find the nlohmann/json headers, for example by installing `nlohmann-json3-dev`.

## Configuration

Create `config.json` in the project root:

```json
{
  "BOT_TOKEN": "YOUR_DISCORD_BOT_TOKEN",
  "OPENAI_API_KEY": "YOUR_OPENAI_API_KEY",
  "IMAGE_MODEL": "gpt-image-2.5-flare"
}
```

`IMAGE_MODEL` is optional and defaults to `gpt-image-2.5-flare`. Image requests return one 1024×1024 PNG at medium quality. The image API requires access to the selected model, and image generation and editing incur API charges.

`BING_API_KEY` is no longer required. `config.json` is excluded by `.gitignore`; do not commit actual credentials.

The application currently reads configuration from `../config.json` relative to its working directory. Run it from `build/` or `src/`, rather than directly from the repository root.

### Discord setup

1. Create a Discord Application and Bot, obtain the Bot Token, and invite the bot to your server.
2. Enable **Message Content Intent** on the Bot page in the Developer Portal.
3. Grant permissions to view the target channels, send messages, read message history, and attach files. Deleting messages rejected by moderation also requires **Manage Messages**.
4. Update the channel lists in `src/main.cpp` and rebuild:
   - `GPT_CHANNEL_IDS`: chat channels.
   - `BING_CHANNEL_IDS`: disabled search channels.
   - `STARTUP_CHANNEL_IDS`: channels for startup and interactive shutdown notifications.

The channel IDs in the repository belong to the existing deployment. Replace them when deploying to your own server. The bot ignores other bots and messages containing user mentions.

Some slash command handlers remain in the source, but automatic registration is currently commented out. By default, interact with the bot using regular messages in the configured channels.

## Using images

| Action | What to send in Discord |
| --- | --- |
| Ask about an image / read text in a screenshot | Upload a PNG, JPG/JPEG, or WebP and include a question. If there is no text, the bot describes the image. |
| Generate an image | `Draw a goose riding a bicycle and send me the image`, or `!image A goose riding a bicycle, seaside sunset, illustration style` |
| Edit an image | Attach an image to the current message and say `Keep the subject and only change the background to a seaside sunset`. You can also use `!image Change the background to snowy mountains` or `!edit Change the background to snowy mountains`. |
| Chinese commands | `画图：description`, `生成图片：description`, `修改图片：instructions` |

Text without a command prefix triggers one additional intent-classification request to distinguish image generation/editing from image questions, HTML/SVG code requests, and questions about using tools. This adds a small amount of API usage and latency. Explicit image commands skip this step. If intent classification fails, the bot explains the reason and suggests available commands.

Once generation or editing finishes, the bot sends `image_<message_id>.png`. A message can include multiple images, subject to the overall limit of 5 attachments. Image contents are validated and moderated together with the text. GIF, SVG, and HEIC are not currently supported as image inputs; convert them to PNG/JPEG/WebP.

Images are used only for the current request, and their Base64 data is not stored in history. Reattach the image for further edits. Text replies can still refer to the previous turn's written description.

## Build and run

### CMake (recommended)

```bash
git clone https://github.com/Bigfish3012/dc-bot-bigfish.git
cd dc-bot-bigfish
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
cd build
./discord-bot
```

Before running the bot for the first time, configure the root-level `config.json` and channel IDs. In an interactive terminal, enter `quit` and confirm to exit.

### Makefile

Run from the repository root:

```bash
make -C src -j2
cd src
./bot
```

The Makefile compiles `main.cpp`, `helper.cpp`, `pdf_extract.cpp`, and `media.cpp`, links DPP, CURL, and pthread, and automatically generates `.d` files for header dependencies.

## Running in the background and updating

You can manage the bot with systemd. The repository does not automatically install or modify system services. The existing Raspberry Pi deployment uses `dcbot.service` with these key settings:

```ini
WorkingDirectory=/home/taotao/dc-bot-bigfish/build
ExecStart=/home/taotao/dc-bot-bigfish/build/discord-bot
```

After manually updating and rebuilding, restart the existing service:

```bash
cd ~/dc-bot-bigfish
git pull --ff-only
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
sudo systemctl restart dcbot.service
systemctl status dcbot.service --no-pager
```

**Running only `git pull` or restarting the service does not compile the updated program.** Building `src/bot` with the Makefile also does not update `build/discord-bot`, which the current systemd service uses.

View logs:

```bash
journalctl -u dcbot.service -n 100 --no-pager
journalctl -u dcbot.service -f
```

## Current default limits

Most parameters are defined in `include/runtime_limits.h`. Rebuild after changing them.

| Item | Default |
| --- | --- |
| Worker threads | 2 |
| Total running and queued tasks | 32 |
| Running and queued tasks per conversation | 4 |
| Maximum queue wait | 2 minutes (defined in `main.cpp`) |
| Attachment count | 5 |
| Single image / single document | 8 MiB / 2 MiB |
| Total attachments / document portion | 16 MiB / 4 MiB |
| Generated image / image API response body | 8 MiB / 16 MiB |
| Image generation or editing timeout | 240 seconds |
| Image prompt | 32000 bytes |
| Combined message and attachment text | 64 KiB |
| Chat / moderation response body | 1 MiB / 128 KiB |
| Conversation history | Up to 128 conversations, with up to 15 turns and 256 KiB per conversation |
| Connection / download / moderation / chat timeout | 10 / 30 / 20 / 180 seconds |
| PDF parsing | 15 seconds wall time, 10 seconds CPU time, 256 MiB address space |

Low-speed protection applies only to attachment downloads: a transfer is aborted if it stays below 32 bytes/second for 15 seconds. Non-streaming chat and moderation requests may receive no response data while waiting and use their respective total timeouts.

Replies longer than 1900 length units are sent as attachments: Chinese characters count as 1 unit, while non-BMP characters conservatively count as 2. Original attachment contents are used only for the current request. History stores the user's message text or an attachment-only placeholder, together with the model's reply. Attachment excerpts quoted in a model reply remain in history as part of that reply.

## Troubleshooting

### The old “处理消息时发生错误，请稍后重试。” message

This means “An error occurred while processing the message. Please try again later.” It was a generic message in the old exception handler in `src/main.cpp`, not a model-generated response. The current version provides categorized explanations; unknown internal errors include a message ID for matching against the logs.

Known errors report specific causes, such as DNS/TLS/connection timeouts, HTTP 401 for an invalid API key, 403 for insufficient permissions, 429 for rate limits or insufficient balance, invalid image formats, PDF parsing failures, empty response text, model length limits, and Discord attachment delivery failures. Unknown internal errors include a message ID for an administrator to locate in the logs. Keys, request contents, and raw server error text are not exposed in the channel.

### Long requests fail while short chats work

First, check that the running binary has been rebuilt. The previous 4096-token budget could be consumed entirely by reasoning, resulting in `finish_reason=length` and an empty response body. The current version removes that explicit budget.

Response logs record `finish_reason`, `completion_tokens`, `reasoning_tokens`, and `text_bytes`, without logging the model's reply or reasoning contents. Incomplete output with `finish_reason=length` is neither delivered as complete code nor added to history.

### HTTP timeouts

`HTTP request failed` logs include the operation type, HTTP status, CURL error code, and timings for DNS, connection, TLS, first byte, and the total request. `status=0` means no HTTP status code was received; use the remaining fields to diagnose the cause.

### PDF extraction failures

Check that both tools listed above exist, that the PDF contains extractable text rather than only scanned images, and that it is within the size, time, memory, and text-length limits. PDF OCR is not currently implemented.

## Tests

Run the basic tests from the project root:

```bash
mkdir -p build/local-checks
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread tests/runtime_tests.cpp -o build/local-checks/runtime_tests
./build/local-checks/runtime_tests
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror tests/text_tests.cpp -o build/local-checks/text_tests
./build/local-checks/text_tests
```

These cover input limits, integer overflow, ordering within a conversation, parallelism across conversations, overload rejection, exception recovery, an 800-task concurrency check, and Chinese/English/emoji text boundaries.

After a CMake build, you can run offline API response parsing tests without calling OpenAI:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread \
  -Ibuild/_deps/nlohmann_json-src/include \
  tests/chat_response_tests.cpp \
  build/CMakeFiles/discord-bot.dir/src/helper.cpp.o \
  build/CMakeFiles/discord-bot.dir/src/media.cpp.o \
  -ldpp -lcurl -o build/local-checks/chat_response_tests
./build/local-checks/chat_response_tests
```

Offline image module tests (no API calls):

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -Ibuild/_deps/nlohmann_json-src/include tests/media_tests.cpp src/media.cpp \
  -o build/local-checks/media_tests
./build/local-checks/media_tests
```

`tests/pdf_tests.cpp` checks invalid PDF input, size limits, and isolation under concurrent execution. Set `TMPDIR` to keep its temporary files inside the project. `tests/image_smoke.cpp` is a manual integration test tool that uses the root-level configuration to call the real OpenAI API. Image generation/editing incurs charges, so it should not run automatically on every build.

## Project layout

```text
include/             Interfaces, queue, text utilities, and resource limits
src/main.cpp         Discord events, task scheduling, moderation, and conversation management
src/helper.cpp       Configuration loading, HTTP requests, and API response parsing
src/pdf_extract.cpp  PDF child processes, temporary files, and specific failure reasons
src/media.cpp        Image encoding/decoding, multimodal requests, and API error classification
src/Makefile         Standalone Make build
CMakeLists.txt       Recommended build entry point
tests/               Concurrency, text, response parsing, and PDF tests
HARDENING.md         Stability changes and validation records
```

Conversation history is stored only in memory and is cleared on restart. Dedicated SIGTERM shutdown handling is not currently implemented; see `HARDENING.md` for details.
