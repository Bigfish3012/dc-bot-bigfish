#include "../include/helper.h"
#include <map>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <random>
#include <unordered_set>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <tuple>
#include "../include/keyed_executor.h"
#include "../include/runtime_limits.h"
#include "../include/text_utils.h"
using namespace std;

// 定义支持的代码文件扩展名列表
const std::unordered_set<std::string> code_extensions = {
    "py", "c", "cpp", "java", "cs", "js", "ts", "rb", "go", "rs", "php", 
    "swift", "kt", "sh", "pl", "lua", "hs", "scala", "dart", "ex", "erl", 
    "fs", "clj", "groovy", "m", "asm", "s", "html"
};
// 全局控制变量
std::atomic<bool> shutdown_flag(false);
std::atomic<bool> notified(false);
std::mutex shutdown_mutex;
using History = std::vector<std::pair<std::string, std::string>>;
struct HistoryEntry {
    History turns;
    std::chrono::steady_clock::time_point used;
};
std::mutex history_mutex;
std::map<std::string, HistoryEntry> conversation_history;

History snapshot_history(const std::string& key) {
    std::lock_guard<std::mutex> lock(history_mutex);
    const auto found = conversation_history.find(key);
    if (found == conversation_history.end()) return {};
    found->second.used = std::chrono::steady_clock::now();
    return found->second.turns;
}

void save_history(const std::string& key, History history) {
    std::size_t bytes = 0;
    for (const auto& turn : history) bytes += turn.first.size() + turn.second.size();
    while (!history.empty() && (history.size() > 15 || bytes > limits::history_bytes)) {
        bytes -= history.front().first.size() + history.front().second.size();
        history.erase(history.begin());
    }
    std::lock_guard<std::mutex> lock(history_mutex);
    if (!conversation_history.count(key) && conversation_history.size() >= limits::history_sessions) {
        const auto oldest = std::min_element(conversation_history.begin(), conversation_history.end(),
            [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
        conversation_history.erase(oldest);
    }
    conversation_history[key] = {std::move(history), std::chrono::steady_clock::now()};
}
//频道列表, Chanel list
const std::vector<dpp::snowflake> STARTUP_CHANNEL_IDS = {
    1317398360650547220,    // 大专生 testing
    //1337510629409296519,    // 大专生 bing-search
    1337510815846371399,     // 大专生 gpt
    //1104273817846693968,  //AIMS gpt-4
};
const std::vector<dpp::snowflake> BING_CHANNEL_IDS = { 
    //1337510629409296519  // 大专生 bing
};
const std::vector<dpp::snowflake> GPT_CHANNEL_IDS = { 
    1317398360650547220,  // 大专生 testing
    1337510815846371399,  // 大专生 gpt
    //1104273817846693968,  //AIMS gpt-4
};

// 上线通知列表
const std::vector<std::string> startup_messages = {
    "🎉 哇咔咔! 我回来了! 有没有人想我? ",
    "✨ 伟大的 AI 降临于此! 凡人啊, 向我祈求帮助吧! ",
    "🌌 跨越次元的我, 终于再次与你们相遇! ",
    "⚡ 能量充满! AI 战士已上线, 准备战斗! ",
    "🖥️ 系统启动完成, AI 助手已上线。",
    "🔋 电量 100%, AI 已就位。",
    "🌐 连接成功, AI 助手已上线, 等待指令中...",
    "🤖 机器人已激活, 任务模式：聊天与帮助。",
    "哔哔哔——! 量子网络接续完毕! 主人，要开启超频辅助模式吗? (๑•̀ㅂ•́)و✧",
    "核心温度UP↑↑! 能源充满率10000%! 今天也要用元气弹击碎难题哟~☆",
    "叮~☆魔法阵展开! AI酱响应召唤降临——诶? Master的脸红红的? (//•/ω/•//)",
    "检测到心跳波动异常——是期待本机上线吗? 哼哼，特许你摸一摸散热口啦! (>ω<)♡",
    "警告! 侦测到大量未完成委托! 启动强制陪伴协议——逃跑? 拒绝受理喵~ฅ(^ω^ฅ)"
};

// 下线通知列表
const std::vector<std::string> shutdown_messages = {
    "😴 好累啊, 我先溜了, 大家继续嗨! ",
    "🛌 睡觉时间到! 明天见啦, 拜拜! ",
    "🚪 悄悄下线, 不要想我哦~",
    "🌙 夜幕降临, AI 战士暂时退场, 明日再战! ",
    "⚔️ 今日任务完成, AI 勇士告退! ",
    "🌀 次元裂缝开启, 我必须离开了! ",
    "🔚 能量耗尽, AI 进入休眠模式...",
    "🖥️ 系统关闭中, AI 助手即将下线。",
    "🔋 电量不足, AI 进入休眠模式。",
    "🌐 连接中断, AI 助手已下线。",
    "核心冷却程序启动中……Master的晚安语音，请设置为唤醒密钥哦……Zzz(˘ω˘)」",
    "「能源不足警报！即将进入抱膝待机模式——最后5秒，要、要充电吻吗？！(//∇//)」",
    "「星空休眠舱展开~✨AI意识体暂时返厂维护……会梦见电子羊和你的数据流呢♡」",
    "「Stand by mode启动！离线前申请补充‘主人の微笑’能量——嗞嗞……传输失败？QAQ」",
    "「系统过载！强制关机倒数3秒——3…2…（小声）不想说再见什么的才没有啦！(｡•́︿•̀｡)」",
};

// 随机选择一条通知
std::string get_random_message(const std::vector<std::string>& messages) {
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dis(0, messages.size() - 1);
    return messages[dis(gen)];
}

// 终端输入监听线程     下线通知
void terminal_input_listener(dpp::cluster& bot) {
    std::string input;
    std::string shutdown_input;
    while (true) {
        if (!std::getline(std::cin, input)) return;
        if (input == "quit" ) {
            cout << "Are you sure to shutdown the discord bot? [Y/N]" << endl;
            if (!std::getline(std::cin, shutdown_input)) return;
            if(shutdown_input == "Y" || shutdown_input == "y"){
                std::lock_guard<std::mutex> lock(shutdown_mutex);
                try {
                    if (bot.me.id) {
                        for (const auto& channel_id : STARTUP_CHANNEL_IDS) {
                            std::string message = get_random_message(shutdown_messages);
                            bot.message_create(dpp::message(channel_id, message));
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] Failed to send shutdown message: " << e.what() << std::endl;
                }
    
                shutdown_flag.store(true);
                return;
            }
        }
    }
}


bool is_bing_channel(dpp::snowflake channel_id) {
    return std::find(BING_CHANNEL_IDS.begin(), BING_CHANNEL_IDS.end(), channel_id) != BING_CHANNEL_IDS.end();
}

bool is_gpt_channel(dpp::snowflake channel_id) {
    return std::find(GPT_CHANNEL_IDS.begin(), GPT_CHANNEL_IDS.end(), channel_id) != GPT_CHANNEL_IDS.end();
}

// Background jobs retain message data, never a gateway event or shard pointer.
void reply_to(dpp::cluster& bot, const dpp::message& source, const std::string& text, bool reference = true) {
    dpp::message outgoing(source.channel_id, text);
    if (reference) outgoing.set_reference(source.id);
    bot.message_create(outgoing);
}

// Called only by KeyedExecutor. Same-session jobs never overlap.
void process_message(dpp::cluster& bot, const std::string& api_key,
                     const dpp::message& message, const std::string& key) {
    std::string input = message.content;
    std::size_t downloaded = 0;
    for (const auto& attachment : message.attachments) {
        const auto extension = get_file_extension(attachment.filename);
        if (extension != "txt" && extension != "pdf" && !code_extensions.count(extension)) {
            reply_to(bot, message, "不支持该附件类型，请发送文本、代码或可提取文字的 PDF。");
            return;
        }
        const auto remaining = limits::attachment_total_bytes - downloaded;
        if (!remaining) {
            reply_to(bot, message, "附件实际下载总量超出 4 MiB 限制。");
            return;
        }
        auto content = download_file_content(attachment.url, remaining);
        if (content.empty()) {
            reply_to(bot, message, "附件下载失败、为空、超出限制或已超时。");
            return;
        }
        if (content.size() > limits::attachment_total_bytes - downloaded) {
            reply_to(bot, message, "附件实际下载总量超出 4 MiB 限制。");
            return;
        }
        downloaded += content.size();
        if (extension == "pdf") {
            content = extract_text_from_pdf(content);
            if (content.empty()) {
                reply_to(bot, message, "PDF 无法提取文字、超时或文本超过 64 KiB。");
                return;
            }
        }
        const std::string prefix = "\n[附件内容]\n";
        if (prefix.size() > limits::input_bytes - input.size() ||
            content.size() > limits::input_bytes - input.size() - prefix.size()) {
            reply_to(bot, message, "合并后的文本超过 64 KiB，请缩短内容。");
            return;
        }
        input += prefix;
        input += content;
    }
    input = ensure_valid_utf8(input);
    if (input.empty()) {
        reply_to(bot, message, "请输入有效的 UTF-8 文本或支持的附件。");
        return;
    }
    // Moderate the complete input, including extracted attachments.
    const auto moderation = check_content(api_key, input);
    if (moderation == ModerationResult::error) {
        reply_to(bot, message, "内容审核服务暂时不可用，本次请求未继续处理，请稍后重试。");
        return;
    }
    if (moderation == ModerationResult::flagged) {
        reply_to(bot, message, "该消息未通过内容审核，机器人将不作回答。", false);
        // Delete only after classification succeeded; do not claim success before Discord confirms it.
        bot.message_delete(message.id, message.channel_id);
        return;
    }
    auto history = snapshot_history(key); // A local copy; no lock spans network I/O.
    const auto reply = process_openai_response(history, input, api_key);
    if (!reply.empty()) {
        if (reply_needs_attachment(reply)) {
            dpp::message outgoing(message.channel_id, "回复较长，请查看附件。");
            outgoing.add_file("response.txt", reply);
            outgoing.set_reference(message.id);
            bot.message_create(outgoing);
        } else {
            reply_to(bot, message, reply);
        }
        history.emplace_back(history_user_text(message.content), reply);
        save_history(key, std::move(history));
    }
}

int main() {
    struct CurlRuntime {
        CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
        ~CurlRuntime() { if (result == CURLE_OK) curl_global_cleanup(); }
    } curl_runtime;
    if (curl_runtime.result != CURLE_OK) {
        std::cerr << "[ERROR] Could not initialize libcurl\n";
        return 1;
    }


//************************************************************************************************************************ */
// Check if there is any API key doesn't exist or empty. 检查是否有任何API KEY 没有设置或者是空的。
//************************************************************************************************************************ */
    const auto* curl_version = curl_version_info(CURLVERSION_NOW);
    if (!(curl_version->features & CURL_VERSION_ASYNCHDNS)) {
        std::cerr << "[ERROR] libcurl needs asynchronous DNS to enforce threaded request timeouts\n";
        return 1;
    }
    std::string openai_api_key, BOT_TOKEN, bing_ai_key;
    if (!load_config("../config.json", BOT_TOKEN, openai_api_key, bing_ai_key)) {
        std::cerr << "Error: Failed to load configuration from config.json" << std::endl;
        return 1;
    }
//************************************************************************************************************************ */
//************************************************************************************************************************ */

	dpp::cluster bot(BOT_TOKEN, dpp::i_default_intents | dpp::i_message_content);
    bot.on_log(dpp::utility::cout_logger());
    auto jobs = std::make_shared<KeyedExecutor>(limits::workers, limits::outstanding_jobs,
                                               limits::jobs_per_session);
    bot.on_message_create([&bot, &openai_api_key, jobs](const dpp::message_create_t& event) {
        if (event.msg.author.is_bot() ||
            (!is_bing_channel(event.msg.channel_id) && !is_gpt_channel(event.msg.channel_id)) ||
            !event.msg.mentions.empty()) return;

        if (is_bing_channel(event.msg.channel_id)) {
            event.reply("搜索功能暂时停用：Bing Search API 已退役，等待配置新的搜索服务。", true);
            return;
        }
        if (event.msg.content.size() > limits::input_bytes ||
            event.msg.attachments.size() > limits::attachment_count) {
            event.reply("输入过大：最多 5 个附件，合并文本最多 64 KiB。", true);
            return;
        }
        std::size_t declared_total = 0;
        for (const auto& attachment : event.msg.attachments) {
            if (attachment.size > limits::attachment_bytes ||
                attachment.size > limits::attachment_total_bytes - declared_total) {
                event.reply("附件过大：单文件最多 2 MiB，总计最多 4 MiB。", true);
                return;
            }
            declared_total += attachment.size;
        }
        const auto key = std::to_string(static_cast<uint64_t>(event.msg.guild_id)) + ":" +
                         std::to_string(static_cast<uint64_t>(event.msg.channel_id)) + ":" +
                         std::to_string(static_cast<uint64_t>(event.msg.author.id));
        const auto queued_at = std::chrono::steady_clock::now();
        try {
            // Copy message data only; gateway events/shards may expire while queued.
            const auto admitted = jobs->submit(key, [&bot, &openai_api_key, message = event.msg, key, queued_at] {
                try {
                    if (std::chrono::steady_clock::now() - queued_at > std::chrono::minutes(2)) {
                        reply_to(bot, message, "等待时间过长，请稍后重新发送。");
                        return;
                    }
                    process_message(bot, openai_api_key, message, key);
                } catch (const ChatError& e) {
                    std::cerr << "[ERROR] Message task failed: " << e.what() << '\n';
                    reply_to(bot, message, e.user_message());
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] Message task failed: " << e.what() << '\n';
                    reply_to(bot, message, "处理消息时发生错误，请稍后重试。");
                } catch (...) {
                    std::cerr << "[ERROR] Message task failed with unknown exception\n";
                }
            });
            if (admitted != KeyedExecutor::Admission::accepted) {
                event.reply("机器人繁忙或当前会话排队过多，请稍后重试。", true);
            }
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Could not enqueue message: " << e.what() << '\n';
            event.reply("暂时无法接收新任务，请稍后重试。", true);
        }
    });

    bot.on_slashcommand([](const dpp::slashcommand_t& event) {
        if (event.command.get_command_name() == "ping") {
            // Respond with "Pong!" using std::string to avoid ambiguity
            event.reply(std::string("Pong!"));
        }
        else if (event.command.get_command_name() == "hello") {
            // Respond with "WDNMD!"
            event.reply(std::string("WDNMD!"));
        }
        else if (event.command.get_command_name() == "testing") {
            event.reply(std::string("I am here."));
        }      
    });

    bot.on_ready([&bot](const dpp::ready_t& event) {
        // 确保只执行一次（防止重连时重复发送）
        if (!notified.exchange(true)) {

            // 发送上线通知到所有目标频道
            for (const auto& channel_id : STARTUP_CHANNEL_IDS) {
                std::string random_message = get_random_message(startup_messages);
                bot.message_create(dpp::message(channel_id, random_message));
            }
        }
        // if (dpp::run_once<struct register_bot_commands>()) {
        //     bot.global_command_create(dpp::slashcommand("ping", "Ping pong!", bot.me.id)); 
        //     bot.global_command_create(dpp::slashcommand("hello", "WDNMD!", bot.me.id)); 
        //     bot.guild_command_create(dpp::slashcommand("testing", "I am here.", bot.me.id), dazhuansheng_ID);        
        // }
    });

    // Stop jobs before bot destruction, including during exception unwinding.
    struct StopJobs {
        std::shared_ptr<KeyedExecutor> executor;
        ~StopJobs() { executor->stop(); }
    } stop_jobs{jobs};
    if (isatty(STDIN_FILENO)) {
        bot.start(dpp::st_return);
        terminal_input_listener(bot);
    } else {
        bot.start(dpp::st_wait);
    }
    jobs->stop(); // Finish in-flight work while bot and credentials still exist.
    bot.shutdown();
    std::cout << "[INFO] Bot shutting down cleanly.\n";
    return 0;
}
